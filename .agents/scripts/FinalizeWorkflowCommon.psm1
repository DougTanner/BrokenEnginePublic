Set-StrictMode -Version Latest

$script:FinalizeUtf8 = [Text.UTF8Encoding]::new($false, $true)

function Get-FinalizeRootPreservingFullPath([string] $Path) {
	$full = [IO.Path]::GetFullPath($Path)
	$root = [IO.Path]::GetPathRoot($full)
	if ($full.Length -gt $root.Length) {
		return $full.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
	}
	return $full
}

if (-not ('BrokenEngine.FinalizeWorkflowPathIdentity' -as [type])) {
	Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace BrokenEngine {
public static class FinalizeWorkflowPathIdentity {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 static extern SafeFileHandle CreateFileW(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 static extern uint GetFinalPathNameByHandleW(SafeFileHandle file,StringBuilder path,uint size,uint flags);
 public static string Resolve(string path) {
  using(var handle=CreateFileW(path,0,7,IntPtr.Zero,3,0x02000000,IntPtr.Zero)) {
   if(handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
   var buffer=new StringBuilder(32768); uint length=GetFinalPathNameByHandleW(handle,buffer,(uint)buffer.Capacity,0);
   if(length==0 || length>=buffer.Capacity) throw new Win32Exception(Marshal.GetLastWin32Error());
   string result=buffer.ToString();
   if(result.StartsWith(@"\\?\UNC\",StringComparison.OrdinalIgnoreCase)) return @"\\"+result.Substring(8);
   if(result.StartsWith(@"\\?\",StringComparison.OrdinalIgnoreCase)) return result.Substring(4);
   return result;
  }
 }
}}
'@
}

function Get-FinalizeExistingWindowsIdentity([string] $Path, [string] $Label = 'Path') {
	$full = Get-FinalizeRootPreservingFullPath $Path
	if (-not (Test-Path -LiteralPath $full)) { throw "$Label does not exist: '$full'." }
	try {
		return Get-FinalizeRootPreservingFullPath ([BrokenEngine.FinalizeWorkflowPathIdentity]::Resolve($full))
	}
	catch {
		throw "Unable to resolve $Label identity '$full': $($_.Exception.Message)"
	}
}

function Test-FinalizeExistingIdentityEqual([string] $Candidate, [string] $Expected) {
	try {
		if (-not (Test-Path -LiteralPath (Get-FinalizeRootPreservingFullPath $Candidate))) { return $false }
		return (Get-FinalizeExistingWindowsIdentity $Candidate 'Registered worktree').Equals($Expected, [StringComparison]::OrdinalIgnoreCase)
	}
	catch {
		return $false
	}
}

function Invoke-FinalizeNativeText([string] $Executable, [string[]] $Arguments, [string] $WorkingDirectory) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $Executable
	$start.WorkingDirectory = $WorkingDirectory
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = $script:FinalizeUtf8
	$start.StandardErrorEncoding = $script:FinalizeUtf8
	foreach ($argument in $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start '$Executable'." }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$result = [pscustomobject]@{
		ExitCode = $process.ExitCode
		Stdout = $stdoutTask.GetAwaiter().GetResult()
		Stderr = $stderrTask.GetAwaiter().GetResult()
	}
	$process.Dispose()
	return $result
}

function Invoke-FinalizeGit([string] $Worktree, [string[]] $Arguments) {
	$response = Invoke-FinalizeNativeText 'git.exe' (@('-C', $Worktree) + $Arguments) $Worktree
	if ($response.ExitCode -ne 0) { throw "git $($Arguments -join ' ') failed: $($response.Stderr.Trim())." }
	return $response.Stdout
}

function Test-FinalizeGitSuccess([string] $Worktree, [string[]] $Arguments) {
	return (Invoke-FinalizeNativeText 'git.exe' (@('-C', $Worktree) + $Arguments) $Worktree).ExitCode -eq 0
}

function Get-FinalizeGitIdentity([string] $Worktree, [string] $Label = 'Worktree') {
	$identity = Get-FinalizeExistingWindowsIdentity $Worktree $Label
	$topLevel = Get-FinalizeExistingWindowsIdentity ((Invoke-FinalizeGit $identity @('rev-parse', '--show-toplevel')).Trim()) "$Label Git top-level"
	if (-not $identity.Equals($topLevel, [StringComparison]::OrdinalIgnoreCase)) {
		throw "$Label is not a Git top-level: '$identity'."
	}
	$common = Get-FinalizeExistingWindowsIdentity ((Invoke-FinalizeGit $identity @('rev-parse', '--path-format=absolute', '--git-common-dir')).Trim()) "$Label Git common directory"
	$branch = (Invoke-FinalizeGit $identity @('branch', '--show-current')).Trim()
	$head = (Invoke-FinalizeGit $identity @('rev-parse', 'HEAD')).Trim()
	if ([string]::IsNullOrWhiteSpace($branch) -or $head -cnotmatch '^[0-9a-f]{40}$') {
		throw "$Label has an unattached branch or malformed HEAD."
	}
	return [pscustomobject]@{ Worktree = $identity; CommonDirectory = $common; Branch = $branch; Head = $head }
}

function Assert-FinalizeGitPath([string] $Path) {
	if ([string]::IsNullOrEmpty($Path) -or $Path.IndexOf("`0", [StringComparison]::Ordinal) -ge 0 -or
		$Path.IndexOf("`r", [StringComparison]::Ordinal) -ge 0 -or $Path.IndexOf("`n", [StringComparison]::Ordinal) -ge 0 -or
		$Path.Contains('\', [StringComparison]::Ordinal) -or $Path.StartsWith('/', [StringComparison]::Ordinal) -or
		$Path -match '^[A-Za-z]:' -or [IO.Path]::IsPathRooted($Path)) {
		throw "Git path is not canonical repository-relative text: '$Path'."
	}
	foreach ($component in $Path.Split('/')) {
		if ([string]::IsNullOrEmpty($component) -or $component -ceq '.' -or $component -ceq '..') {
			throw "Git path contains an empty or traversing component: '$Path'."
		}
	}
}

function Get-FinalizeWorktreeRecords([string] $Worktree) {
	$records = [Collections.Generic.List[object]]::new()
	$current = $null
	$output = Invoke-FinalizeGit $Worktree @('worktree', 'list', '--porcelain', '-z')
	foreach ($field in $output.Split([char]0, [StringSplitOptions]::RemoveEmptyEntries)) {
		if ($field.StartsWith('worktree ', [StringComparison]::Ordinal)) {
			if ($null -ne $current) { $records.Add([pscustomobject] $current) }
			$current = [ordered]@{ Path = $field.Substring(9); Head = $null; Branch = $null; Prunable = $false; Bare = $false }
		}
		elseif ($null -ne $current -and $field.StartsWith('HEAD ', [StringComparison]::Ordinal)) { $current.Head = $field.Substring(5) }
		elseif ($null -ne $current -and $field.StartsWith('branch refs/heads/', [StringComparison]::Ordinal)) { $current.Branch = $field.Substring(18) }
		elseif ($null -ne $current -and $field.StartsWith('prunable', [StringComparison]::Ordinal)) { $current.Prunable = $true }
		elseif ($null -ne $current -and $field -ceq 'bare') { $current.Bare = $true }
	}
	if ($null -ne $current) { $records.Add([pscustomobject] $current) }
	return $records.ToArray()
}

function Test-FinalizeWorktreeRegistration([string] $RepositoryWorktree, [string] $ExpectedWorktree, [string] $ExpectedBranch, [string] $ExpectedHead) {
	$expectedIdentity = Get-FinalizeExistingWindowsIdentity $ExpectedWorktree 'Expected worktree'
	$records = @(Get-FinalizeWorktreeRecords $RepositoryWorktree | Where-Object {
		-not $_.Prunable -and -not $_.Bare -and (Test-FinalizeExistingIdentityEqual $_.Path $expectedIdentity)
	})
	if ($records.Count -ne 1) { return [pscustomobject]@{ Registered = $false; Message = 'Worktree is not uniquely registered by canonical identity.' } }
	$record = $records[0]
	if ($record.Head -cne $ExpectedHead -or $record.Branch -cne $ExpectedBranch) {
		return [pscustomobject]@{ Registered = $false; Message = 'Registered worktree branch or HEAD differs from the expected identity.' }
	}
	return [pscustomobject]@{ Registered = $true; Message = 'Worktree registration matches canonical identity.' }
}

function Test-FinalizeAllWorktreesClear([string] $RepositoryWorktree) {
	$problems = [Collections.Generic.List[string]]::new()
	$inspected = [Collections.Generic.List[string]]::new()
	foreach ($record in @(Get-FinalizeWorktreeRecords $RepositoryWorktree)) {
		if ($record.Prunable -or $record.Bare) { continue }
		try {
			$worktree = Get-FinalizeExistingWindowsIdentity $record.Path 'Registered worktree'
			$inspected.Add($worktree)
			foreach ($marker in @('MERGE_HEAD', 'rebase-merge', 'rebase-apply', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'BISECT_LOG', 'sequencer')) {
				$markerPath = (Invoke-FinalizeGit $worktree @('rev-parse', '--path-format=absolute', '--git-path', $marker)).Trim()
				if (Test-Path -LiteralPath $markerPath) { $problems.Add("${worktree}: active Git marker $marker") }
			}
		}
		catch {
			$problems.Add("$($record.Path): unreadable ($($_.Exception.Message))")
		}
	}
	return [pscustomobject]@{ Clear = $problems.Count -eq 0; Inspected = $inspected.ToArray(); Problems = $problems.ToArray() }
}

function New-FinalizeLandingSanityResult([bool] $Ok, [string] $Code, [string] $Message, $Session, $Primary, [string] $WorktreeCliExecutable) {
	return [pscustomobject]@{
		Ok = $Ok
		Code = $Code
		Message = $Message
		SessionWorktree = $(if ($null -ne $Session) { $Session.Worktree } else { $null })
		PrimaryWorktree = $(if ($null -ne $Primary) { $Primary.Worktree } else { $null })
		GitCommonDirectory = $(if ($null -ne $Session) { $Session.CommonDirectory } else { $null })
		SessionTip = $(if ($null -ne $Session) { $Session.Head } else { $null })
		PrimaryTip = $(if ($null -ne $Primary) { $Primary.Head } else { $null })
		WorktreeCliExecutable = $WorktreeCliExecutable
	}
}

# Structural sanity shared by approval preparation and landing: two Git top-levels of one
# repository on the expected branches and tips, no Git operation in progress, clean trees,
# and the canonical WorktreeCli the session reaches through its primary Output link.
function Test-FinalizeLandingSanity {
	[CmdletBinding()] param(
		[Parameter(Mandatory)][string] $SessionWorktree,
		[Parameter(Mandatory)][string] $PrimaryWorktree,
		[Parameter(Mandatory)][string] $SessionBranch,
		[Parameter(Mandatory)][string] $PrimaryBranch,
		[string] $ExpectedSessionTip,
		[string] $ExpectedPrimaryTip
	)
	$session = $null
	$primary = $null
	try {
		$session = Get-FinalizeGitIdentity $SessionWorktree 'Session worktree'
		$primary = Get-FinalizeGitIdentity $PrimaryWorktree 'Primary worktree'
	}
	catch { return New-FinalizeLandingSanityResult $false 'identity.unreadable' $_.Exception.Message $session $primary $null }
	if (-not $session.CommonDirectory.Equals($primary.CommonDirectory, [StringComparison]::OrdinalIgnoreCase)) {
		return New-FinalizeLandingSanityResult $false 'identity.repository-mismatch' 'Session and primary worktrees do not share one Git common directory.' $session $primary $null
	}
	if ($session.Worktree.Equals($primary.Worktree, [StringComparison]::OrdinalIgnoreCase)) {
		return New-FinalizeLandingSanityResult $false 'identity.session-is-primary' 'Session landing requires distinct session and primary worktrees.' $session $primary $null
	}
	if ($session.Branch -cne $SessionBranch -or $primary.Branch -cne $PrimaryBranch) {
		return New-FinalizeLandingSanityResult $false 'git.branch-mismatch' 'A checked-out branch differs from its supplied identity.' $session $primary $null
	}
	if (-not [string]::IsNullOrWhiteSpace($ExpectedSessionTip) -and $session.Head -cne $ExpectedSessionTip) {
		return New-FinalizeLandingSanityResult $false 'git.session-tip-changed' 'Session tip differs from the recorded expectation.' $session $primary $null
	}
	if (-not [string]::IsNullOrWhiteSpace($ExpectedPrimaryTip) -and $primary.Head -cne $ExpectedPrimaryTip) {
		return New-FinalizeLandingSanityResult $false 'git.primary-tip-changed' 'Primary tip differs from the recorded expectation.' $session $primary $null
	}
	$executable = $null
	try {
		foreach ($worktree in @($session.Worktree, $primary.Worktree)) {
			foreach ($marker in @('MERGE_HEAD', 'rebase-merge', 'rebase-apply', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'BISECT_LOG', 'sequencer')) {
				$markerPath = (Invoke-FinalizeGit $worktree @('rev-parse', '--path-format=absolute', '--git-path', $marker)).Trim()
				if (Test-Path -LiteralPath $markerPath) {
					return New-FinalizeLandingSanityResult $false 'git.operation-active' "Git operation marker '$marker' is active in '$worktree'." $session $primary $null
				}
			}
		}
		if ((Invoke-FinalizeGit $primary.Worktree @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) {
			return New-FinalizeLandingSanityResult $false 'git.primary-dirty' 'Primary worktree is not clean.' $session $primary $null
		}
		if ((Invoke-FinalizeGit $session.Worktree @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) {
			return New-FinalizeLandingSanityResult $false 'git.session-dirty' 'Session worktree has remaining staged, unstaged, or untracked status.' $session $primary $null
		}
		$relativeOutput = 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
		$primaryOutput = Get-Item -LiteralPath (Join-Path $primary.Worktree $relativeOutput) -Force -ErrorAction Stop
		if (-not $primaryOutput.PSIsContainer -or ($primaryOutput.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
			return New-FinalizeLandingSanityResult $false 'worktreecli.primary-output-invalid' 'Primary WorktreeCli Output must be an ordinary directory.' $session $primary $null
		}
		$executable = Get-Item -LiteralPath (Join-Path $primaryOutput.FullName 'WorktreeCli.exe') -Force -ErrorAction Stop
		if ($executable.PSIsContainer -or ($executable.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $executable.Length -eq 0) {
			return New-FinalizeLandingSanityResult $false 'worktreecli.executable-invalid' 'Canonical WorktreeCli.exe must be a nonempty ordinary file.' $session $primary $null
		}
		$sessionOutput = Get-Item -LiteralPath (Join-Path $session.Worktree $relativeOutput) -Force -ErrorAction Stop
		if (-not $sessionOutput.PSIsContainer -or ($sessionOutput.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
			return New-FinalizeLandingSanityResult $false 'worktreecli.session-output-not-link' 'Session WorktreeCli Output must be a directory link.' $session $primary $null
		}
		if (-not (Test-FinalizeExistingIdentityEqual $sessionOutput.FullName (Get-FinalizeExistingWindowsIdentity $primaryOutput.FullName 'Primary WorktreeCli Output'))) {
			return New-FinalizeLandingSanityResult $false 'worktreecli.session-output-wrong-target' 'Session WorktreeCli Output does not target primary WorktreeCli Output.' $session $primary $null
		}
	}
	catch { return New-FinalizeLandingSanityResult $false 'state.unreadable' $_.Exception.Message $session $primary $null }
	return New-FinalizeLandingSanityResult $true 'ok' 'Landing sanity checks passed.' $session $primary (Get-FinalizeRootPreservingFullPath $executable.FullName)
}

function New-FinalizeLandingLockClaimResult([bool] $Claimed, [string] $Code, [string] $Message, [string] $Disposition, [bool] $RequiresUserAuthority, [int] $RetryAfterMilliseconds, [string] $Owner, $Lock, [int] $Attempts) {
	return [pscustomobject]@{
		Claimed = $Claimed
		Code = $Code
		Message = $Message
		Disposition = $Disposition
		RequiresUserAuthority = $RequiresUserAuthority
		RetryAfterMilliseconds = $RetryAfterMilliseconds
		Owner = $Owner
		Lock = $Lock
		Attempts = $Attempts
	}
}

function ConvertFrom-FinalizeLandingLockJson($Response, [string] $Operation) {
	if ([string]::IsNullOrWhiteSpace($Response.Stdout)) {
		throw "$Operation returned no JSON. stderr: $($Response.Stderr.Trim())"
	}
	try {
		$convertArguments = if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) { @{ DateKind = 'String' } } else { @{} }
		return $Response.Stdout.Trim() | ConvertFrom-Json -Depth 32 -ErrorAction Stop @convertArguments
	}
	catch {
		throw "$Operation returned invalid JSON: $($Response.Stdout.Trim())"
	}
}

function Test-FinalizeLandingLockClaimIdentity($Status, [string] $Owner, [string] $Session, [string] $Worktree) {
	if ($null -eq $Status) { return $false }
	$properties = @($Status.PSObject.Properties.Name)
	if (-not ($properties -ccontains 'owner') -or $Status.owner -isnot [string] -or $Status.owner -cne $Owner -or
		-not ($properties -ccontains 'session') -or $Status.session -isnot [string] -or $Status.session -cne $Session -or
		-not ($properties -ccontains 'worktree') -or $Status.worktree -isnot [string]) {
		return $false
	}
	return Test-FinalizeExistingIdentityEqual $Status.worktree $Worktree
}

function Get-FinalizeLandingLockState([string] $WorktreeCliExecutable, [string] $GitCommonDirectory, [string] $WorkingDirectory) {
	$response = Invoke-FinalizeNativeText $WorktreeCliExecutable @('lock', 'status', '--repo', $GitCommonDirectory) $WorkingDirectory
	$status = ConvertFrom-FinalizeLandingLockJson $response 'landing lock status'
	$properties = @($status.PSObject.Properties.Name)
	if ($response.ExitCode -eq 2 -and $properties -ccontains 'held' -and $status.held -is [bool] -and -not $status.held) {
		return [pscustomobject]@{ Kind = 'absent'; Status = $status; Response = $response; ExpiresAt = $null }
	}
	if ($response.ExitCode -ne 0 -or -not ($properties -ccontains 'held') -or $status.held -isnot [bool] -or -not $status.held) {
		return [pscustomobject]@{ Kind = 'terminal'; Status = $status; Response = $response; ExpiresAt = $null }
	}
	if (-not ($properties -ccontains 'leaseState') -or $status.leaseState -isnot [string]) {
		return [pscustomobject]@{ Kind = 'authority-required'; Status = $status; Response = $response; ExpiresAt = $null }
	}
	if ($status.leaseState -ceq 'unverifiable') {
		return [pscustomobject]@{ Kind = 'authority-required'; Status = $status; Response = $response; ExpiresAt = $null }
	}
	if ($status.leaseState -cnotin @('live', 'expired') -or -not ($properties -ccontains 'owner') -or $status.owner -isnot [string] -or [string]::IsNullOrWhiteSpace($status.owner) -or
		-not ($properties -ccontains 'expiresAt') -or $status.expiresAt -isnot [string]) {
		return [pscustomobject]@{ Kind = 'authority-required'; Status = $status; Response = $response; ExpiresAt = $null }
	}
	$expiresAt = [DateTimeOffset]::MinValue
	if (-not [DateTimeOffset]::TryParse($status.expiresAt, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AssumeUniversal, [ref] $expiresAt)) {
		return [pscustomobject]@{ Kind = 'authority-required'; Status = $status; Response = $response; ExpiresAt = $null }
	}
	return [pscustomobject]@{ Kind = [string]$status.leaseState; Status = $status; Response = $response; ExpiresAt = $expiresAt.ToUniversalTime() }
}

# WorktreeCli owns the bounded wait and the guarded expired-lease recovery, so this helper
# makes one blocking claim and interprets its single outcome. It is shared by reconciliation
# and post-confirmation landing so neither path can steal a live foreign lease or reinterpret
# malformed metadata as a retryable conflict: unverifiable state still requires user authority.
function Invoke-FinalizeLandingLockClaim {
	[CmdletBinding()] param(
		[Parameter(Mandatory)][string] $WorktreeCliExecutable,
		[Parameter(Mandatory)][string] $GitCommonDirectory,
		[Parameter(Mandatory)][string] $Owner,
		[Parameter(Mandatory)][string] $Session,
		[Parameter(Mandatory)][string] $Worktree,
		[ValidateRange(60, 86400)][int] $LeaseSeconds = 3600,
		[ValidateRange(1, 3600)][int] $WaitSeconds = 300,
		[ValidateRange(50, 5000)][int] $PollMilliseconds = 500
	)
	$item = Get-Item -LiteralPath $WorktreeCliExecutable -Force -ErrorAction Stop
	if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.Length -eq 0) {
		throw "WorktreeCli executable must be a nonempty ordinary file: '$WorktreeCliExecutable'."
	}
	if ([string]::IsNullOrWhiteSpace($GitCommonDirectory) -or [string]::IsNullOrWhiteSpace($Owner) -or [string]::IsNullOrWhiteSpace($Session) -or [string]::IsNullOrWhiteSpace($Worktree)) {
		throw 'Landing lock claim requires nonblank repository, owner, session, and worktree identities.'
	}
	$worktreeIdentity = Get-FinalizeExistingWindowsIdentity $Worktree 'Landing worktree'
	$claim = Invoke-FinalizeNativeText $item.FullName @('lock', 'claim', '--repo', $GitCommonDirectory, '--owner', $Owner, '--session', $Session, '--worktree', $worktreeIdentity, '--lease-seconds', [string]$LeaseSeconds, '--wait-seconds', [string]$WaitSeconds, '--poll-milliseconds', [string]$PollMilliseconds) $worktreeIdentity
	if ($claim.ExitCode -eq 0) {
		$status = ConvertFrom-FinalizeLandingLockJson $claim 'landing lock claim'
		$properties = @($status.PSObject.Properties.Name)
		if (($properties -ccontains 'held') -and $status.held -is [bool] -and $status.held -and
			($properties -ccontains 'leaseState') -and $status.leaseState -ceq 'live' -and
			(Test-FinalizeLandingLockClaimIdentity $status $Owner $Session $worktreeIdentity)) {
			return New-FinalizeLandingLockClaimResult $true 'ok' 'Landing lock is live and owned by this transaction.' 'terminal' $false 0 $Owner $status 1
		}
		return New-FinalizeLandingLockClaimResult $false 'landing-lock.claim-invalid' 'Landing lock claim returned an invalid ownership record.' 'terminal' $false 0 $Owner $status 1
	}
	if ($claim.ExitCode -ne 2) {
		return New-FinalizeLandingLockClaimResult $false 'landing-lock.claim-failed' "Landing lock claim failed: $($claim.Stdout.Trim())$($claim.Stderr.Trim())" 'terminal' $false 0 $Owner $null 1
	}

	$state = Get-FinalizeLandingLockState $item.FullName $GitCommonDirectory $worktreeIdentity
	if ($state.Kind -ceq 'terminal') {
		return New-FinalizeLandingLockClaimResult $false 'landing-lock.status-failed' "Landing lock status is not a recognized absence or lease state: $($state.Response.Stdout.Trim())$($state.Response.Stderr.Trim())" 'terminal' $false 0 $Owner $state.Status 1
	}
	if ($state.Kind -ceq 'authority-required') {
		return New-FinalizeLandingLockClaimResult $false 'landing-lock.unverifiable' 'Landing lock metadata cannot be verified; external repair authority is required.' 'authority-required' $true 0 $Owner $state.Status 1
	}
	if (($state.Kind -ceq 'live') -and (Test-FinalizeLandingLockClaimIdentity $state.Status $Owner $Session $worktreeIdentity)) {
		return New-FinalizeLandingLockClaimResult $true 'ok' 'Landing lock was already live and owned by this transaction.' 'terminal' $false 0 $Owner $state.Status 1
	}
	return New-FinalizeLandingLockClaimResult $false 'landing-lock.retryable-wait' 'A foreign landing lease remains live; retry this claim after its reported expiry.' 'retryable-wait' $false $PollMilliseconds $Owner $state.Status 1
}

# Scratch-fixture helpers shared by the finalize-changes suites. Assert-SafeScratchRoot gates every
# recursive scratch delete: a root only passes when it is the expected GUID leaf directly under the
# expected fixture parent.
function Assert-SafeScratchRoot([string] $Parent, [string] $Root, [string] $ExpectedLeaf) {
	$parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
	$rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
	if ((Split-Path -Parent $rootPath) -cne $parentPath -or (Split-Path -Leaf $rootPath) -cne $ExpectedLeaf -or $ExpectedLeaf -cnotmatch '^[0-9a-f]{32}$') {
		throw "Fixture scratch root failed containment validation: '$rootPath'."
	}
	return $rootPath
}

function Invoke-ScratchGit([string] $Root, [string[]] $Arguments) {
	$output = @(& git -C $Root -c user.name=fixture -c user.email=fixture@example.com @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
}

Export-ModuleMember -Function Assert-SafeScratchRoot, Invoke-ScratchGit, Get-FinalizeRootPreservingFullPath, Get-FinalizeExistingWindowsIdentity, Test-FinalizeExistingIdentityEqual, Invoke-FinalizeNativeText, Invoke-FinalizeGit, Test-FinalizeGitSuccess, Get-FinalizeGitIdentity, Assert-FinalizeGitPath, Get-FinalizeWorktreeRecords, Test-FinalizeWorktreeRegistration, Test-FinalizeAllWorktreesClear, Test-FinalizeLandingSanity, Test-FinalizeLandingLockClaimIdentity, Get-FinalizeLandingLockState, Invoke-FinalizeLandingLockClaim
