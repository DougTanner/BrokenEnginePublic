# Canonical read-only structural preflight for finalization: identity, Git state,
# WorktreeCli capability, and wrapper-claim checks. Invoked once per landing at the
# pre-mutation checkpoint inside the landing transaction (other checkpoint labels
# remain accepted for the primary-commit route).
#
# Capability profile: pass -HasPlanRowClaim only when finalization owns a row claim.
# The machine-local plan queue never appears in the session diff, so landing takes no
# queue locks and this preflight probes no queue-lock capability. Session mode requires
# BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER and the wrapper's authoritative provenance
# variables to match; primary mode requires neither wrapper provenance nor a session
# claim.
#
# Emits one broken-engine-finalize-preflight/v1 JSON object: exit 0 with status pass
# is the only success; exit 2 is a reported deterministic blocker; exit 1 is
# malformed input or unreadable/internal state. Callers never reconstruct a failed
# check ad hoc and never search another WorktreeCli path — a missing, empty,
# wrong-target, or capability-stale executable requires explicitly authorized
# /compile primary maintenance.
[CmdletBinding()]
param(
	[string] $Mode,
	[string] $Checkpoint,
	[string] $CurrentWorktree,
	[string] $PrimaryWorktree,
	[string] $CurrentBranch,
	[string] $PrimaryBranch,
	[string] $Baseline,
	[string] $ExpectedCurrentTip,
	[string] $ExpectedPrimaryTip,
	[string] $SessionOwner,
	[string] $WaitSeconds = '60',
	[switch] $HasPlanRowClaim
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workflowModule = Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1'
if (-not (Test-Path -LiteralPath $workflowModule)) {
	$workflowModule = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\FinalizeWorkflowCommon.psm1'
}
Import-Module $workflowModule -Force

$result = [ordered]@{
	schemaVersion = 'broken-engine-finalize-preflight/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Preflight did not complete.'
	mode = $Mode
	checkpoint = $Checkpoint
	identities = [ordered]@{ currentWorktree = $null; primaryWorktree = $null; gitCommonDirectory = $null; currentBranch = $null; primaryBranch = $null }
	tips = [ordered]@{ baseline = $Baseline; current = $null; primary = $null; expectedCurrent = $ExpectedCurrentTip; expectedPrimary = $ExpectedPrimaryTip }
	worktreeCli = [ordered]@{ outputPath = $null; outputLinkTarget = $null; path = $null; capabilityResult = 'not-checked'; requiredCapabilities = @() }
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

function Get-WorktreeRecords([string] $Worktree) {
	return @(Get-FinalizeWorktreeRecords $Worktree)
}

try {
	Assert-Input (@('session-landing', 'primary-commit') -ccontains $Mode) "Mode is invalid: '$Mode'."
	Assert-Input (@('initial', 'after-reconciliation', 'pre-mutation', 'post-mutation') -ccontains $Checkpoint) "Checkpoint is invalid: '$Checkpoint'."
	foreach ($value in @($CurrentWorktree, $PrimaryWorktree, $CurrentBranch, $PrimaryBranch, $Baseline)) {
		Assert-Input (-not [string]::IsNullOrWhiteSpace($value)) 'Required string inputs must not be empty.'
	}
	Assert-Input ($Baseline -cmatch '^[0-9a-f]{40}$') 'Baseline must be exactly 40 lowercase hexadecimal characters.'
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

	if ($Mode -eq 'session-landing') {
		Assert-Input ($SessionOwner -cmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') 'Session landing requires a canonical lowercase SessionOwner GUID.'
		$provenance = [ordered]@{
			BROKEN_ENGINE_WORKTREE_PATH = $CurrentWorktree
			BROKEN_ENGINE_SESSION_BRANCH = $CurrentBranch
			BROKEN_ENGINE_PRIMARY_CHECKOUT = $PrimaryWorktree
			BROKEN_ENGINE_TARGET_BRANCH = $PrimaryBranch
			BROKEN_ENGINE_BASELINE = $Baseline
			BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER = $SessionOwner
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
	if (-not (Test-GitSuccess $currentIdentity @('merge-base', '--is-ancestor', $Baseline, $currentTip)) -or -not (Test-GitSuccess $primaryIdentity @('merge-base', '--is-ancestor', $Baseline, $primaryTip))) { Stop-Validation 'git.baseline-not-ancestor' 'Baseline is not an ancestor of both current and primary tips.' }
	if ($Mode -eq 'session-landing' -and $Checkpoint -in @('after-reconciliation', 'pre-mutation') -and -not (Test-GitSuccess $currentIdentity @('merge-base', '--is-ancestor', $primaryTip, $currentTip))) { Stop-Validation 'git.session-not-rebased' 'Primary tip is not an ancestor of the reconciled session tip.' }
	if ($Mode -eq 'session-landing' -and $Checkpoint -eq 'post-mutation' -and $currentTip -cne $primaryTip) { Stop-Validation 'git.post-landing-tip-mismatch' 'Primary and session tips differ after landing.' }
	foreach ($worktree in @($currentIdentity, $primaryIdentity) | Select-Object -Unique) {
		foreach ($marker in @('MERGE_HEAD', 'rebase-merge', 'rebase-apply', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'BISECT_LOG', 'sequencer')) {
			$markerPath = (Invoke-Git $worktree @('rev-parse', '--path-format=absolute', '--git-path', $marker)).Trim()
			if (Test-Path -LiteralPath $markerPath) { Stop-Validation 'git.operation-active' "Git operation marker '$marker' is active in '$worktree'." }
		}
	}
	if ($Mode -eq 'session-landing' -and (Invoke-Git $primaryIdentity @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) { Stop-Validation 'git.primary-dirty' 'Primary worktree is not clean for session landing.' }

	$relativeOutput = 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	$primaryOutput = Get-Item -LiteralPath (Join-Path $primaryIdentity $relativeOutput) -Force -ErrorAction Stop
	if (-not $primaryOutput.PSIsContainer -or ($primaryOutput.Attributes -band [IO.FileAttributes]::ReparsePoint)) { Stop-Validation 'worktreecli.primary-output-invalid' 'Primary WorktreeCli Output must be an ordinary directory.' }
	$selectedOutput = if ($Mode -eq 'session-landing') { Get-Item -LiteralPath (Join-Path $currentIdentity $relativeOutput) -Force -ErrorAction Stop } else { $primaryOutput }
	if (-not $selectedOutput.PSIsContainer) { Stop-Validation 'worktreecli.output-invalid' 'Selected WorktreeCli Output is not a directory.' }
	if ($Mode -eq 'session-landing') {
		if (($selectedOutput.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) { Stop-Validation 'worktreecli.output-not-link' 'Session WorktreeCli Output must be a directory link.' }
		$linkTarget = Get-ReparseTargetIdentity $selectedOutput 'Session WorktreeCli Output'
		$result.worktreeCli.outputLinkTarget = $linkTarget
		if (-not $linkTarget.Equals((Get-RootPreservingFullPath $primaryOutput.FullName), [StringComparison]::OrdinalIgnoreCase)) { Stop-Validation 'worktreecli.output-wrong-target' 'Session WorktreeCli Output does not target primary WorktreeCli Output.' }
	}
	elseif (($selectedOutput.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { Stop-Validation 'worktreecli.primary-output-link' 'Primary commit mode requires an ordinary primary WorktreeCli Output directory.' }
	$result.worktreeCli.outputPath = Get-RootPreservingFullPath $selectedOutput.FullName
	$worktreeCliPath = Join-Path $selectedOutput.FullName 'WorktreeCli.exe'
	if (-not (Test-Path -LiteralPath $worktreeCliPath -PathType Leaf)) { Stop-Validation 'worktreecli.executable-missing' 'Selected WorktreeCli executable is missing; run authorized /compile primary maintenance.' }
	$worktreeCliItem = Get-Item -LiteralPath $worktreeCliPath -Force
	if (($worktreeCliItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $worktreeCliItem.Length -eq 0) { Stop-Validation 'worktreecli.executable-invalid' 'Selected WorktreeCli executable must be an ordinary nonempty file.' }
	$result.worktreeCli.path = Get-RootPreservingFullPath $worktreeCliItem.FullName
	$requiredHelp = [Collections.Generic.List[string]]::new()
	$requiredCapabilities = [Collections.Generic.List[string]]::new()
	$requiredHelp.Add('WorktreeCli.exe lock <token|claim|status|refresh|recover|release|steal> ...')
	$requiredCapabilities.Add('lock:token,claim,status,refresh,recover,release,steal')
	if ($HasPlanRowClaim) {
		$requiredHelp.Add('WorktreeCli.exe plan row status --repo COMMON-DIR --order PATH --plan PATH [--owner TOKEN]')
		$requiredHelp.Add('WorktreeCli.exe plan row unclaim --repo COMMON-DIR --order PATH --plan PATH --owner TOKEN')
		$requiredCapabilities.Add('plan:row:status,unclaim')
	}
	$result.worktreeCli.requiredCapabilities = $requiredCapabilities.ToArray()
	$help = Invoke-NativeText $worktreeCliItem.FullName @('--help') $currentIdentity
	if ($help.ExitCode -ne 0) { Stop-Validation 'worktreecli.help-failed' 'Selected WorktreeCli --help failed; run authorized /compile primary maintenance.' }
	$result.worktreeCli.capabilityResult = 'fail'
	foreach ($required in $requiredHelp) { if (-not $help.Stdout.Contains($required, [StringComparison]::Ordinal)) { Stop-Validation 'worktreecli.capability-stale' "Selected WorktreeCli lacks finalization capability '$required'; run authorized /compile primary maintenance." } }
	$result.worktreeCli.capabilityResult = 'pass'

	if ($Mode -eq 'session-landing') {
		$module = Join-Path $currentIdentity '.agents\scripts\WorktreeCliSessionExclusion.psm1'
		Import-Module $module -Force
		$classification = Get-WorktreeCliSessionClassification -RepositoryRoot $primaryIdentity -Owner $SessionOwner -Worktree $authoritativeSessionWorktree -WaitSeconds $parsedWaitSeconds
		$result.claim.classification = $classification.Classification
		$result.claim.worktree = $classification.ClaimWorktree
		$result.claim.pid = $classification.ClaimPid
		$result.claim.processStartUtc = $classification.ClaimProcessStartUtc
		$result.claim.actualProcessStartUtc = $classification.ActualProcessStartUtc
		if ($classification.Classification -cne 'expected-live') { Stop-Validation "claim.$($classification.Classification)" "Wrapper WorktreeCli session claim classified as '$($classification.Classification)'." }
	}

	Complete-Preflight 0 'pass' 'ok' 'Finalization preflight passed.'
}
catch {
	Complete-Preflight 1 'error' 'state.unreadable' $_.Exception.Message
}
