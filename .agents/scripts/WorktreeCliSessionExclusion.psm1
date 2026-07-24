Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1')

$script:LedgerVersion = 2
$script:DefaultWaitSeconds = 660

function Get-WorktreeCliRepositoryIdentity([string] $RepositoryRoot) {
	$root = Get-AgentCanonicalPath $RepositoryRoot
	$common = @(& git -C $root rev-parse --path-format=absolute --git-common-dir 2>&1)
	if ($LASTEXITCODE -ne 0 -or $common.Count -ne 1) { throw "Unable to resolve Git common directory for '$root': $($common -join '; ')." }
	$common = Get-AgentCanonicalPath $common[0].Trim()
	$sha256 = [Security.Cryptography.SHA256]::Create()
	try { $sha = $sha256.ComputeHash([Text.Encoding]::UTF8.GetBytes($common.ToUpperInvariant())) } finally { $sha256.Dispose() }
	$hash = [BitConverter]::ToString($sha).Replace('-', '').ToLowerInvariant()
	$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value.Replace('-', '_')
	$directory = Join-Path $env:LOCALAPPDATA 'BrokenEngineLocks'
	return [pscustomobject]@{
		Repository = $common
		LedgerPath = Join-Path $directory "worktreecli-sessions-$hash.json"
		MutexName = "Global\BrokenEngine_WorktreeCli_${sid}_$hash"
	}
}

function Get-ProcessStartUtc([int] $ProcessId) {
	try { return [Diagnostics.Process]::GetProcessById($ProcessId).StartTime.ToUniversalTime().ToString('O') }
	catch [ArgumentException] { return $null }
	catch { throw "Unable to verify process $ProcessId liveness: $($_.Exception.Message)" }
}

function Test-ClaimProcessLive($Claim) {
	$actual = Get-ProcessStartUtc ([int]$Claim.pid)
	if ($null -eq $actual) { return $false }
	return $actual -eq $Claim.processStartUtc
}

function Test-StrictString($Value) { return $Value -is [string] -and -not [string]::IsNullOrWhiteSpace($Value) }

function Test-StrictPid($Value) {
	return ($Value -is [int] -or $Value -is [long]) -and $Value -ge 1 -and $Value -le [int]::MaxValue
}

function Test-StrictUtcRoundTrip($Value) {
	if ($Value -isnot [string]) { return $false }
	$parsed = [DateTime]::MinValue
	if (-not [DateTime]::TryParseExact($Value, 'O', [Globalization.CultureInfo]::InvariantCulture,
		[Globalization.DateTimeStyles]::RoundtripKind, [ref]$parsed)) { return $false }
	return $parsed.Kind -eq [DateTimeKind]::Utc -and $parsed.ToString('O', [Globalization.CultureInfo]::InvariantCulture) -ceq $Value
}

function Test-StrictClaim($Claim, $Identity, $Owners) {
	if ($Claim -isnot [pscustomobject]) { return $false }
	$names = @($Claim.PSObject.Properties.Name)
	$missingNames = @(@('owner', 'pid', 'processStartUtc', 'label', 'repository', 'worktree') | Where-Object { $_ -notin $names })
	if ($names.Count -ne 6 -or $missingNames.Count -ne 0) { return $false }
	$ownerGuid = [guid]::Empty
	if (-not [guid]::TryParseExact($Claim.owner, 'D', [ref]$ownerGuid) -or $ownerGuid.ToString() -cne $Claim.owner) { return $false }
	try { if ((Get-AgentCanonicalPath $Claim.worktree) -cne $Claim.worktree) { return $false } } catch { return $false }
	return (Test-StrictString $Claim.owner) -and (Test-StrictPid $Claim.pid) -and (Test-StrictUtcRoundTrip $Claim.processStartUtc) -and
		(Test-StrictString $Claim.label) -and (Test-StrictString $Claim.repository) -and
		$Claim.repository.Equals($Identity.Repository, [StringComparison]::OrdinalIgnoreCase) -and
		(Test-StrictString $Claim.worktree) -and ($null -eq $Owners -or $Owners.Add($Claim.owner))
}

function Read-WorktreeCliLedger($Identity) {
	if (-not (Test-Path -LiteralPath $Identity.LedgerPath -PathType Leaf)) { return $null }
	try {
		$convertArguments = if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) { @{ DateKind = 'String' } } else { @{} }
		$ledger = Get-Content -Raw -LiteralPath $Identity.LedgerPath | ConvertFrom-Json @convertArguments
	}
	catch { throw "WorktreeCli session ledger is unreadable or malformed: '$($Identity.LedgerPath)': $($_.Exception.Message)" }
	$ledgerNames = if ($ledger -is [pscustomobject]) { @($ledger.PSObject.Properties.Name) } else { @() }
	$missingLedgerNames = @(@('version', 'repository', 'sessions') | Where-Object { $_ -notin $ledgerNames })
	if ($ledger -isnot [pscustomobject] -or $ledgerNames.Count -ne 3 -or $missingLedgerNames.Count -ne 0 -or
		($ledger.version -isnot [int] -and $ledger.version -isnot [long]) -or $ledger.version -ne $script:LedgerVersion -or
		-not (Test-StrictString $ledger.repository) -or
		-not $ledger.repository.Equals($Identity.Repository, [StringComparison]::OrdinalIgnoreCase) -or
		$ledger.sessions -isnot [array]) {
		throw "WorktreeCli session ledger failed validation: '$($Identity.LedgerPath)'."
	}
	$owners = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
	foreach ($claim in @($ledger.sessions)) {
		if (-not (Test-StrictClaim $claim $Identity $owners)) {
			throw "WorktreeCli session ledger contains an invalid session claim: '$($Identity.LedgerPath)'."
		}
	}
	return $ledger
}

function Remove-StaleClaims($Ledger) {
	$live = @()
	foreach ($claim in @($Ledger.sessions)) {
		if (Test-ClaimProcessLive $claim) { $live += $claim }
	}
	$Ledger.sessions = @($live)
}

function Write-WorktreeCliLedger($Identity, $Ledger) {
	$directory = Split-Path -Parent $Identity.LedgerPath
	New-Item -ItemType Directory -Path $directory -Force | Out-Null
	$temp = Join-Path $directory ('.' + [guid]::NewGuid().ToString() + '.tmp')
	$bytes = [Text.Encoding]::UTF8.GetBytes(($Ledger | ConvertTo-Json -Depth 8 -Compress))
	$stream = [IO.File]::Open($temp, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
	try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
	try {
		if (Test-Path -LiteralPath $Identity.LedgerPath -PathType Leaf) { [IO.File]::Replace($temp, $Identity.LedgerPath, [System.Management.Automation.Language.NullString]::Value) }
		else { [IO.File]::Move($temp, $Identity.LedgerPath) }
	}
	finally { if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force } }
}

function Invoke-LedgerTransition($Identity, [scriptblock] $Action, [DateTime] $Deadline = [DateTime]::UtcNow.AddSeconds($script:DefaultWaitSeconds)) {
	$mutex = [Threading.Mutex]::new($false, $Identity.MutexName)
	$held = $false
	try {
		$remaining = $Deadline - [DateTime]::UtcNow
		$milliseconds = [Math]::Max(0, [Math]::Min([int]::MaxValue, [Math]::Ceiling($remaining.TotalMilliseconds)))
		try { $held = $mutex.WaitOne([int]$milliseconds) } catch [Threading.AbandonedMutexException] { $held = $true }
		if (-not $held) { throw 'Timed out waiting for WorktreeCli session ledger mutex.' }
		$ledger = Read-WorktreeCliLedger $Identity
		if ($null -ne $ledger) { Remove-StaleClaims $ledger }
		return & $Action $ledger
	}
	finally { if ($held) { $mutex.ReleaseMutex() }; $mutex.Dispose() }
}

function New-Claim([string] $Owner, [string] $Label, [string] $Repository, [string] $Worktree) {
	return [pscustomobject]@{
		owner = $Owner; pid = $PID; processStartUtc = Get-ProcessStartUtc $PID; label = $Label
		repository = $Repository; worktree = Get-AgentCanonicalPath $Worktree
	}
}

function Initialize-WorktreeCliLedger($Identity) {
	return [pscustomobject]@{ version = $script:LedgerVersion; repository = $Identity.Repository; sessions = @() }
}

function Register-WorktreeCliSession {
	[CmdletBinding()] param([string] $RepositoryRoot, [string] $Owner = [guid]::NewGuid().ToString(), [string] $Label, [string] $Worktree,
		[int] $WaitSeconds = $script:DefaultWaitSeconds)
	$identity = Get-WorktreeCliRepositoryIdentity $RepositoryRoot
	$deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
	# One mutex-guarded transition: the ledger is created on demand, a duplicate owner is
	# refused, and the claim is appended. Duplicate worktrees are legal because two transient
	# operations may run in one worktree at once.
	Invoke-LedgerTransition $identity {
		param($ledger)
		if ($null -eq $ledger) { $ledger = Initialize-WorktreeCliLedger $identity }
		if (@($ledger.sessions | Where-Object { $_.owner -eq $Owner }).Count -ne 0) { throw "WorktreeCli session owner '$Owner' already exists." }
		$ledger.sessions = @($ledger.sessions) + (New-Claim $Owner $Label $identity.Repository $Worktree)
		Write-WorktreeCliLedger $identity $ledger
	} $deadline | Out-Null
	return [pscustomobject]@{ Owner = $Owner; Identity = $identity; Mode = 'session' }
}

function Get-WorktreeCliExclusionStatus {
	[CmdletBinding()] param([string] $RepositoryRoot, [int] $WaitSeconds = $script:DefaultWaitSeconds)
	$identity = Get-WorktreeCliRepositoryIdentity $RepositoryRoot
	$deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
	return Invoke-LedgerTransition $identity {
		param($ledger)
		if ($null -eq $ledger) { return [pscustomobject]@{ Initialized = $false; Repository = $identity.Repository; Sessions = @() } }
		return [pscustomobject]@{ Initialized = $true; Repository = $ledger.repository; Sessions = @($ledger.sessions) }
	} $deadline
}

function Wait-WorktreeCliSharedQuiescence {
	[CmdletBinding()] param(
		[Parameter(Mandatory)][string] $RepositoryRoot,
		[string] $CooperatingSessionOwner,
		[ValidateRange(0, 55)][int] $WaitSeconds = 55
	)
	if (-not [string]::IsNullOrWhiteSpace($CooperatingSessionOwner)) {
		$ownerGuid = [guid]::Empty
		if (-not [guid]::TryParseExact($CooperatingSessionOwner, 'D', [ref]$ownerGuid) -or $ownerGuid.ToString() -cne $CooperatingSessionOwner) {
			throw "Cooperating WorktreeCli session owner must be a canonical lowercase GUID: '$CooperatingSessionOwner'."
		}
	}
	$started = [DateTime]::UtcNow
	$deadline = $started.AddSeconds($WaitSeconds)
	do {
		$remaining = [Math]::Max(0, [int][Math]::Ceiling(($deadline - [DateTime]::UtcNow).TotalSeconds))
		$status = Get-WorktreeCliExclusionStatus -RepositoryRoot $RepositoryRoot -WaitSeconds $remaining
		$blockers = @($status.Sessions | Where-Object { [string]::IsNullOrWhiteSpace($CooperatingSessionOwner) -or $_.owner -cne $CooperatingSessionOwner } | ForEach-Object {
			[ordered]@{ kind = 'session'; owner = $_.owner; label = $_.label; worktree = $_.worktree }
		})
		$waitedMilliseconds = [int][Math]::Floor(([DateTime]::UtcNow - $started).TotalMilliseconds)
		if ($blockers.Count -eq 0) {
			return [pscustomobject]@{ schemaVersion = 'broken-engine-shared-quiescence/v1'; disposition = 'quiescent'; requiresUserAuthority = $false; retryAfterSeconds = 0; waitedMilliseconds = $waitedMilliseconds; liveBlockers = @() }
		}
		if ([DateTime]::UtcNow -ge $deadline) {
			return [pscustomobject]@{ schemaVersion = 'broken-engine-shared-quiescence/v1'; disposition = 'shared-quiescence'; requiresUserAuthority = $false; retryAfterSeconds = 5; waitedMilliseconds = $waitedMilliseconds; liveBlockers = @($blockers) }
		}
		Start-Sleep -Milliseconds ([Math]::Min(500, [Math]::Max(1, [int][Math]::Floor(($deadline - [DateTime]::UtcNow).TotalMilliseconds))))
	} while ($true)
}

function Unregister-WorktreeCliSession([string] $RepositoryRoot, [string] $Owner) {
	$identity = Get-WorktreeCliRepositoryIdentity $RepositoryRoot
	Invoke-LedgerTransition $identity {
		param($ledger)
		if ($null -eq $ledger) { throw 'WorktreeCli exclusion ledger is not initialized.' }
		$claim = @($ledger.sessions | Where-Object { $_.owner -eq $Owner })
		if ($claim.Count -ne 1 -or $claim[0].pid -ne $PID -or -not (Test-ClaimProcessLive $claim[0])) { throw "WorktreeCli session release owner mismatch for '$Owner'." }
		$ledger.sessions = @($ledger.sessions | Where-Object { $_.owner -ne $Owner })
		Write-WorktreeCliLedger $identity $ledger
	}
}

# Runs a short action while holding the ledger mutex with no in-flight operation claim other
# than the caller's own cooperating claim (its live claim consents and must not self-block,
# e.g. a landing operation promoting AgentTools it just landed). The mutex blocks concurrent
# registrations for the action's duration, so promotion excludes exactly the operation claims
# live during the swap; an absent ledger means no operation is in flight and the action runs.
function Invoke-WorktreeCliExclusiveOperation {
	[CmdletBinding()] param([string] $RepositoryRoot, [string] $Label, [scriptblock] $Action,
		[string] $CooperatingSessionOwner, [int] $WaitSeconds = $script:DefaultWaitSeconds)
	$identity = Get-WorktreeCliRepositoryIdentity $RepositoryRoot
	$deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
	# Distinct name: inside the transition scriptblock, dynamic scoping would resolve
	# $Action to Invoke-LedgerTransition's own parameter instead of the caller's.
	$exclusiveAction = $Action
	do {
		$outcome = Invoke-LedgerTransition $identity {
			param($ledger)
			$claims = if ($null -eq $ledger) { @() } else { @($ledger.sessions) }
			$blocking = @($claims | Where-Object { [string]::IsNullOrWhiteSpace($CooperatingSessionOwner) -or $_.owner -ne $CooperatingSessionOwner })
			if ($blocking.Count -ne 0) {
				$owners = @($blocking | ForEach-Object { "$($_.owner):$($_.label):$($_.worktree)" }) -join ', '
				Write-Host "Waiting for WorktreeCli operations [$owners] to clear for '$Label'."
				return [pscustomobject]@{ Ran = $false; Value = $null }
			}
			return [pscustomobject]@{ Ran = $true; Value = (& $exclusiveAction) }
		} $deadline
		if ($outcome.Ran) { return $outcome.Value }
		if ([DateTime]::UtcNow -ge $deadline) { throw "Timed out after $WaitSeconds seconds waiting for exclusive WorktreeCli operation '$Label'." }
		$remainingMilliseconds = [Math]::Max(0, [Math]::Floor(($deadline - [DateTime]::UtcNow).TotalMilliseconds))
		if ($remainingMilliseconds -gt 0) { Start-Sleep -Milliseconds ([Math]::Min(5000, $remainingMilliseconds)) }
	} while ($true)
}

if (-not ('BrokenEngine.TrackedProcess' -as [type])) {
	Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace BrokenEngine {
public static class TrackedProcess {
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct STARTUPINFO { public int cb; public string lpReserved, lpDesktop, lpTitle; public int dwX,dwY,dwXSize,dwYSize,dwXCountChars,dwYCountChars,dwFillAttribute,dwFlags; public short wShowWindow,cbReserved2; public IntPtr lpReserved2,hStdInput,hStdOutput,hStdError; }
  [StructLayout(LayoutKind.Sequential)] struct STARTUPINFOEX { public STARTUPINFO StartupInfo; public IntPtr lpAttributeList; }
  [StructLayout(LayoutKind.Sequential)] struct PROCESS_INFORMATION { public IntPtr hProcess,hThread; public int processId,threadId; }
 [StructLayout(LayoutKind.Sequential)] struct BASIC { public long PerProcessUserTimeLimit,PerJobUserTimeLimit; public uint LimitFlags; public UIntPtr MinimumWorkingSetSize,MaximumWorkingSetSize; public uint ActiveProcessLimit; public UIntPtr Affinity; public uint PriorityClass,SchedulingClass; }
 [StructLayout(LayoutKind.Sequential)] struct IO { public ulong ReadOperationCount,WriteOperationCount,OtherOperationCount,ReadTransferCount,WriteTransferCount,OtherTransferCount; }
 [StructLayout(LayoutKind.Sequential)] struct EXTENDED { public BASIC BasicLimitInformation; public IO IoInfo; public UIntPtr ProcessMemoryLimit,JobMemoryLimit,PeakProcessMemoryUsed,PeakJobMemoryUsed; }
  [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool CreateProcess(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string cwd, ref STARTUPINFOEX si, out PROCESS_INFORMATION pi);
 [DllImport("kernel32", SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr a,string n);
 [DllImport("kernel32", SetLastError=true)] static extern bool SetInformationJobObject(IntPtr j,int c,ref EXTENDED i,uint l);
  [DllImport("kernel32", SetLastError=true)] static extern bool InitializeProcThreadAttributeList(IntPtr l,int c,int f,ref IntPtr s);
  [DllImport("kernel32", SetLastError=true)] static extern bool UpdateProcThreadAttribute(IntPtr l,uint f,IntPtr a,IntPtr v,IntPtr s,IntPtr p,IntPtr r);
  [DllImport("kernel32")] static extern void DeleteProcThreadAttributeList(IntPtr l);
 [DllImport("kernel32", SetLastError=true)] static extern uint ResumeThread(IntPtr t);
 [DllImport("kernel32", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h,uint ms);
 [DllImport("kernel32", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr p,out uint c);
 [DllImport("kernel32")] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32")] static extern bool TerminateProcess(IntPtr p,uint c);
  const uint CREATE_SUSPENDED=4, EXTENDED_STARTUPINFO_PRESENT=0x80000, INFINITE=0xffffffff, WAIT_OBJECT_0=0, KILL_ON_CLOSE=0x2000;
  static readonly IntPtr PROC_THREAD_ATTRIBUTE_JOB_LIST=(IntPtr)0x2000d;
 public static int Run(string executable,string commandLine,string workingDirectory) {
  IntPtr job=CreateJobObject(IntPtr.Zero,null); if(job==IntPtr.Zero) throw new Win32Exception();
  IntPtr attributes=IntPtr.Zero,jobList=IntPtr.Zero; bool attributesInitialized=false,processCreated=false;
  var pi=new PROCESS_INFORMATION();
  try {
   var limits=new EXTENDED(); limits.BasicLimitInformation.LimitFlags=KILL_ON_CLOSE;
   if(!SetInformationJobObject(job,9,ref limits,(uint)Marshal.SizeOf(limits))) throw new Win32Exception();
   IntPtr size=IntPtr.Zero; InitializeProcThreadAttributeList(IntPtr.Zero,1,0,ref size); if(size==IntPtr.Zero) throw new Win32Exception();
   attributes=Marshal.AllocHGlobal(size); jobList=Marshal.AllocHGlobal(IntPtr.Size); Marshal.WriteIntPtr(jobList,job);
   if(!InitializeProcThreadAttributeList(attributes,1,0,ref size)) throw new Win32Exception(); attributesInitialized=true;
   if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,jobList,(IntPtr)IntPtr.Size,IntPtr.Zero,IntPtr.Zero)) throw new Win32Exception();
   var si=new STARTUPINFOEX(); si.StartupInfo.cb=Marshal.SizeOf(si); si.lpAttributeList=attributes;
   if(!CreateProcess(executable,commandLine,IntPtr.Zero,IntPtr.Zero,true,CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,IntPtr.Zero,workingDirectory,ref si,out pi)) throw new Win32Exception(); processCreated=true;
   if(ResumeThread(pi.hThread)==0xffffffff) { TerminateProcess(pi.hProcess,995); throw new Win32Exception(); }
   CloseHandle(pi.hThread); pi.hThread=IntPtr.Zero;
   uint wait=WaitForSingleObject(pi.hProcess,INFINITE); if(wait!=WAIT_OBJECT_0) throw new Win32Exception();
   uint code; if(!GetExitCodeProcess(pi.hProcess,out code)) throw new Win32Exception(); return unchecked((int)code);
  }
  finally {
   if(attributesInitialized) DeleteProcThreadAttributeList(attributes);
   if(jobList!=IntPtr.Zero) Marshal.FreeHGlobal(jobList); if(attributes!=IntPtr.Zero) Marshal.FreeHGlobal(attributes);
   if(processCreated) { if(pi.hThread!=IntPtr.Zero) CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
   CloseHandle(job);
  }
 }
}}
'@
}

function ConvertTo-WindowsCommandLineArgument([string] $Argument) {
	if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') { return $Argument }
	$builder = [Text.StringBuilder]::new('"')
	$slashes = 0
	foreach ($character in $Argument.ToCharArray()) {
		if ($character -eq '\') { ++$slashes; continue }
		if ($character -eq '"') { [void]$builder.Append(('\' * ($slashes * 2 + 1))); [void]$builder.Append('"'); $slashes = 0; continue }
		[void]$builder.Append(('\' * $slashes)); $slashes = 0; [void]$builder.Append($character)
	}
	[void]$builder.Append(('\' * ($slashes * 2))); [void]$builder.Append('"')
	return $builder.ToString()
}

function Invoke-WorktreeCliTrackedProcess {
	[CmdletBinding()] param([Parameter(Mandatory)][string] $Executable, [string[]] $ArgumentList = @(), [string] $WorkingDirectory = (Get-Location).Path)
	$application = (Get-Item -LiteralPath $Executable -ErrorAction Stop).FullName
	$parts = @((ConvertTo-WindowsCommandLineArgument $application)) + @($ArgumentList | ForEach-Object { ConvertTo-WindowsCommandLineArgument ([string]$_) })
	return [BrokenEngine.TrackedProcess]::Run($application, ($parts -join ' '), (Get-AgentCanonicalPath $WorkingDirectory))
}

Export-ModuleMember -Function Get-WorktreeCliRepositoryIdentity,Wait-WorktreeCliSharedQuiescence,Register-WorktreeCliSession,Unregister-WorktreeCliSession,Invoke-WorktreeCliExclusiveOperation,Invoke-WorktreeCliTrackedProcess
