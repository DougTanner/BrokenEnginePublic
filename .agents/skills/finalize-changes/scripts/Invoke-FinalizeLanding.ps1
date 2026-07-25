


# Exclusive owner of post-approval landing: baseline provenance, structural preflight, locks, ancestry proofs, ref advance, candidate certification, and crash recovery remain mandatory. Scheduler touchpoint is only receipt-bound terminal release after primary advances.
# caller reports LANDED; exit 2 may report a post-advance blocker but still
# carries the authoritative lock-cleanup state.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $CurrentWorktree,
	[Parameter(Mandatory)][string] $PrimaryWorktree,
	[Parameter(Mandatory)][string] $CurrentBranch,
	[Parameter(Mandatory)][string] $PrimaryBranch,
	[Parameter(Mandatory)][string] $Baseline,
	[Parameter(Mandatory)][string] $ExpectedCurrentTip,
	[Parameter(Mandatory)][string] $ExpectedPrimaryTip,
	[Parameter(Mandatory)][string] $SessionOwner,
	[Parameter(Mandatory)][string] $SessionLabel,
	[Parameter(Mandatory)][string] $ApprovedSessionCommit,
	[string] $ClaimReceiptPath,
	[string] $ClaimReceiptSha256,
	[ValidateSet('none', 'completed', 'rejected')][string] $TerminalDisposition = 'none',
	[string] $CandidateReceiptPath,
	[string] $CandidateReceiptSha256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$claimReceiptPathBound = $PSBoundParameters.ContainsKey('ClaimReceiptPath')
$claimReceiptSha256Bound = $PSBoundParameters.ContainsKey('ClaimReceiptSha256')
$candidateReceiptPathBound = $PSBoundParameters.ContainsKey('CandidateReceiptPath')
$candidateReceiptSha256Bound = $PSBoundParameters.ContainsKey('CandidateReceiptSha256')

$commonModule = Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1'
if (-not (Test-Path -LiteralPath $commonModule)) {
	$commonModule = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\FinalizeWorkflowCommon.psm1'
}
Import-Module $commonModule -Force

$exclusionModule = Join-Path $PSScriptRoot '..\..\..\scripts\WorktreeCliSessionExclusion.psm1'
if (-not (Test-Path -LiteralPath $exclusionModule)) {
	$exclusionModule = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\WorktreeCliSessionExclusion.psm1'
}
Import-Module $exclusionModule -Force

$result = [ordered]@{
	schemaVersion = 'broken-engine-finalize-landing/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Landing transaction did not complete.'
	primaryAdvanced = $false
	identities = [ordered]@{ currentWorktree = $null; primaryWorktree = $null; gitCommonDirectory = $null; currentBranch = $null; primaryBranch = $null }
	tips = [ordered]@{ approvedSession = $ApprovedSessionCommit; expectedCurrent = $ExpectedCurrentTip; expectedPrimary = $ExpectedPrimaryTip; current = $null; primary = $null }
	locks = [ordered]@{ landingOwner = $null; landingClaimed = $false; landingReleased = $false; claim = $null }
	planClaim = [ordered]@{ requested = $claimReceiptPathBound; released = $false; receipt = $ClaimReceiptPath }
	planValidation = $null
	candidateBootstrap = $null
	cleanup = [ordered]@{ worktreesClear = $null; worktreeProblems = @() }
	disposition = 'terminal'
	requiresUserAuthority = $false
	retryAfterMilliseconds = 0
	blocker = $null
	residuals = [Collections.Generic.List[string]]::new()
}
$script:WorktreeCliPath = $null
$script:LandingOwner = $null
$script:LandingClaimed = $false
$script:PrimaryIdentity = $null
$script:CurrentIdentity = $null
$script:ClaimReceiptPath = $ClaimReceiptPath
$script:ClaimReceiptSha256 = $ClaimReceiptSha256
$script:PlanCompletionTerminalProven = $false
$script:CertifiedForeignDiagnosticFingerprints = $null
$script:FailureExitCode = 0
$script:FailureCode = $null
$script:FailureMessage = $null
$script:LandingTransientOwner = $null

function Throw-Landing([int] $ExitCode, [string] $Code, [string] $Message, [string] $Disposition = 'terminal', [bool] $RequiresUserAuthority = $false, [int] $RetryAfterMilliseconds = 0) {
	$exception = [InvalidOperationException]::new($Message)
	$exception.Data['FinalizeExitCode'] = $ExitCode
	$exception.Data['FinalizeCode'] = $Code
	$exception.Data['FinalizeDisposition'] = $Disposition
	$exception.Data['FinalizeRequiresUserAuthority'] = $RequiresUserAuthority
	$exception.Data['FinalizeRetryAfterMilliseconds'] = $RetryAfterMilliseconds
	throw $exception
}

function Get-JsonResponse($Response, [string] $Operation) {
	if ([string]::IsNullOrWhiteSpace($Response.Stdout)) {
		Throw-Landing 1 'worktreecli.no-json' "$Operation returned no JSON. stderr: $($Response.Stderr.Trim())"
	}
	try {
		return $Response.Stdout.Trim() | ConvertFrom-Json -Depth 32 -ErrorAction Stop
	}
	catch {
		Throw-Landing 1 'worktreecli.invalid-json' "$Operation returned invalid JSON: $($Response.Stdout.Trim())"
	}
}

function Invoke-WorktreeCli([string[]] $Arguments) {
	return Invoke-FinalizeNativeText $script:WorktreeCliPath $Arguments $script:CurrentIdentity.Worktree
}

function Invoke-Preflight([string] $Checkpoint, [string] $CurrentTip, [string] $PrimaryTip) {
	$preflight = Join-Path $PSScriptRoot 'Test-FinalizePreflight.ps1'
	$arguments = [Collections.Generic.List[string]]::new()
	foreach ($argument in @(
		'-NoProfile', '-File', $preflight,
		'-Mode', 'session-landing',
		'-Checkpoint', $Checkpoint,
		'-CurrentWorktree', $CurrentWorktree,
		'-PrimaryWorktree', $PrimaryWorktree,
		'-CurrentBranch', $CurrentBranch,
		'-PrimaryBranch', $PrimaryBranch,
		'-Baseline', $Baseline,
		'-ExpectedCurrentTip', $CurrentTip,
		'-ExpectedPrimaryTip', $PrimaryTip
	)) { $arguments.Add($argument) }
	foreach ($argument in @('-SessionOwner', $SessionOwner, '-WaitSeconds', '60')) { $arguments.Add($argument) }
	if ($candidateReceiptPathBound) {
		$arguments.Add('-CandidateReceiptPath')
		$arguments.Add($CandidateReceiptPath)
		$arguments.Add('-CandidateReceiptSha256')
		$arguments.Add($CandidateReceiptSha256)
	}
	if ($claimReceiptPathBound) { foreach ($argument in @('-ClaimReceiptPath',$ClaimReceiptPath,'-ClaimReceiptSha256',$ClaimReceiptSha256)) { $arguments.Add($argument) } }
	$response = Invoke-FinalizeNativeText 'pwsh.exe' $arguments.ToArray() $script:CurrentIdentity.Worktree
	$preflightResult = Get-JsonResponse $response "finalization preflight $Checkpoint"
	if ($response.ExitCode -ne 0 -or $preflightResult.status -cne 'pass' -or $preflightResult.code -cne 'ok') {
		$exitCode = if ($response.ExitCode -eq 2) { 2 } else { 1 }
		Throw-Landing $exitCode "preflight.$($preflightResult.code)" "$Checkpoint preflight failed: $($preflightResult.message)"
	}
	$result.candidateBootstrap = $preflightResult.worktreeCli.candidate
	return $preflightResult
}

function Assert-ReconciledPlanMetadata {
	$arguments = @('plan', 'validate', '--repo', $result.identities.gitCommonDirectory, '--worktree', $script:CurrentIdentity.Worktree, '--baseline', $Baseline)
	if ($claimReceiptPathBound) { $arguments += @('--terminal-receipt', $ClaimReceiptPath, '--terminal-receipt-sha256', $ClaimReceiptSha256) }
	$response = Invoke-WorktreeCli $arguments
	$validation = Get-JsonResponse $response 'reconciled Plan metadata validation'
	# Record decision-relevant fields only: `plans` carries one entry per repository Plan and never informs the landing.
	$projectedValidation = [ordered]@{}
	foreach ($name in @('status', 'code', 'message', 'diagnostics', 'notices', 'healedClaims')) {
		if ($validation.PSObject.Properties.Name -ccontains $name) { $projectedValidation[$name] = $validation.$name }
	}
	$result.planValidation = $projectedValidation
	if ($response.ExitCode -ne 0 -or $validation.status -cne 'valid' -or $validation.code -cne 'ok') {
		$details = if ($validation.PSObject.Properties.Name -ccontains 'diagnostics') { @($validation.diagnostics | ConvertTo-Json -Depth 8 -Compress) -join '' } else { [string]$validation.message }
		Throw-Landing $(if ($response.ExitCode -eq 2) { 2 } else { 1 }) 'plan.validation-failed' "Reconciled Plan metadata is invalid; primary was not mutated. $details"
	}
}

function Assert-LandingOwner {
	$response = Invoke-WorktreeCli @('lock', 'status', '--repo', $result.identities.gitCommonDirectory)
	$status = Get-JsonResponse $response 'landing lock status'
	if ($response.ExitCode -ne 0 -or $status.owner -cne $script:LandingOwner -or $status.leaseState -cne 'live') {
		Throw-Landing 2 'landing-lock.not-owned' 'Landing lock is not live and owned by this transaction.'
	}
}

function Refresh-LandingOwner {
	$response = Invoke-WorktreeCli @('lock', 'refresh', '--repo', $result.identities.gitCommonDirectory, '--owner', $script:LandingOwner)
	if ($response.ExitCode -ne 0) {
		Throw-Landing 2 'landing-lock.refresh-failed' "Landing lock refresh failed: $($response.Stdout.Trim())$($response.Stderr.Trim())"
	}
	Assert-LandingOwner
}

function Release-LandingLockIfSafe {
	if (-not $script:LandingClaimed) { return }
	$clear = Test-FinalizeAllWorktreesClear $script:PrimaryIdentity.Worktree
	$result.cleanup.worktreesClear = $clear.Clear
	$result.cleanup.worktreeProblems = @($clear.Problems)
	if (-not $clear.Clear) {
		foreach ($problem in $clear.Problems) { $result.residuals.Add("Landing lock retained: $problem") }
		return
	}
	try {
		Refresh-LandingOwner
		$response = Invoke-WorktreeCli @('lock', 'release', '--repo', $result.identities.gitCommonDirectory, '--owner', $script:LandingOwner)
		if ($response.ExitCode -ne 0) {
			$result.residuals.Add("Landing lock release failed: $($response.Stdout.Trim())$($response.Stderr.Trim())")
			return
		}
		$script:LandingClaimed = $false
		$result.locks.landingReleased = $true
	}
	catch {
		$result.residuals.Add("Landing lock release error: $($_.Exception.Message)")
	}
}

function Assert-PrimaryAdvanceState {
	$primary = Get-FinalizeGitIdentity $PrimaryWorktree 'Primary worktree'
	$current = Get-FinalizeGitIdentity $CurrentWorktree 'Session worktree'
	if ($primary.Worktree -cne $script:PrimaryIdentity.Worktree -or $current.Worktree -cne $script:CurrentIdentity.Worktree -or
		$primary.Branch -cne $PrimaryBranch -or $current.Branch -cne $CurrentBranch -or
		$primary.Head -cne $ExpectedPrimaryTip -or $current.Head -cne $ApprovedSessionCommit) {
		Throw-Landing 2 'git.identity-changed' 'Primary or session identity changed after approval.'
	}
	if ((Invoke-FinalizeGit $primary.Worktree @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) {
		Throw-Landing 2 'git.primary-dirty' 'Primary worktree is not clean immediately before landing.'
	}
	if (-not (Test-FinalizeGitSuccess $primary.Worktree @('merge-base', '--is-ancestor', $ExpectedPrimaryTip, $ApprovedSessionCommit))) {
		Throw-Landing 2 'git.primary-not-ancestor' 'Approved session commit does not descend from the approved primary tip.'
	}
	if ((Invoke-FinalizeGit $primary.Worktree @('rev-list', "${ApprovedSessionCommit}..${ExpectedPrimaryTip}")).Trim().Length -ne 0 -or
		(Invoke-FinalizeGit $primary.Worktree @('rev-list', '--min-parents=2', "${ExpectedPrimaryTip}..${ApprovedSessionCommit}")).Trim().Length -ne 0) {
		Throw-Landing 2 'git.landing-history-invalid' 'Landing would replay primary commits or introduce a merge commit.'
	}
}

function Complete-LandedState {
	if ($claimReceiptPathBound) {
		$release = Invoke-WorktreeCli @('plan','release-after-landing','--worktree',$script:CurrentIdentity.Worktree,'--claim-receipt',$ClaimReceiptPath,'--claim-receipt-sha256',$ClaimReceiptSha256,'--landed-commit',$ApprovedSessionCommit)
		$releaseJson = Get-JsonResponse $release 'post-landing plan release'
		$result.planClaim.release = $releaseJson
		$hasReleased = $releaseJson.PSObject.Properties.Name -ccontains 'released' -and $releaseJson.released -is [bool] -and $releaseJson.released
		$hasAlreadyReleased = $releaseJson.PSObject.Properties.Name -ccontains 'alreadyReleased' -and $releaseJson.alreadyReleased -is [bool] -and $releaseJson.alreadyReleased
		$terminalStateVerified = $releaseJson.PSObject.Properties.Name -ccontains 'terminalStateVerified' -and $releaseJson.terminalStateVerified -is [bool] -and $releaseJson.terminalStateVerified
		$expectedCode = if ($hasReleased) { 'released' } elseif ($hasAlreadyReleased) { 'already-released' } else { '' }
		if ($release.ExitCode -ne 0 -or -not $terminalStateVerified -or $hasReleased -eq $hasAlreadyReleased -or $releaseJson.code -cne $expectedCode) { Throw-Landing $(if ($release.ExitCode -eq 2) { 2 } else { 1 }) 'plan.release-failed' 'Receipt-bound Plan release failed after primary advance.' }
		$result.planClaim.released = $true
	}
	$registration = Test-FinalizeWorktreeRegistration $script:PrimaryIdentity.Worktree $script:CurrentIdentity.Worktree $CurrentBranch $ApprovedSessionCommit
	if (-not $registration.Registered) { Throw-Landing 2 'session.registration-invalid' $registration.Message }
	if ((Invoke-FinalizeGit $script:CurrentIdentity.Worktree @('status','--porcelain','-z','--untracked-files=all')).Length -ne 0) { Throw-Landing 2 'session.dirty' 'Session worktree is dirty after landing.' }
	$result.status='landed';$result.code='ok';$result.message='Primary advanced and post-landing finalization completed.'
}

try {
	if ($ApprovedSessionCommit -cnotmatch '^[0-9a-f]{40}$' -or $ExpectedCurrentTip -cnotmatch '^[0-9a-f]{40}$' -or $ExpectedPrimaryTip -cnotmatch '^[0-9a-f]{40}$') {
		Throw-Landing 1 'input.commit-invalid' 'Approved and expected commits must be lowercase 40-character object IDs.'
	}
	if ($claimReceiptPathBound -ne $claimReceiptSha256Bound -or ($claimReceiptPathBound -and ([string]::IsNullOrWhiteSpace($ClaimReceiptPath) -or $ClaimReceiptSha256 -cnotmatch '^[0-9a-f]{64}$'))) { Throw-Landing 1 'input.plan-claim-invalid' 'Claim receipt path and SHA-256 must be supplied together.' }
	if (($TerminalDisposition -ceq 'none') -ne (-not $claimReceiptPathBound)) { Throw-Landing 1 'input.terminal-disposition-invalid' 'TerminalDisposition and the claim receipt must either both be supplied or both be absent.' }
	if ($candidateReceiptPathBound -ne $candidateReceiptSha256Bound -or ($candidateReceiptPathBound -and ([string]::IsNullOrWhiteSpace($CandidateReceiptPath) -or $CandidateReceiptSha256 -cnotmatch '^[0-9a-f]{64}$'))) {
		Throw-Landing 1 'input.candidate-invalid' 'Candidate receipt path and lowercase SHA-256 must be supplied together.'
	}

	$script:CurrentIdentity = Get-FinalizeGitIdentity $CurrentWorktree 'Session worktree'
	$script:PrimaryIdentity = Get-FinalizeGitIdentity $PrimaryWorktree 'Primary worktree'
	if (-not $script:CurrentIdentity.CommonDirectory.Equals($script:PrimaryIdentity.CommonDirectory, [StringComparison]::OrdinalIgnoreCase)) {
		Throw-Landing 1 'identity.repository-mismatch' 'Session and primary worktrees do not share one Git common directory.'
	}
	$result.identities.currentWorktree = $script:CurrentIdentity.Worktree
	$result.identities.primaryWorktree = $script:PrimaryIdentity.Worktree
	$result.identities.gitCommonDirectory = $script:CurrentIdentity.CommonDirectory
	$result.identities.currentBranch = $script:CurrentIdentity.Branch
	$result.identities.primaryBranch = $script:PrimaryIdentity.Branch
	$result.tips.current = $script:CurrentIdentity.Head
	$result.tips.primary = $script:PrimaryIdentity.Head
	# A transient operation claim with a fresh per-landing owner (never the durable receipt session
	# owner, whose reuse as a promotion cooperating exemption would hide a second attachment's
	# in-flight landing) excludes AgentTools promotion from swapping WorktreeCli.exe across this
	# multi-invocation landing transaction. Registered before the first WorktreeCli.exe use;
	# released in cleanup alongside the landing lock.
	$landingOwner = [guid]::NewGuid().ToString()
	Register-WorktreeCliSession -RepositoryRoot $script:CurrentIdentity.Worktree -Owner $landingOwner -Label 'session landing' -Worktree $script:CurrentIdentity.Worktree | Out-Null
	$script:LandingTransientOwner = $landingOwner
	if ($script:CurrentIdentity.Head -ceq $ApprovedSessionCommit -and
		(Test-FinalizeGitSuccess $script:PrimaryIdentity.Worktree @('merge-base', '--is-ancestor', $ApprovedSessionCommit, $script:PrimaryIdentity.Head))) {
		$preflight = Invoke-Preflight 'post-advance-recovery' $ApprovedSessionCommit $script:PrimaryIdentity.Head
		$script:WorktreeCliPath = [string]$preflight.worktreeCli.path
		$result.primaryAdvanced = $true
		Complete-LandedState
		[Console]::Out.Write(($result | ConvertTo-Json -Depth 12 -Compress))
		exit 0
	}

	$preflight = Invoke-Preflight 'pre-mutation' $ExpectedCurrentTip $ExpectedPrimaryTip
	if ($preflight.tips.current -cne $ApprovedSessionCommit) { Throw-Landing 2 'approval.session-tip-changed' 'Session tip is not the explicit user-approved commit.' }
	$script:WorktreeCliPath = [string] $preflight.worktreeCli.path
	$result.identities.currentWorktree = [string] $preflight.identities.currentWorktree
	$result.identities.primaryWorktree = [string] $preflight.identities.primaryWorktree
	$result.identities.gitCommonDirectory = [string] $preflight.identities.gitCommonDirectory
	if ($claimReceiptPathBound) {
		$operation = if ($TerminalDisposition -ceq 'rejected') { 'prepare-rejection' } else { 'prepare-completion' }
		$prepareArguments = @('plan', $operation, '--repo', $result.identities.gitCommonDirectory, '--worktree', $script:CurrentIdentity.Worktree, '--claim-receipt', $ClaimReceiptPath, '--claim-receipt-sha256', $ClaimReceiptSha256)
		if ($TerminalDisposition -ceq 'rejected') { $prepareArguments += '--user-authorized-rejection' }
		$preparedResponse = Invoke-WorktreeCli $prepareArguments
		$prepared = Get-JsonResponse $preparedResponse 'pre-landing terminal preparation'
		if ($preparedResponse.ExitCode -ne 0 -or -not $prepared.prepared -or $prepared.claimState -cne 'awaiting-landing') {
			Throw-Landing $(if ($preparedResponse.ExitCode -eq 2) { 2 } else { 1 }) 'plan.prepare-failed' 'Receipt-bound Plan terminal preparation could not be proven before landing.'
		}
		if ($prepared.PSObject.Properties.Name -cnotcontains 'disposition' -or $prepared.disposition -isnot [string] -or $prepared.disposition -cne $TerminalDisposition) {
			Throw-Landing 2 'plan.disposition-mismatch' 'Prepared Plan terminal disposition does not match the approved landing disposition.'
		}
		$result.planClaim.preparation = $prepared
		if ($prepared.PSObject.Properties.Name -ccontains 'changedPaths' -and @($prepared.changedPaths).Count -ne 0) {
			Throw-Landing 2 'approval.refresh-required' 'Terminal preparation changed Plan metadata after approval; revalidate and obtain refreshed landing confirmation.'
		}
	}
	Assert-ReconciledPlanMetadata

	$tokenResponse = Invoke-WorktreeCli @('lock', 'token')
	if ($tokenResponse.ExitCode -ne 0 -or $tokenResponse.Stdout.Trim() -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') {
		Throw-Landing 1 'landing-lock.token-failed' 'WorktreeCli could not generate a canonical landing owner token.'
	}
	$script:LandingOwner = $tokenResponse.Stdout.Trim()
	$result.locks.landingOwner = $script:LandingOwner
	$claimOutcome = Invoke-FinalizeLandingLockClaim -WorktreeCliExecutable $script:WorktreeCliPath -GitCommonDirectory $result.identities.gitCommonDirectory -Owner $script:LandingOwner -Session $SessionLabel -Worktree $script:CurrentIdentity.Worktree -LeaseSeconds 3600 -WaitSeconds 55
	$result.locks.claim = [ordered]@{
		code = $claimOutcome.Code
		disposition = $claimOutcome.Disposition
		requiresUserAuthority = $claimOutcome.RequiresUserAuthority
		retryAfterMilliseconds = $claimOutcome.RetryAfterMilliseconds
		attempts = $claimOutcome.Attempts
		lock = $claimOutcome.Lock
	}
	if (-not $claimOutcome.Claimed) {
		$exitCode = if ($claimOutcome.Disposition -ceq 'terminal') { 1 } else { 2 }
		Throw-Landing $exitCode 'landing-lock.claim-failed' $claimOutcome.Message $claimOutcome.Disposition $claimOutcome.RequiresUserAuthority $claimOutcome.RetryAfterMilliseconds
	}
	$script:LandingClaimed = $true
	$result.locks.landingClaimed = $true
	Assert-LandingOwner

	Assert-PrimaryAdvanceState
	Refresh-LandingOwner
	$rebase = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:PrimaryIdentity.Worktree, 'rebase', $ApprovedSessionCommit) $script:PrimaryIdentity.Worktree
	if ($rebase.ExitCode -ne 0) { Throw-Landing 1 'git.primary-rebase-failed' "Primary rebase failed: $($rebase.Stdout)$($rebase.Stderr)" }
	$result.primaryAdvanced = $true
	$result.tips.current = $ApprovedSessionCommit
	$result.tips.primary = $ApprovedSessionCommit
	Release-LandingLockIfSafe
	if ($script:LandingClaimed) { Throw-Landing 2 'landing-lock.release-failed' 'Landing lock could not be released after the primary advance.' }
	Complete-LandedState
}
catch {
	$script:FailureExitCode = if ($_.Exception.Data.Contains('FinalizeExitCode')) { [int] $_.Exception.Data['FinalizeExitCode'] } else { 1 }
	$script:FailureCode = if ($_.Exception.Data.Contains('FinalizeCode')) { [string] $_.Exception.Data['FinalizeCode'] } else { 'internal.error' }
	$script:FailureMessage = $_.Exception.Message
	$result.disposition = if ($_.Exception.Data.Contains('FinalizeDisposition')) { [string] $_.Exception.Data['FinalizeDisposition'] } else { 'terminal' }
	$result.requiresUserAuthority = if ($_.Exception.Data.Contains('FinalizeRequiresUserAuthority')) { [bool] $_.Exception.Data['FinalizeRequiresUserAuthority'] } else { $false }
	$result.retryAfterMilliseconds = if ($_.Exception.Data.Contains('FinalizeRetryAfterMilliseconds')) { [int] $_.Exception.Data['FinalizeRetryAfterMilliseconds'] } else { 0 }
	$result.blocker = [ordered]@{
		disposition = $result.disposition
		requiresUserAuthority = $result.requiresUserAuthority
		retryAfterMilliseconds = $result.retryAfterMilliseconds
	}
}
finally {
	Release-LandingLockIfSafe
	if ($null -ne $script:LandingTransientOwner) {
		try { Unregister-WorktreeCliSession -RepositoryRoot $script:CurrentIdentity.Worktree -Owner $script:LandingTransientOwner }
		catch { $result.residuals.Add("Landing transient claim release failed: $($_.Exception.Message)") }
	}
}

if ($script:FailureExitCode -ne 0) {
	$result.status = if ($script:FailureExitCode -eq 2) { 'blocked' } else { 'error' }
	$result.code = $script:FailureCode
	$result.message = $script:FailureMessage
}
if ($script:LandingClaimed) {
	if ($result.status -eq 'landed') {
		$result.status = 'blocked'
		$result.code = 'cleanup.incomplete'
		$result.message = 'Landing completed but required lock cleanup was not proven complete.'
	}
	if ($result.status -eq 'error') { $result.code = 'cleanup.' + $result.code }
}
[Console]::Out.Write(($result | ConvertTo-Json -Depth 12 -Compress))
exit $(if ($result.status -eq 'landed') { 0 } elseif ($result.status -eq 'blocked') { 2 } else { 1 })
