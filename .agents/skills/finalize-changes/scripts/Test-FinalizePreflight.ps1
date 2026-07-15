[CmdletBinding()]
param(
	[string] $Mode,
	[string] $Checkpoint,
	[string] $CurrentWorktree,
	[string] $PrimaryWorktree,
	[string] $CurrentBranch,
	[string] $PrimaryBranch,
	[string] $Baseline,
	[string] $ManifestComparisonBase,
	[string] $ExpectedCurrentTip,
	[string] $ExpectedPrimaryTip,
	[string] $VerificationReportPath,
	[string] $VerificationReportSha256,
	[string[]] $ManifestRange,
	[string] $SessionOwner,
	[string] $WaitSeconds = '60',
	[switch] $HasPlanRowClaim,
	[switch] $HasCompletedPlanClaim,
	[switch] $QueueChangingLanding
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$result = [ordered]@{
	schemaVersion = 'broken-engine-finalize-preflight/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Preflight did not complete.'
	mode = $Mode
	checkpoint = $Checkpoint
	identities = [ordered]@{ currentWorktree = $null; primaryWorktree = $null; gitCommonDirectory = $null; currentBranch = $null; primaryBranch = $null }
	tips = [ordered]@{ baseline = $Baseline; comparisonBase = $ManifestComparisonBase; current = $null; primary = $null; expectedCurrent = $ExpectedCurrentTip; expectedPrimary = $ExpectedPrimaryTip }
	manifest = [ordered]@{ expectedSha256 = $null; expectedCount = 0; actualSha256 = $null; actualCount = 0; equal = $false }
	agentCli = [ordered]@{ outputPath = $null; outputLinkTarget = $null; path = $null; capabilityResult = 'not-checked'; requiredCapabilities = @() }
	claim = [ordered]@{ classification = if ($Mode -eq 'primary-commit') { 'not-required' } else { 'not-checked' }; owner = $SessionOwner; worktree = $null; pid = $null; processStartUtc = $null; actualProcessStartUtc = $null }
}
$authoritativeSessionWorktree = $null

function Complete-Preflight([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 8 -Compress))
	exit $ExitCode
}

function Assert-Input([bool] $Condition, [string] $Message) {
	if (-not $Condition) { Complete-Preflight 1 'error' 'input.invalid' $Message }
}

function Stop-Validation([string] $Code, [string] $Message) {
	Complete-Preflight 2 'blocked' $Code $Message
}

function Get-RootPreservingFullPath([string] $Path) {
	$full = [IO.Path]::GetFullPath($Path)
	$root = [IO.Path]::GetPathRoot($full)
	if ($full.Length -gt $root.Length) { return $full.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) }
	return $full
}

if (-not ('BrokenEngine.FinalizePathIdentity' -as [type])) {
	try { $null = Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace BrokenEngine {
public static class FinalizePathIdentity {
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
	catch { Complete-Preflight 1 'error' 'state.unreadable' $_.Exception.Message }
}

function Get-ExistingWindowsIdentity([string] $Path, [string] $Label) {
	$full = Get-RootPreservingFullPath $Path
	if (-not (Test-Path -LiteralPath $full)) { Stop-Validation 'identity.missing' "$Label does not exist: '$full'." }
	try { return Get-RootPreservingFullPath ([BrokenEngine.FinalizePathIdentity]::Resolve($full)) }
	catch { throw "Unable to resolve $Label identity '$full': $($_.Exception.Message)" }
}

function Get-ReparseTargetIdentity([IO.FileSystemInfo] $Item, [string] $Label) {
	if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) { return $null }
	$targets = @($Item.Target)
	if ($targets.Count -ne 1 -or [string]::IsNullOrWhiteSpace([string]$targets[0])) {
		Stop-Validation 'identity.reparse-target-invalid' "$Label has no single readable reparse target: '$($Item.FullName)'."
	}
	$target = [string]$targets[0]
	if (-not [IO.Path]::IsPathRooted($target)) { $target = Join-Path $Item.Parent.FullName $target }
	return Get-ExistingWindowsIdentity $target "$Label target"
}

function Test-ExistingIdentityEqual([string] $Candidate, [string] $Expected) {
	try {
		if (-not (Test-Path -LiteralPath (Get-RootPreservingFullPath $Candidate))) { return $false }
		return (Get-ExistingWindowsIdentity $Candidate 'Registered worktree').Equals($Expected, [StringComparison]::OrdinalIgnoreCase)
	}
	catch { return $false }
}

function Invoke-NativeText([string] $Executable, [string[]] $Arguments, [string] $WorkingDirectory) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $Executable
	$start.WorkingDirectory = $WorkingDirectory
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false, $true)
	$start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false, $true)
	foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start '$Executable'." }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$stdout = $stdoutTask.GetAwaiter().GetResult()
	$stderr = $stderrTask.GetAwaiter().GetResult()
	$exitCode = $process.ExitCode
	$process.Dispose()
	return [pscustomobject]@{ ExitCode = $exitCode; Stdout = $stdout; Stderr = $stderr }
}

function Invoke-Git([string] $Worktree, [string[]] $Arguments) {
	$response = Invoke-NativeText 'git.exe' (@('-C', $Worktree) + $Arguments) $Worktree
	if ($response.ExitCode -ne 0) { throw "git $($Arguments -join ' ') failed: $($response.Stderr.Trim())." }
	return $response.Stdout
}

function Test-GitSuccess([string] $Worktree, [string[]] $Arguments) {
	return (Invoke-NativeText 'git.exe' (@('-C', $Worktree) + $Arguments) $Worktree).ExitCode -eq 0
}

function Get-Sha256([string[]] $Lines) {
	$text = if ($Lines.Count -eq 0) { '' } else { ($Lines -join "`n") + "`n" }
	return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.UTF8Encoding]::new($false).GetBytes($text))).ToLowerInvariant()
}

function Assert-GitPath([string] $Path) {
	if ([string]::IsNullOrEmpty($Path) -or $Path.IndexOf("`0", [StringComparison]::Ordinal) -ge 0 -or
		$Path.IndexOf("`r", [StringComparison]::Ordinal) -ge 0 -or $Path.IndexOf("`n", [StringComparison]::Ordinal) -ge 0 -or
		$Path.Contains('\', [StringComparison]::Ordinal) -or $Path.StartsWith('/', [StringComparison]::Ordinal) -or
		$Path -match '^[A-Za-z]:' -or [IO.Path]::IsPathRooted($Path)) {
		Stop-Validation 'manifest.path-invalid' "Manifest path is not a canonical repository-relative Git path: '$Path'."
	}
	foreach ($component in $Path.Split('/')) {
		if ([string]::IsNullOrEmpty($component) -or $component -ceq '.' -or $component -ceq '..') {
			Stop-Validation 'manifest.path-invalid' "Manifest path contains an empty or traversing component: '$Path'."
		}
	}
}

function Get-ExpectedManifest([string[]] $Sections) {
	$rows = [Collections.Generic.List[string]]::new()
	foreach ($section in $Sections) {
		$lines = @([regex]::Split($section, "`r`n|`n|`r"))
		for ($index = 0; $index -lt $lines.Count; ++$index) {
			if ($index -eq ($lines.Count - 1) -and $lines[$index].Length -eq 0) { continue }
			if ($lines[$index].Length -eq 0) { Stop-Validation 'manifest.row-malformed' 'Manifest ranges contain an empty interior row.' }
			$rows.Add($lines[$index])
		}
	}
	$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	$previous = $null
	foreach ($row in $rows) {
		$tab = $row.LastIndexOf([char]9)
		if ($tab -le 0 -or $tab -eq ($row.Length - 1)) { Stop-Validation 'manifest.row-malformed' "Manifest row has no final real tab delimiter: '$row'." }
		$path = $row.Substring(0, $tab)
		$status = $row.Substring($tab + 1)
		Assert-GitPath $path
		if ($status -cne 'DELETED' -and $status -cnotmatch '^blob:[0-9a-f]{40}$') {
			Stop-Validation 'manifest.status-invalid' "Manifest row has an invalid status: '$row'."
		}
		if (-not $seen.Add($path)) { Stop-Validation 'manifest.path-duplicate' "Manifest contains duplicate path '$path'." }
		if ($null -ne $previous -and [StringComparer]::Ordinal.Compare($previous, $path) -ge 0) {
			Stop-Validation 'manifest.order-invalid' "Manifest paths are not strictly ordinal-sorted at '$path'."
		}
		$previous = $path
	}
	return $rows.ToArray()
}

function Get-ActualManifest([string] $Worktree, [string] $ComparisonBase) {
	$paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($arguments in @(
		@('diff', '--name-only', '--no-renames', '-z', $ComparisonBase, '--'),
		@('ls-files', '--others', '--exclude-standard', '-z')
	)) {
		$output = Invoke-Git $Worktree $arguments
		foreach ($rawPath in $output.Split([char]0, [StringSplitOptions]::RemoveEmptyEntries)) {
			$path = $rawPath.Replace('\', '/')
			Assert-GitPath $path
			[void]$paths.Add($path)
		}
	}
	$ordered = [Collections.Generic.List[string]]::new()
	foreach ($path in $paths) { $ordered.Add($path) }
	$ordered.Sort([StringComparer]::Ordinal)
	$rows = [Collections.Generic.List[string]]::new()
	foreach ($path in $ordered) {
		$absolute = Join-Path $Worktree ($path.Replace('/', [IO.Path]::DirectorySeparatorChar))
		if (Test-Path -LiteralPath $absolute -PathType Leaf) {
			$oid = (Invoke-Git $Worktree @('hash-object', "--path=$path", '--', $path)).Trim()
			if ($oid -cnotmatch '^[0-9a-f]{40}$') { throw "git hash-object returned an invalid object id for '$path': '$oid'." }
			$rows.Add($path + [char]9 + 'blob:' + $oid)
		}
		elseif (Test-Path -LiteralPath $absolute) { throw "Changed Git path is not a file or deletion: '$path'." }
		else { $rows.Add($path + [char]9 + 'DELETED') }
	}
	return $rows.ToArray()
}

function Get-WorktreeRecords([string] $Worktree) {
	$records = [Collections.Generic.List[object]]::new()
	$current = $null
	$output = Invoke-Git $Worktree @('worktree', 'list', '--porcelain', '-z')
	foreach ($field in $output.Split([char]0, [StringSplitOptions]::RemoveEmptyEntries)) {
		if ($field.StartsWith('worktree ', [StringComparison]::Ordinal)) {
			if ($null -ne $current) { $records.Add([pscustomobject]$current) }
			$current = [ordered]@{ Path = $field.Substring(9); Head = $null; Branch = $null }
		}
		elseif ($null -ne $current -and $field.StartsWith('HEAD ', [StringComparison]::Ordinal)) { $current.Head = $field.Substring(5) }
		elseif ($null -ne $current -and $field.StartsWith('branch refs/heads/', [StringComparison]::Ordinal)) { $current.Branch = $field.Substring(18) }
	}
	if ($null -ne $current) { $records.Add([pscustomobject]$current) }
	return $records.ToArray()
}

try {
	Assert-Input (@('session-landing', 'primary-commit') -ccontains $Mode) "Mode is invalid: '$Mode'."
	Assert-Input (@('initial', 'after-lock', 'after-reconciliation', 'after-approval', 'pre-approval-lock-release', 'pre-mutation', 'post-mutation', 'pre-claim-release') -ccontains $Checkpoint) "Checkpoint is invalid: '$Checkpoint'."
	foreach ($value in @($CurrentWorktree, $PrimaryWorktree, $CurrentBranch, $PrimaryBranch, $Baseline, $ManifestComparisonBase, $VerificationReportPath, $VerificationReportSha256)) {
		Assert-Input (-not [string]::IsNullOrWhiteSpace($value)) 'Required string inputs must not be empty.'
	}
	Assert-Input ($Baseline -cmatch '^[0-9a-f]{40}$') 'Baseline must be exactly 40 lowercase hexadecimal characters.'
	Assert-Input ($VerificationReportSha256 -cmatch '^[0-9a-f]{64}$') 'VerificationReportSha256 must be exactly 64 lowercase hexadecimal characters.'
	Assert-Input (@($ManifestRange).Count -gt 0) 'At least one exact manifest range is required.'
	foreach ($range in @($ManifestRange)) { Assert-Input ($range -cmatch '^L[1-9][0-9]*-L[1-9][0-9]*$') "Manifest range has invalid grammar: '$range'." }
	if ($Checkpoint -ne 'initial') {
		Assert-Input ($ExpectedCurrentTip -cmatch '^[0-9a-f]{40}$') 'Later checkpoints require ExpectedCurrentTip.'
		Assert-Input ($ExpectedPrimaryTip -cmatch '^[0-9a-f]{40}$') 'Later checkpoints require ExpectedPrimaryTip.'
	}
	else {
		if (-not [string]::IsNullOrWhiteSpace($ExpectedCurrentTip)) { Assert-Input ($ExpectedCurrentTip -cmatch '^[0-9a-f]{40}$') 'ExpectedCurrentTip is malformed.' }
		if (-not [string]::IsNullOrWhiteSpace($ExpectedPrimaryTip)) { Assert-Input ($ExpectedPrimaryTip -cmatch '^[0-9a-f]{40}$') 'ExpectedPrimaryTip is malformed.' }
	}
	$parsedWaitSeconds = 0
	Assert-Input ([int]::TryParse($WaitSeconds, [Globalization.NumberStyles]::None, [Globalization.CultureInfo]::InvariantCulture, [ref]$parsedWaitSeconds)) 'WaitSeconds must be an integer between 1 and 660.'
	Assert-Input ($parsedWaitSeconds -ge 1 -and $parsedWaitSeconds -le 660) 'WaitSeconds must be between 1 and 660.'
	Assert-Input (-not $HasCompletedPlanClaim -or $HasPlanRowClaim) 'HasCompletedPlanClaim requires HasPlanRowClaim.'
	Assert-Input (-not $QueueChangingLanding -or $Mode -ceq 'session-landing') 'QueueChangingLanding is valid only for session landing.'

	if ($Mode -eq 'session-landing') {
		Assert-Input ($SessionOwner -cmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') 'Session landing requires a canonical lowercase SessionOwner GUID.'
		$provenance = [ordered]@{
			BROKEN_ENGINE_WORKTREE_PATH = $CurrentWorktree
			BROKEN_ENGINE_SESSION_BRANCH = $CurrentBranch
			BROKEN_ENGINE_PRIMARY_CHECKOUT = $PrimaryWorktree
			BROKEN_ENGINE_TARGET_BRANCH = $PrimaryBranch
			BROKEN_ENGINE_BASELINE = $Baseline
			BROKEN_ENGINE_AGENTCLI_SESSION_OWNER = $SessionOwner
		}
		foreach ($entry in $provenance.GetEnumerator()) {
			$actual = [Environment]::GetEnvironmentVariable($entry.Key)
			Assert-Input (-not [string]::IsNullOrWhiteSpace($actual)) "Session landing requires $($entry.Key)."
			if ($entry.Key -in @('BROKEN_ENGINE_WORKTREE_PATH', 'BROKEN_ENGINE_PRIMARY_CHECKOUT')) {
				Assert-Input ((Get-ExistingWindowsIdentity $actual $entry.Key).Equals((Get-ExistingWindowsIdentity $entry.Value 'Supplied provenance path'), [StringComparison]::OrdinalIgnoreCase)) "$($entry.Key) does not match the supplied path."
				if ($entry.Key -ceq 'BROKEN_ENGINE_WORKTREE_PATH') { $authoritativeSessionWorktree = $actual }
			}
			else { Assert-Input ($actual -ceq $entry.Value) "$($entry.Key) does not match the supplied value." }
		}
	}

	$repositoryRoot = Get-RootPreservingFullPath (Join-Path $PSScriptRoot '..\..\..\..')
	$reader = Join-Path $repositoryRoot '.agents\scripts\Read-AgentReportSection.ps1'
	if (-not (Test-Path -LiteralPath $reader -PathType Leaf)) { throw "Report reader is unavailable: '$reader'." }
	$sections = [Collections.Generic.List[string]]::new()
	try {
		Push-Location $CurrentWorktree
		try { foreach ($range in @($ManifestRange)) { $sections.Add([string](& $reader -ReportPath $VerificationReportPath -ExpectedSha256 $VerificationReportSha256 -Range $range)) } }
		finally { Pop-Location }
	}
	catch { Stop-Validation 'report.invalid' $_.Exception.Message }
	$expectedManifest = @(Get-ExpectedManifest $sections.ToArray())
	$result.manifest.expectedCount = $expectedManifest.Count
	$result.manifest.expectedSha256 = Get-Sha256 $expectedManifest

	$currentIdentity = Get-ExistingWindowsIdentity $CurrentWorktree 'Current worktree'
	$primaryIdentity = Get-ExistingWindowsIdentity $PrimaryWorktree 'Primary worktree'
	$currentTop = Get-ExistingWindowsIdentity ((Invoke-Git $currentIdentity @('rev-parse', '--show-toplevel')).Trim()) 'Current Git top-level'
	$primaryTop = Get-ExistingWindowsIdentity ((Invoke-Git $primaryIdentity @('rev-parse', '--show-toplevel')).Trim()) 'Primary Git top-level'
	if (-not $currentIdentity.Equals($currentTop, [StringComparison]::OrdinalIgnoreCase)) { Stop-Validation 'identity.current-not-root' 'CurrentWorktree is not the current Git top-level.' }
	if (-not $primaryIdentity.Equals($primaryTop, [StringComparison]::OrdinalIgnoreCase)) { Stop-Validation 'identity.primary-not-root' 'PrimaryWorktree is not the primary Git top-level.' }
	$currentCommon = Get-ExistingWindowsIdentity ((Invoke-Git $currentIdentity @('rev-parse', '--path-format=absolute', '--git-common-dir')).Trim()) 'Current Git common directory'
	$primaryCommon = Get-ExistingWindowsIdentity ((Invoke-Git $primaryIdentity @('rev-parse', '--path-format=absolute', '--git-common-dir')).Trim()) 'Primary Git common directory'
	if (-not $currentCommon.Equals($primaryCommon, [StringComparison]::OrdinalIgnoreCase)) { Stop-Validation 'identity.repository-mismatch' 'Current and primary worktrees do not share one Git common directory.' }
	if ($Mode -eq 'session-landing' -and $currentIdentity.Equals($primaryIdentity, [StringComparison]::OrdinalIgnoreCase)) { Stop-Validation 'identity.session-is-primary' 'Session landing requires distinct current and primary worktrees.' }
	if ($Mode -eq 'primary-commit' -and -not $currentIdentity.Equals($primaryIdentity, [StringComparison]::OrdinalIgnoreCase)) { Stop-Validation 'identity.primary-mode-mismatch' 'Primary commit requires CurrentWorktree and PrimaryWorktree to identify the same checkout.' }
	$primaryDotGit = Get-Item -LiteralPath (Join-Path $primaryIdentity '.git') -Force -ErrorAction Stop
	if (-not $primaryDotGit.PSIsContainer -or ($primaryDotGit.Attributes -band [IO.FileAttributes]::ReparsePoint)) { Stop-Validation 'identity.not-primary-checkout' 'PrimaryWorktree does not have an ordinary primary .git directory.' }

	$records = Get-WorktreeRecords $currentIdentity
	$currentRecords = @($records | Where-Object { Test-ExistingIdentityEqual $_.Path $currentIdentity })
	$primaryRecords = @($records | Where-Object { Test-ExistingIdentityEqual $_.Path $primaryIdentity })
	if ($currentRecords.Count -ne 1 -or $primaryRecords.Count -ne 1) { Stop-Validation 'identity.worktree-registration' 'Current or primary checkout is not uniquely registered by Git.' }
	$actualCurrentBranch = (Invoke-Git $currentIdentity @('branch', '--show-current')).Trim()
	$actualPrimaryBranch = (Invoke-Git $primaryIdentity @('branch', '--show-current')).Trim()
	$currentTip = (Invoke-Git $currentIdentity @('rev-parse', 'HEAD')).Trim()
	$primaryTip = (Invoke-Git $primaryIdentity @('rev-parse', 'HEAD')).Trim()
	$result.identities.currentWorktree = $currentIdentity
	$result.identities.primaryWorktree = $primaryIdentity
	$result.identities.gitCommonDirectory = $currentCommon
	$result.identities.currentBranch = $actualCurrentBranch
	$result.identities.primaryBranch = $actualPrimaryBranch
	$result.tips.current = $currentTip
	$result.tips.primary = $primaryTip
	if ($actualCurrentBranch -cne $CurrentBranch -or $actualPrimaryBranch -cne $PrimaryBranch) { Stop-Validation 'git.branch-mismatch' 'A current or primary branch differs from its supplied identity.' }
	if ($currentRecords[0].Head -cne $currentTip -or $primaryRecords[0].Head -cne $primaryTip -or $currentRecords[0].Branch -cne $actualCurrentBranch -or $primaryRecords[0].Branch -cne $actualPrimaryBranch) { Stop-Validation 'git.worktree-record-mismatch' 'Git worktree registration does not match current branch/tip state.' }
	if (-not [string]::IsNullOrWhiteSpace($ExpectedCurrentTip) -and $currentTip -cne $ExpectedCurrentTip) { Stop-Validation 'git.current-tip-changed' 'Current tip differs from the recorded expectation.' }
	if (-not [string]::IsNullOrWhiteSpace($ExpectedPrimaryTip) -and $primaryTip -cne $ExpectedPrimaryTip) { Stop-Validation 'git.primary-tip-changed' 'Primary tip differs from the recorded expectation.' }
	if (-not (Test-GitSuccess $currentIdentity @('rev-parse', '--verify', "$Baseline^{commit}"))) { Stop-Validation 'git.baseline-invalid' 'Baseline is not a commit in this repository.' }
	if (-not (Test-GitSuccess $currentIdentity @('rev-parse', '--verify', "$ManifestComparisonBase^{commit}"))) { Stop-Validation 'git.comparison-base-invalid' 'ManifestComparisonBase is not a commit in this repository.' }
	if (-not (Test-GitSuccess $currentIdentity @('merge-base', '--is-ancestor', $Baseline, $currentTip)) -or -not (Test-GitSuccess $primaryIdentity @('merge-base', '--is-ancestor', $Baseline, $primaryTip))) { Stop-Validation 'git.baseline-not-ancestor' 'Baseline is not an ancestor of both current and primary tips.' }
	if ($Mode -eq 'session-landing' -and $Checkpoint -in @('after-reconciliation', 'after-approval', 'pre-mutation') -and -not (Test-GitSuccess $currentIdentity @('merge-base', '--is-ancestor', $primaryTip, $currentTip))) { Stop-Validation 'git.session-not-rebased' 'Primary tip is not an ancestor of the reconciled session tip.' }
	if ($Mode -eq 'session-landing' -and $Checkpoint -in @('post-mutation', 'pre-claim-release') -and $currentTip -cne $primaryTip) { Stop-Validation 'git.post-landing-tip-mismatch' 'Primary and session tips differ after landing.' }
	foreach ($worktree in @($currentIdentity, $primaryIdentity) | Select-Object -Unique) {
		foreach ($marker in @('MERGE_HEAD', 'rebase-merge', 'rebase-apply', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'BISECT_LOG', 'sequencer')) {
			$markerPath = (Invoke-Git $worktree @('rev-parse', '--path-format=absolute', '--git-path', $marker)).Trim()
			if (Test-Path -LiteralPath $markerPath) { Stop-Validation 'git.operation-active' "Git operation marker '$marker' is active in '$worktree'." }
		}
	}
	if ($Mode -eq 'session-landing' -and (Invoke-Git $primaryIdentity @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) { Stop-Validation 'git.primary-dirty' 'Primary worktree is not clean for session landing.' }

	$actualManifest = @(Get-ActualManifest $currentIdentity $ManifestComparisonBase)
	$result.manifest.actualCount = $actualManifest.Count
	$result.manifest.actualSha256 = Get-Sha256 $actualManifest
	$result.manifest.equal = $expectedManifest.Count -eq $actualManifest.Count
	if ($result.manifest.equal) {
		for ($index = 0; $index -lt $expectedManifest.Count; ++$index) { if ($expectedManifest[$index] -cne $actualManifest[$index]) { $result.manifest.equal = $false; break } }
	}
	if (-not $result.manifest.equal) { Stop-Validation 'manifest.mismatch' 'Current canonical manifest differs from the hash-bound verification manifest.' }

	$relativeOutput = 'Tools\AgentCli\Platforms\VisualStudio2026\Output'
	$primaryOutput = Get-Item -LiteralPath (Join-Path $primaryIdentity $relativeOutput) -Force -ErrorAction Stop
	if (-not $primaryOutput.PSIsContainer -or ($primaryOutput.Attributes -band [IO.FileAttributes]::ReparsePoint)) { Stop-Validation 'agentcli.primary-output-invalid' 'Primary AgentCli Output must be an ordinary directory.' }
	$selectedOutput = if ($Mode -eq 'session-landing') { Get-Item -LiteralPath (Join-Path $currentIdentity $relativeOutput) -Force -ErrorAction Stop } else { $primaryOutput }
	if (-not $selectedOutput.PSIsContainer) { Stop-Validation 'agentcli.output-invalid' 'Selected AgentCli Output is not a directory.' }
	if ($Mode -eq 'session-landing') {
		if (($selectedOutput.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) { Stop-Validation 'agentcli.output-not-link' 'Session AgentCli Output must be a directory link.' }
		$linkTarget = Get-ReparseTargetIdentity $selectedOutput 'Session AgentCli Output'
		$result.agentCli.outputLinkTarget = $linkTarget
		if (-not $linkTarget.Equals((Get-RootPreservingFullPath $primaryOutput.FullName), [StringComparison]::OrdinalIgnoreCase)) { Stop-Validation 'agentcli.output-wrong-target' 'Session AgentCli Output does not target primary AgentCli Output.' }
	}
	elseif (($selectedOutput.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { Stop-Validation 'agentcli.primary-output-link' 'Primary commit mode requires an ordinary primary AgentCli Output directory.' }
	$result.agentCli.outputPath = Get-RootPreservingFullPath $selectedOutput.FullName
	$agentCliPath = Join-Path $selectedOutput.FullName 'AgentCli.exe'
	if (-not (Test-Path -LiteralPath $agentCliPath -PathType Leaf)) { Stop-Validation 'agentcli.executable-missing' 'Selected AgentCli executable is missing; run authorized /compile primary maintenance.' }
	$agentCliItem = Get-Item -LiteralPath $agentCliPath -Force
	if (($agentCliItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $agentCliItem.Length -eq 0) { Stop-Validation 'agentcli.executable-invalid' 'Selected AgentCli executable must be an ordinary nonempty file.' }
	$result.agentCli.path = Get-RootPreservingFullPath $agentCliItem.FullName
	$requiredHelp = [Collections.Generic.List[string]]::new()
	$requiredCapabilities = [Collections.Generic.List[string]]::new()
	$requiredHelp.Add('AgentCli.exe lock <token|claim|status|refresh|recover|release|steal> ...')
	$requiredCapabilities.Add('lock:token,claim,status,refresh,recover,release,steal')
	if ($HasPlanRowClaim) {
		$requiredHelp.Add('AgentCli.exe plan row status --repo COMMON-DIR --order PATH --plan PATH [--owner TOKEN]')
		$requiredHelp.Add('AgentCli.exe plan row unclaim --repo COMMON-DIR --order PATH --plan PATH --owner TOKEN')
		$requiredCapabilities.Add('plan:row:status,unclaim')
	}
	if ($HasCompletedPlanClaim) {
		$requiredHelp.Add('AgentCli.exe plan order complete --repo COMMON-DIR --worktree CHECKOUT --owner TOKEN --session TOKEN --plan PATH [--reapply]')
		$requiredCapabilities.Add('plan:order:complete:reapply')
	}
	if ($QueueChangingLanding) {
		$requiredHelp.Add('AgentCli.exe plan queue lock --repo COMMON-DIR --order PATH --owner TOKEN --session TOKEN')
		$requiredHelp.Add('AgentCli.exe plan queue status --repo COMMON-DIR --order PATH [--owner TOKEN]')
		$requiredHelp.Add('AgentCli.exe plan queue unlock --repo COMMON-DIR --order PATH --owner TOKEN')
		$requiredCapabilities.Add('plan:queue:lock,status,unlock')
	}
	$result.agentCli.requiredCapabilities = $requiredCapabilities.ToArray()
	$help = Invoke-NativeText $agentCliItem.FullName @('--help') $currentIdentity
	if ($help.ExitCode -ne 0) { Stop-Validation 'agentcli.help-failed' 'Selected AgentCli --help failed; run authorized /compile primary maintenance.' }
	$result.agentCli.capabilityResult = 'fail'
	foreach ($required in $requiredHelp) { if (-not $help.Stdout.Contains($required, [StringComparison]::Ordinal)) { Stop-Validation 'agentcli.capability-stale' "Selected AgentCli lacks finalization capability '$required'; run authorized /compile primary maintenance." } }
	$result.agentCli.capabilityResult = 'pass'

	if ($Mode -eq 'session-landing') {
		$module = Join-Path $currentIdentity '.agents\scripts\AgentCliSessionExclusion.psm1'
		Import-Module $module -Force
		$classification = Get-AgentCliSessionClassification -RepositoryRoot $primaryIdentity -Owner $SessionOwner -Worktree $authoritativeSessionWorktree -WaitSeconds $parsedWaitSeconds
		$result.claim.classification = $classification.Classification
		$result.claim.worktree = $classification.ClaimWorktree
		$result.claim.pid = $classification.ClaimPid
		$result.claim.processStartUtc = $classification.ClaimProcessStartUtc
		$result.claim.actualProcessStartUtc = $classification.ActualProcessStartUtc
		if ($classification.Classification -cne 'expected-live') { Stop-Validation "claim.$($classification.Classification)" "Wrapper AgentCli session claim classified as '$($classification.Classification)'." }
	}

	Complete-Preflight 0 'pass' 'ok' 'Finalization preflight passed.'
}
catch {
	Complete-Preflight 1 'error' 'state.unreadable' $_.Exception.Message
}
