

# Canonical read-only structural preflight for finalization: identity, Git state,
# WorktreeCli capability, session-landing receipt, and receipt-bound terminal Plan checks.
# Approval preparation invokes after-reconciliation checks; landing invokes the
# pre-mutation check or the post-mutation crash-recovery check.
#
# Capability profile validates optional receipt-bound terminal Plan release.
# This preflight probes no Plan scheduler mutation capability. Session landing requires
# a canonical SessionOwner GUID and the in-worktree session receipt to match the supplied
# identity, branch, target branch, primary checkout, and baseline; primary mode requires
# neither a receipt nor a session claim.
#
# Emits one broken-engine-finalize-preflight/v1 JSON object: exit 0 with status pass
# is the only success; exit 2 is a reported deterministic blocker; exit 1 is
# malformed input or unreadable/internal state. Callers never reconstruct a failed
# check ad hoc and never search another WorktreeCli path. The sole bootstrap
# exception is an approval-bound v2 candidate receipt/hash when the canonical
# executable lacks required current scheduler capability during cutover.
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
	[string] $CandidateReceiptPath,
	[string] $CandidateReceiptSha256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$candidateReceiptPathBound = $PSBoundParameters.ContainsKey('CandidateReceiptPath')
$candidateReceiptSha256Bound = $PSBoundParameters.ContainsKey('CandidateReceiptSha256')
$workflowModule = Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1'
if (-not (Test-Path -LiteralPath $workflowModule)) {
	$workflowModule = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\FinalizeWorkflowCommon.psm1'
}
Import-Module $workflowModule -Force
$receiptModule = Join-Path $PSScriptRoot '..\..\..\scripts\PlanClaimReceipt.psm1'
if (-not (Test-Path -LiteralPath $receiptModule)) { $receiptModule = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\PlanClaimReceipt.psm1' }
Import-Module $receiptModule -Force -DisableNameChecking

$result = [ordered]@{
	schemaVersion = 'broken-engine-finalize-preflight/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Preflight did not complete.'
	mode = $Mode
	checkpoint = $Checkpoint
	identities = [ordered]@{ currentWorktree = $null; primaryWorktree = $null; gitCommonDirectory = $null; currentBranch = $null; primaryBranch = $null }
	tips = [ordered]@{ baseline = $Baseline; current = $null; primary = $null; expectedCurrent = $ExpectedCurrentTip; expectedPrimary = $ExpectedPrimaryTip }
	worktreeCli = [ordered]@{ outputPath = $null; outputLinkTarget = $null; path = $null; selection = 'canonical'; capabilityResult = 'not-checked'; requiredCapabilities = @(); candidate = $null }
	claim = [ordered]@{ classification = if ($Mode -eq 'primary-commit') { 'not-required' } else { 'not-checked' }; owner = $SessionOwner; worktree = $null }
	planClaim = [ordered]@{ present = $false; state = 'absent'; disposition = 'none'; validation = $null }
}

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

function Get-CertifiedCandidateWorktreeCli([string] $RepositoryRoot, [string] $WorktreeRoot, [string] $ExpectedCommit) {
	$certificationScript = Join-Path $PSScriptRoot 'Test-AgentToolsCandidateCertification.ps1'
	if (-not (Test-Path -LiteralPath $certificationScript -PathType Leaf)) { throw "AgentTools candidate certification script is missing: '$certificationScript'." }
	$response = Invoke-NativeText 'pwsh.exe' @('-NoProfile', '-File', $certificationScript, '-RepositoryRoot', $RepositoryRoot, '-WorktreeRoot', $WorktreeRoot,
		'-CandidateReceiptPath', $CandidateReceiptPath, '-CandidateReceiptSha256', $CandidateReceiptSha256, '-ExpectedCommit', $ExpectedCommit) $WorktreeRoot
	if ([string]::IsNullOrWhiteSpace($response.Stdout)) { throw 'AgentTools candidate certification returned no JSON.' }
	try { $certification = $response.Stdout.Trim() | ConvertFrom-Json -Depth 100 -ErrorAction Stop }
	catch { throw "AgentTools candidate certification returned invalid JSON: $($_.Exception.Message)" }
	if ($response.ExitCode -eq 2 -and $certification.status -ceq 'blocked') {
		Stop-Validation 'worktreecli.candidate-certification-failed' "Certified WorktreeCli candidate is unavailable: $($certification.message)"
	}
	if ($response.ExitCode -ne 0 -or $certification.status -cne 'pass' -or $certification.code -cne 'ok' -or $null -eq $certification.executables.WorktreeCli) {
		throw "AgentTools candidate certification failed: $($certification.message)"
	}
	$candidate = $certification.executables.WorktreeCli
	if ($candidate.path -isnot [string] -or $candidate.sha256 -isnot [string] -or $candidate.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
		-not (Test-Path -LiteralPath $candidate.path -PathType Leaf)) { throw 'AgentTools candidate certification returned an invalid WorktreeCli identity.' }
	return [pscustomobject]@{ Certification = $certification; Executable = (Get-ExistingWindowsIdentity $candidate.path 'Certified WorktreeCli candidate') }
}

function Get-WorktreeRecords([string] $Worktree) {
	return @(Get-FinalizeWorktreeRecords $Worktree)
}

try {
	Assert-Input (@('session-landing', 'primary-commit') -ccontains $Mode) "Mode is invalid: '$Mode'."
	Assert-Input (@('initial', 'after-reconciliation', 'pre-mutation', 'post-mutation', 'post-advance-recovery') -ccontains $Checkpoint) "Checkpoint is invalid: '$Checkpoint'."
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
	Assert-Input ($candidateReceiptPathBound -eq $candidateReceiptSha256Bound) 'Candidate receipt path and SHA-256 must be supplied together.'
	if ($candidateReceiptPathBound) {
		Assert-Input (-not [string]::IsNullOrWhiteSpace($CandidateReceiptPath)) 'CandidateReceiptPath must not be blank.'
		Assert-Input ($CandidateReceiptSha256 -cmatch '^[0-9a-f]{64}$') 'CandidateReceiptSha256 must be 64 lowercase hexadecimal characters.'
	}

	if ($Mode -eq 'session-landing') {
		Assert-Input ($SessionOwner -cmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') 'Session landing requires a canonical lowercase SessionOwner GUID.'
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
	if ($Mode -eq 'session-landing' -and $Checkpoint -eq 'post-advance-recovery' -and -not (Test-GitSuccess $primaryIdentity @('merge-base', '--is-ancestor', $currentTip, $primaryTip))) { Stop-Validation 'git.post-landing-not-contained' 'Session tip is not contained in the advanced primary tip.' }
	foreach ($worktree in @($currentIdentity, $primaryIdentity) | Select-Object -Unique) {
		foreach ($marker in @('MERGE_HEAD', 'rebase-merge', 'rebase-apply', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'BISECT_LOG', 'sequencer')) {
			$markerPath = (Invoke-Git $worktree @('rev-parse', '--path-format=absolute', '--git-path', $marker)).Trim()
			if (Test-Path -LiteralPath $markerPath) { Stop-Validation 'git.operation-active' "Git operation marker '$marker' is active in '$worktree'." }
		}
	}
	if ($Mode -eq 'session-landing' -and (Invoke-Git $primaryIdentity @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) { Stop-Validation 'git.primary-dirty' 'Primary worktree is not clean for session landing.' }
	if ($Mode -eq 'session-landing' -and (Invoke-Git $currentIdentity @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) { Stop-Validation 'git.session-dirty' 'Session worktree has remaining staged, unstaged, or untracked status.' }

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
	$requiredHelp.Add('WorktreeCli.exe plan validate --repo COMMON-DIR --worktree CHECKOUT --baseline COMMIT')
	$requiredCapabilities.Add('plan:validate')
	$requiredHelp.Add('WorktreeCli.exe plan release-after-landing --worktree SESSION --claim-receipt Temp/RECEIPT --claim-receipt-sha256 SHA256 --landed-commit COMMIT')
	$requiredCapabilities.Add('plan:release-after-landing')
	$result.worktreeCli.requiredCapabilities = $requiredCapabilities.ToArray()
	$help = Invoke-NativeText $worktreeCliItem.FullName @('--help') $currentIdentity
	if ($help.ExitCode -ne 0) { Stop-Validation 'worktreecli.help-failed' 'Selected WorktreeCli --help failed; run authorized /compile primary maintenance.' }
	$result.worktreeCli.capabilityResult = 'fail'
	$missingHelp = @($requiredHelp | Where-Object { -not $help.Stdout.Contains($_, [StringComparison]::Ordinal) })
	$bootstrapCandidate = $Checkpoint -in @('after-reconciliation', 'pre-mutation', 'post-advance-recovery') -and
		$missingHelp.Count -ne 0 -and $candidateReceiptPathBound
	if ($missingHelp.Count -ne 0 -and -not $bootstrapCandidate) {
		Stop-Validation 'worktreecli.capability-stale' "Selected WorktreeCli lacks finalization capability '$($missingHelp[0])'; run authorized /compile primary maintenance."
	}
	if ($bootstrapCandidate) {
		$candidate = Get-CertifiedCandidateWorktreeCli $primaryIdentity $currentIdentity $currentTip
		$result.worktreeCli.path = $candidate.Executable
		$result.worktreeCli.selection = 'certified-candidate'
		$result.worktreeCli.candidate = [ordered]@{ receiptPath = $CandidateReceiptPath; receiptSha256 = $CandidateReceiptSha256; executablePath = $candidate.Executable; executableSha256 = $candidate.Certification.executables.WorktreeCli.sha256 }
	}
	$result.worktreeCli.capabilityResult = 'pass'
	if ($Mode -eq 'session-landing') {
		$planReceipt = Get-PlanClaimReceipt $currentIdentity
		if ($null -ne $planReceipt) {
			$claimStatusResponse = Invoke-NativeText $result.worktreeCli.path @('plan','claim-status','--worktree',$currentIdentity,'--claim-receipt',$planReceipt.Path,'--claim-receipt-sha256',$planReceipt.Sha256) $currentIdentity
			if ([string]::IsNullOrWhiteSpace($claimStatusResponse.Stdout)) { Stop-Validation 'plan-claim.status-invalid' 'WorktreeCli did not return Plan claim status.' }
			try { $claimStatus = $claimStatusResponse.Stdout.Trim() | ConvertFrom-Json -Depth 20 -ErrorAction Stop } catch { Stop-Validation 'plan-claim.status-invalid' 'WorktreeCli returned invalid Plan claim status.' }
			$postAdvanceRecovery = $Checkpoint -ceq 'post-advance-recovery'
			if (($claimStatusResponse.ExitCode -ne 0 -or -not $claimStatus.ownedByReceipt) -and -not $postAdvanceRecovery) { Stop-Validation 'plan-claim.status-failed' 'The deterministic Plan receipt is not a live owned claim.' }
			$result.planClaim.present = $true
			$result.planClaim.state = if ($postAdvanceRecovery -and ($claimStatusResponse.ExitCode -ne 0 -or -not $claimStatus.ownedByReceipt)) { 'release-recovery' } else { [string]$claimStatus.claimState }
			if ($claimStatus.PSObject.Properties.Name -ccontains 'disposition') { $result.planClaim.disposition = [string]$claimStatus.disposition }
			if ($result.planClaim.state -ceq 'claimed') { Assert-PlanClaimReceiptPlanBytes $planReceipt $currentIdentity }
		}
	}

	if ($Mode -eq 'session-landing') {
		$module = Join-Path $currentIdentity '.agents\scripts\AgentWorktreeSession.psm1'
		Import-Module $module -Force
		try { $receipt = (Read-AgentWorktreeSessionReceipt -Worktree $currentIdentity).Value }
		catch { Stop-Validation 'receipt.unreadable' "Session-landing receipt is missing or failed strict validation: $($_.Exception.Message)" }
		# Compare the receipt's canonical worktree/primary paths to the resolved current/primary
		# identities without requiring the receipt paths to exist on disk. A nonexistent, foreign,
		# stale, or moved path yields the distinct receipt.worktree-mismatch / receipt.primary-mismatch
		# rather than identity.missing or state.unreadable; Test-ExistingIdentityEqual resolves an
		# existing path for a robust final-identity match and returns false (mismatch) for an absent or
		# divergent one.
		$result.claim.worktree = $receipt.worktree
		$result.claim.owner = $receipt.sessionOwner
		if ($receipt.sessionOwner -cne $SessionOwner) { Stop-Validation 'receipt.owner-mismatch' 'Session-landing receipt owner does not match the supplied SessionOwner.' }
		if (-not (Test-ExistingIdentityEqual $receipt.worktree $currentIdentity)) { Stop-Validation 'receipt.worktree-mismatch' 'Session-landing receipt worktree does not identify the current worktree.' }
		if ($receipt.branch -cne $CurrentBranch) { Stop-Validation 'receipt.branch-mismatch' 'Session-landing receipt branch does not match the current branch.' }
		if ($receipt.targetBranch -cne $PrimaryBranch) { Stop-Validation 'receipt.target-branch-mismatch' 'Session-landing receipt target branch does not match the primary branch.' }
		if (-not (Test-ExistingIdentityEqual $receipt.primaryCheckout $primaryIdentity)) { Stop-Validation 'receipt.primary-mismatch' 'Session-landing receipt primary checkout does not identify the primary worktree.' }
		if ($receipt.baseline -cne $Baseline) { Stop-Validation 'receipt.baseline-mismatch' 'Session-landing receipt baseline does not match the supplied baseline.' }
		$result.claim.classification = 'receipt-verified'
	}

	Complete-Preflight 0 'pass' 'ok' 'Finalization preflight passed.'
}
catch {
	Complete-Preflight 1 'error' 'state.unreadable' $_.Exception.Message
}
