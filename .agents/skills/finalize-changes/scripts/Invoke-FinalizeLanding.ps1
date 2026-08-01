# Exclusive owner of post-confirmation landing: structural sanity, the landing lock lease,
# the guarded primary advance, and best-effort Plan claim deletion. Exit 0 means the caller
# reports LANDED; exit 2 may report a post-advance blocker but still carries the
# authoritative lock-cleanup state.
#
# The scheduler is touched only when -ReleasePlanClaim says this session holds a claim:
# without it the landing runs no `plan` command at all.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $CurrentWorktree,
	[Parameter(Mandatory)][string] $PrimaryWorktree,
	[Parameter(Mandatory)][string] $CurrentBranch,
	[Parameter(Mandatory)][string] $PrimaryBranch,
	[Parameter(Mandatory)][string] $ExpectedCurrentTip,
	[Parameter(Mandatory)][string] $ExpectedPrimaryTip,
	[Parameter(Mandatory)][string] $SessionLabel,
	[Parameter(Mandatory)][string] $ApprovedSessionCommit,
	[Parameter(Mandatory)][string] $ApprovedCandidateTree,
	[switch] $ReleasePlanClaim,
	[ValidateSet('none', 'compare-and-swap', 'post-reset', 'bounded-diagnostic')][string] $FixtureFailure = 'none'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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
	schemaVersion = 'broken-engine-finalize-landing/v2'
	status = 'error'
	code = 'internal.error'
	message = 'Landing transaction did not complete.'
	primaryAdvanced = $false
	identities = [ordered]@{ currentWorktree = $null; primaryWorktree = $null; gitCommonDirectory = $null; currentBranch = $null; primaryBranch = $null }
	tips = [ordered]@{ approvedSession = $ApprovedSessionCommit; expectedCurrent = $ExpectedCurrentTip; expectedPrimary = $ExpectedPrimaryTip; current = $null; primary = $null }
	candidate = [ordered]@{ commit = $ApprovedSessionCommit; tree = $ApprovedCandidateTree; treeVerified = $false }
	locks = [ordered]@{ landingOwner = $null; landingClaimed = $false; landingReleased = $false; claim = $null }
	planClaim = [ordered]@{ requested = [bool]$ReleasePlanClaim; released = $false }
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
$script:FailureExitCode = 0
$script:FailureCode = $null
$script:FailureMessage = $null
$script:LandingTransientOwner = $null

function Get-BoundedLandingText([AllowNull()] $Value, [int] $Limit) {
	if ($null -eq $Value) { return [pscustomobject]@{ Text = $null; Length = 0; Truncated = $false } }
	$Value = [string]$Value
	return [pscustomobject]@{ Text = $(if ($Value.Length -gt $Limit) { $Value.Substring(0, $Limit) } else { $Value }); Length = $Value.Length; Truncated = ($Value.Length -gt $Limit) }
}

function Get-LandingGitObjectId([AllowNull()] $Value) {
	if ($null -ne $Value -and [string]$Value -cmatch '^[0-9a-f]{40}$') { return [string]$Value }
	return $null
}

function New-LandingCollection([object[]] $Values, [scriptblock] $Project, [string] $Requery) {
	[object[]]$all = @($Values)
	for ($index = 1; $index -lt $all.Count; $index++) {
		$value = $all[$index]; $cursor = $index - 1
		while ($cursor -ge 0 -and [StringComparer]::Ordinal.Compare([string]$all[$cursor], [string]$value) -gt 0) { $all[$cursor + 1] = $all[$cursor]; $cursor-- }
		$all[$cursor + 1] = $value
	}
	$items = @($all | Select-Object -First 16 | ForEach-Object { & $Project $_ })
	$truncated = $all.Count -gt 16
	return [ordered]@{ totalCount = $all.Count; items = $items; truncated = $truncated; selector = $null; requery = $(if ($truncated) { $Requery } else { $null }) }
}

function New-LandingProjection {
	$message = Get-BoundedLandingText ([string]$result.message) 512
	$code = Get-BoundedLandingText ([string]$result.code) 128
	$diagnosticValues = [Collections.Generic.List[object]]::new()
	if ($result.status -cne 'landed') { $diagnosticValues.Add([pscustomobject]@{ source = 'Invoke-FinalizeLanding'; code = $result.code; message = $result.message }) }
	$diagnostics = New-LandingCollection $diagnosticValues.ToArray() {
		param($entry)
		$sourceValue = if ($entry.PSObject.Properties.Name -ccontains 'source') { [string]$entry.source } else { 'WorktreeCli' }
		$codeValue = if ($entry.PSObject.Properties.Name -ccontains 'code') { [string]$entry.code } else { 'diagnostic' }
		$pathValue = if ($entry.PSObject.Properties.Name -ccontains 'path') { [string]$entry.path } elseif ($entry.PSObject.Properties.Name -ccontains 'plan') { [string]$entry.plan } else { $null }
		$messageValue = if ($entry.PSObject.Properties.Name -ccontains 'message') { [string]$entry.message } else { [string]$entry }
		$codeText = Get-BoundedLandingText $codeValue 128; $pathText = Get-BoundedLandingText $pathValue 1024; $messageText = Get-BoundedLandingText $messageValue 512
		[ordered]@{ source = $sourceValue; code = $codeText.Text; codeLength = $codeText.Length; codeTruncated = $codeText.Truncated; path = $pathText.Text; pathLength = $pathText.Length; pathTruncated = $pathText.Truncated; message = $messageText.Text; messageLength = $messageText.Length; messageTruncated = $messageText.Truncated }
	} 'Invoke-FinalizeLanding'
	$problems = New-LandingCollection @($result.cleanup.worktreeProblems) {
		param($problem)
		$pathValue = if ($problem.PSObject.Properties.Name -ccontains 'path') { [string]$problem.path } else { $null }
		$messageValue = if ($problem.PSObject.Properties.Name -ccontains 'message') { [string]$problem.message } else { [string]$problem }
		$pathText = Get-BoundedLandingText $pathValue 1024; $messageText = Get-BoundedLandingText $messageValue 512
		[ordered]@{ path = $pathText.Text; pathLength = $pathText.Length; pathTruncated = $pathText.Truncated; message = $messageText.Text; messageLength = $messageText.Length; messageTruncated = $messageText.Truncated }
	} 'Invoke-FinalizeLanding'
	$residuals = New-LandingCollection @($result.residuals) { param($residual); $text = Get-BoundedLandingText ([string]$residual) 512; [ordered]@{ message = $text.Text; messageLength = $text.Length; messageTruncated = $text.Truncated } } 'Invoke-FinalizeLanding'
	return [ordered]@{
		schemaVersion = 'broken-engine-finalize-landing/v2'; status = $result.status; code = $code.Text; message = $message.Text; messageLength = $message.Length; messageTruncated = $message.Truncated
		primaryAdvanced = [bool]$result.primaryAdvanced; candidate = [ordered]@{ commit = Get-LandingGitObjectId $result.candidate.commit; tree = Get-LandingGitObjectId $result.candidate.tree; treeVerified = [bool]$result.candidate.treeVerified }
		planClaim = [ordered]@{ requested = [bool]$result.planClaim.requested; released = [bool]$result.planClaim.released }
		lock = [ordered]@{ claimed = [bool]$result.locks.landingClaimed; released = [bool]$result.locks.landingReleased; claimCode = $(if ($null -ne $result.locks.claim) { [string]$result.locks.claim.code } else { $null }); disposition = $result.disposition; requiresUserAuthority = [bool]$result.requiresUserAuthority; retryAfterMilliseconds = [int]$result.retryAfterMilliseconds; attempts = $(if ($null -ne $result.locks.claim) { [int]$result.locks.claim.attempts } else { 0 }) }
		cleanup = [ordered]@{ worktreesClear = $result.cleanup.worktreesClear; problems = $problems }
		disposition = $result.disposition; requiresUserAuthority = [bool]$result.requiresUserAuthority; retryAfterMilliseconds = [int]$result.retryAfterMilliseconds; diagnostics = $diagnostics; residuals = $residuals
	}
}

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

function Assert-LandingSanity([string] $ExpectedSessionTip, [string] $ExpectedPrimaryTipValue) {
	$sanity = Test-FinalizeLandingSanity -SessionWorktree $CurrentWorktree -PrimaryWorktree $PrimaryWorktree `
		-SessionBranch $CurrentBranch -PrimaryBranch $PrimaryBranch -ExpectedSessionTip $ExpectedSessionTip -ExpectedPrimaryTip $ExpectedPrimaryTipValue
	if (-not $sanity.Ok) { Throw-Landing 2 "sanity.$($sanity.Code)" "Landing sanity failed: $($sanity.Message)" }
	$script:WorktreeCliPath = $sanity.WorktreeCliExecutable
	return $sanity
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
	if ((Invoke-FinalizeGit $primary.Worktree @('rev-parse', "$ApprovedSessionCommit^{tree}")).Trim() -cne $ApprovedCandidateTree) {
		Throw-Landing 2 'candidate.tree-changed' 'Approved session commit no longer has the reviewed candidate tree.'
	}
	$result.candidate.treeVerified = $true
	if (-not (Test-FinalizeGitSuccess $primary.Worktree @('merge-base', '--is-ancestor', $ExpectedPrimaryTip, $ApprovedSessionCommit))) {
		Throw-Landing 2 'git.primary-not-ancestor' 'Approved session commit does not descend from the approved primary tip.'
	}
	if ((Invoke-FinalizeGit $primary.Worktree @('rev-list', "${ApprovedSessionCommit}..${ExpectedPrimaryTip}")).Trim().Length -ne 0 -or
		(Invoke-FinalizeGit $primary.Worktree @('rev-list', '--min-parents=2', "${ExpectedPrimaryTip}..${ApprovedSessionCommit}")).Trim().Length -ne 0) {
		Throw-Landing 2 'git.landing-history-invalid' 'Landing would replay primary commits or introduce a merge commit.'
	}
}

function Advance-PrimaryExactCandidate {
	$expectedCheckout = (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', 'HEAD')).Trim()
	$expectedStatus = Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('status', '--porcelain=v1', '-z', '--untracked-files=all')
	$expectedForCas = if ($FixtureFailure -ceq 'compare-and-swap') { '0000000000000000000000000000000000000000' } else { $ExpectedPrimaryTip }
	$advance = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:PrimaryIdentity.Worktree, 'update-ref', "refs/heads/$PrimaryBranch", $ApprovedSessionCommit, $expectedForCas) $script:PrimaryIdentity.Worktree
	if ($advance.ExitCode -ne 0) { Throw-Landing 2 'git.compare-and-swap-failed' 'Primary branch changed before exact candidate advance.' }
	$result.primaryAdvanced = $true
	$result.tips.current = $ApprovedSessionCommit
	$result.tips.primary = $ApprovedSessionCommit
	try {
		$reset = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:PrimaryIdentity.Worktree, 'reset', '--hard', $ApprovedSessionCommit) $script:PrimaryIdentity.Worktree
		if ($reset.ExitCode -ne 0) { throw "Primary checkout did not update to the exact candidate: $($reset.Stderr.Trim())" }
		if ($FixtureFailure -ceq 'post-reset') { throw 'Fixture forced post-reset failure.' }
		$actual = (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', "refs/heads/$PrimaryBranch")).Trim()
		$actualTree = (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', "$actual^{tree}")).Trim()
		if ($actual -cne $ApprovedSessionCommit -or $actualTree -cne $ApprovedCandidateTree) { throw 'Primary ref does not equal the exact verified candidate and tree.' }
	}
	catch {
		$reason = $_.Exception.Message
		$rollback = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:PrimaryIdentity.Worktree, 'update-ref', "refs/heads/$PrimaryBranch", $ExpectedPrimaryTip, $ApprovedSessionCommit) $script:PrimaryIdentity.Worktree
		if ($rollback.ExitCode -ne 0) { Throw-Landing 1 'git.rollback-failed' "Exact candidate advance postcondition failed and guarded rollback failed: $reason" }
		$restore = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:PrimaryIdentity.Worktree, 'reset', '--hard', $expectedCheckout) $script:PrimaryIdentity.Worktree
		if ($restore.ExitCode -ne 0 -or (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', "refs/heads/$PrimaryBranch")).Trim() -cne $ExpectedPrimaryTip -or (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', 'HEAD')).Trim() -cne $expectedCheckout -or (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('status', '--porcelain=v1', '-z', '--untracked-files=all')) -cne $expectedStatus) { Throw-Landing 1 'git.rollback-failed' "Exact candidate advance rollback did not restore the expected primary checkout: $reason" }
		$result.primaryAdvanced = $false
		Throw-Landing 2 'candidate.postcondition-failed' $reason
	}
}

# The claim is machine-local bookkeeping, not landed state: a failed delete leaves a stale
# lease that expires on its own, so it is reported as a residual and never blocks a landing.
function Complete-LandedState {
	if ($ReleasePlanClaim) {
		try {
			$sessionModule = Join-Path $script:CurrentIdentity.Worktree '.agents\scripts\AgentWorktreeSession.psm1'
			if (-not (Test-Path -LiteralPath $sessionModule -PathType Leaf)) { $sessionModule = Join-Path $PSScriptRoot '..\..\..\scripts\AgentWorktreeSession.psm1' }
			Import-Module $sessionModule -Force -DisableNameChecking
			$context = Get-AgentWorktreeSessionContext -Worktree $script:CurrentIdentity.Worktree
			if ([string]::IsNullOrWhiteSpace($context.SessionId)) { throw "Branch '$($context.Branch)' carries no session identity to release a claim for." }
			$unclaim = Invoke-WorktreeCli @('plan', 'unclaim', '--repo', $result.identities.gitCommonDirectory, '--worktree', $script:CurrentIdentity.Worktree, '--owner', $context.SessionId, '--session', $context.SessionId)
			$unclaimJson = $unclaim.Stdout.Trim() | ConvertFrom-Json -Depth 32 -ErrorAction Stop
			if ($unclaim.ExitCode -ne 0 -or [string]$unclaimJson.code -cnotin @('released', 'already-absent', 'none')) {
				throw "WorktreeCli reported '$($unclaimJson.code)': $($unclaimJson.message)"
			}
			$result.planClaim.released = $true
		}
		catch {
			$result.residuals.Add("Plan claim delete failed after landing; the machine-local claim expires on its own: $($_.Exception.Message)")
		}
	}
	$registration = Test-FinalizeWorktreeRegistration $script:PrimaryIdentity.Worktree $script:CurrentIdentity.Worktree $CurrentBranch $ApprovedSessionCommit
	if (-not $registration.Registered) { Throw-Landing 2 'session.registration-invalid' $registration.Message }
	if ((Invoke-FinalizeGit $script:CurrentIdentity.Worktree @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) { Throw-Landing 2 'session.dirty' 'Session worktree is dirty after landing.' }
	$result.status = 'landed'; $result.code = 'ok'; $result.message = 'Primary advanced and post-landing finalization completed.'
}

try {
	if ($FixtureFailure -cne 'none' -and $env:BROKEN_ENGINE_FINALIZE_WORKFLOW_FIXTURE -cne '1') { Throw-Landing 1 'input.fixture-forbidden' 'Fixture-only inputs require the finalization workflow fixture environment.' }
	if ($FixtureFailure -ceq 'bounded-diagnostic') { Throw-Landing 1 (('c' * 140)) (('m' * 600)) }
	if ($ApprovedSessionCommit -cnotmatch '^[0-9a-f]{40}$' -or $ExpectedCurrentTip -cnotmatch '^[0-9a-f]{40}$' -or $ExpectedPrimaryTip -cnotmatch '^[0-9a-f]{40}$') {
		Throw-Landing 1 'input.commit-invalid' 'Approved and expected commits must be lowercase 40-character object IDs.'
	}
	if ($ApprovedCandidateTree -cnotmatch '^[0-9a-f]{40}$') { Throw-Landing 1 'input.candidate-tree-invalid' 'ApprovedCandidateTree must be a lowercase 40-character object ID.' }

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
	# A transient operation claim with a fresh per-landing owner excludes AgentTools promotion
	# from swapping WorktreeCli.exe across this multi-invocation landing transaction. Registered
	# before the first WorktreeCli.exe use; released in cleanup alongside the landing lock.
	$landingOwner = [guid]::NewGuid().ToString()
	Register-WorktreeCliSession -RepositoryRoot $script:CurrentIdentity.Worktree -Owner $landingOwner -Label 'session landing' -Worktree $script:CurrentIdentity.Worktree | Out-Null
	$script:LandingTransientOwner = $landingOwner
	if ($script:CurrentIdentity.Head -ceq $ApprovedSessionCommit -and
		(Test-FinalizeGitSuccess $script:PrimaryIdentity.Worktree @('merge-base', '--is-ancestor', $ApprovedSessionCommit, $script:PrimaryIdentity.Head))) {
		if ((Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', "$ApprovedSessionCommit^{tree}")).Trim() -cne $ApprovedCandidateTree) { Throw-Landing 2 'candidate.tree-changed' 'Recovery candidate tree does not equal the exact verified tree.' }
		[void] (Assert-LandingSanity $ApprovedSessionCommit $script:PrimaryIdentity.Head)
		$result.primaryAdvanced = $true
		Complete-LandedState
		Write-Output ((New-LandingProjection) | ConvertTo-Json -Depth 10 -Compress)
		exit 0
	}

	[void] (Assert-LandingSanity $ExpectedCurrentTip $ExpectedPrimaryTip)
	if ($ExpectedCurrentTip -cne $ApprovedSessionCommit) { Throw-Landing 2 'approval.session-tip-changed' 'Session tip is not the explicit user-approved commit.' }

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
	Advance-PrimaryExactCandidate
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
Write-Output ((New-LandingProjection) | ConvertTo-Json -Depth 10 -Compress)
exit $(if ($result.status -eq 'landed') { 0 } elseif ($result.status -eq 'blocked') { 2 } else { 1 })
