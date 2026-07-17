# Exclusive owner of the post-approval session-landing transaction: every lock,
# queue, Git, cleanup, and row-release action. Callers pass the exact
# approval-covered session commit, the current/primary identities and expected tips
# from the final reconciliation preflight, report path/hash and returned manifest
# range, wrapper owner/session label, and any retained row-claim locator; for a
# completed-plan claim, also the exact latest WorktreeCli `plan order complete`
# receipt JSON — the executor requires its owner, removed plan, and both
# queue-byte hashes to match landed primary before unclaiming. None of these
# operations may be assembled as inline PowerShell by a caller.
#
# Transaction: re-runs preflight, derives the canonical Git common directory and
# queue-changing scope from the manifest, claims the PC-global landing lock and
# (for a queue-changing landing) the Plans/Features queue locks in canonical path
# order, refreshes owner-only leases around every mutation, proves primary is an
# ancestor with no commits to replay (`git merge-base --is-ancestor`, empty
# rev-lists, no multi-parent commits), advances primary with `git rebase
# <approved-session-commit>` (a pure fast-forward ref advance), performs
# post-mutation preflight, writes the commit-keyed global landing artifact before
# row release, reverses owner-held queue locks in reverse canonical order,
# conditionally releases the landing lock, and releases a verified row claim.
#
# Universal acquired-lock safe-stop rule (owned here): on any cancellation,
# blocker, or failure after the first queue-lock acquisition attempt — partial
# acquisition, ownership failure, identity/ancestry/cleanliness failure,
# pre/post-mutation failure, Git failure, or the normal unlock path — visit the
# successfully acquired queue locators once in reverse canonical order, owner-check
# each, unlock only records still owned by the landing owner, and prove each
# locator absent. Never unlock an absent, foreign-owned, or unverifiable record;
# retain and report anything that cannot be proven released. Only after every
# acquired queue is proven released may the landing claim follow the
# clear-worktree release/active-retain rule.
#
# Result contract: broken-engine-finalize-landing/v1 JSON; exit 0 before the
# caller reports LANDED; exit 2 may report a post-advance blocker but still
# carries the authoritative lock-cleanup state. If the artifact write fails after
# the Git advance, the executor reports the landed-but-blocked artifact residual,
# releases its locks, and retains the row claim; after restoring artifact-store
# access, callers retry only with -ArtifactOnly and both expected tips equal to
# the landed session commit — it reruns post-mutation validation, writes the
# artifact, and releases retained ownership without another Git mutation.
#
# -ValidateOnly runs every input-shape and binding check — parameters, Git
# identity/ancestry state, receipt fields and queue hashes against the expected
# post-landing tree (the session worktree), verification report, artifact-writer
# reachability, and the read-only pre-mutation preflight — without claiming any
# lock or mutating anything; it reports status 'validated' with exit 0.
# CompletedPlanReceipt accepts either the WorktreeCli completion receipt JSON
# content or a path to a file containing it.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $CurrentWorktree,
	[Parameter(Mandatory)][string] $PrimaryWorktree,
	[Parameter(Mandatory)][string] $CurrentBranch,
	[Parameter(Mandatory)][string] $PrimaryBranch,
	[Parameter(Mandatory)][string] $Baseline,
	[Parameter(Mandatory)][string] $ManifestComparisonBase,
	[Parameter(Mandatory)][string] $ExpectedCurrentTip,
	[Parameter(Mandatory)][string] $ExpectedPrimaryTip,
	[Parameter(Mandatory)][string] $VerificationReportPath,
	[Parameter(Mandatory)][string] $VerificationReportSha256,
	[Parameter(Mandatory)][string[]] $ManifestRange,
	[Parameter(Mandatory)][string] $SessionOwner,
	[Parameter(Mandatory)][string] $SessionLabel,
	[Parameter(Mandatory)][string] $ApprovedSessionCommit,
	[switch] $HasPlanRowClaim,
	[switch] $HasCompletedPlanClaim,
	[string] $PlanOrder,
	[string] $Plan,
	[string] $CompletedPlanReceipt,
	[switch] $ArtifactOnly,
	[switch] $ValidateOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$commonModule = Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1'
if (-not (Test-Path -LiteralPath $commonModule)) {
	$commonModule = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\FinalizeWorkflowCommon.psm1'
}
Import-Module $commonModule -Force

$result = [ordered]@{
	schemaVersion = 'broken-engine-finalize-landing/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Landing transaction did not complete.'
	primaryAdvanced = $false
	identities = [ordered]@{ currentWorktree = $null; primaryWorktree = $null; gitCommonDirectory = $null; currentBranch = $null; primaryBranch = $null }
	tips = [ordered]@{ approvedSession = $ApprovedSessionCommit; expectedCurrent = $ExpectedCurrentTip; expectedPrimary = $ExpectedPrimaryTip; current = $null; primary = $null }
	locks = [ordered]@{ landingOwner = $null; landingClaimed = $false; landingReleased = $false; queueChanging = $false; queues = [Collections.Generic.List[object]]::new() }
	planRow = [ordered]@{ requested = $HasPlanRowClaim.IsPresent; completedReceiptVerified = $false; released = $false; order = $PlanOrder; plan = $Plan }
	artifact = [ordered]@{ status = 'not-written'; path = $null; sha256 = $null; transcriptStatus = $null }
	cleanup = [ordered]@{ worktreesClear = $null; worktreeProblems = @(); queueReleaseComplete = $false }
	residuals = [Collections.Generic.List[string]]::new()
}
$script:WorktreeCliPath = $null
$script:LandingOwner = $null
$script:LandingClaimed = $false
$script:PrimaryIdentity = $null
$script:CurrentIdentity = $null
$script:QueueLocks = [Collections.Generic.List[object]]::new()
$script:PlanRowKey = $null
$script:FailureExitCode = 0
$script:FailureCode = $null
$script:FailureMessage = $null

function Throw-Landing([int] $ExitCode, [string] $Code, [string] $Message) {
	$exception = [InvalidOperationException]::new($Message)
	$exception.Data['FinalizeExitCode'] = $ExitCode
	$exception.Data['FinalizeCode'] = $Code
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

function Invoke-Preflight([string] $Checkpoint, [string] $CurrentTip, [string] $PrimaryTip, [bool] $QueueChanging) {
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
		'-ManifestComparisonBase', $ManifestComparisonBase,
		'-ExpectedCurrentTip', $CurrentTip,
		'-ExpectedPrimaryTip', $PrimaryTip,
		'-VerificationReportPath', $VerificationReportPath,
		'-VerificationReportSha256', $VerificationReportSha256,
		'-ManifestRange'
	)) { $arguments.Add($argument) }
	$arguments.Add(($ManifestRange -join ','))
	foreach ($argument in @('-SessionOwner', $SessionOwner, '-WaitSeconds', '60')) { $arguments.Add($argument) }
	if ($HasPlanRowClaim) { $arguments.Add('-HasPlanRowClaim') }
	if ($HasCompletedPlanClaim) { $arguments.Add('-HasCompletedPlanClaim') }
	if ($QueueChanging) { $arguments.Add('-QueueChangingLanding') }
	$response = Invoke-FinalizeNativeText 'pwsh.exe' $arguments.ToArray() $script:CurrentIdentity.Worktree
	$preflightResult = Get-JsonResponse $response "finalization preflight $Checkpoint"
	if ($response.ExitCode -ne 0 -or $preflightResult.status -cne 'pass' -or $preflightResult.code -cne 'ok') {
		$exitCode = if ($response.ExitCode -eq 2) { 2 } else { 1 }
		Throw-Landing $exitCode "preflight.$($preflightResult.code)" "$Checkpoint preflight failed: $($preflightResult.message)"
	}
	return $preflightResult
}

function Test-QueueChangingManifest([string[]] $Rows) {
	foreach ($row in $Rows) {
		$delimiter = $row.LastIndexOf([char]9)
		if ($delimiter -le 0) { Throw-Landing 1 'manifest.invalid' "Canonical manifest row is malformed: '$row'." }
		$path = $row.Substring(0, $delimiter)
		if ($path -ceq 'Documents/Plans/Order.md' -or $path -ceq 'Documents/Features/Order.md' -or
			$path.StartsWith('Documents/Plans/', [StringComparison]::Ordinal) -or $path.StartsWith('Documents/Features/', [StringComparison]::Ordinal)) {
			return $true
		}
	}
	return $false
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

function Acquire-QueueLock([string] $Order) {
	$response = Invoke-WorktreeCli @('plan', 'queue', 'lock', '--repo', $result.identities.gitCommonDirectory, '--order', $Order, '--owner', $script:LandingOwner, '--session', $SessionLabel, '--worktree', $script:CurrentIdentity.Worktree)
	if ($response.ExitCode -ne 0) {
		$detail = if ([string]::IsNullOrWhiteSpace($response.Stdout)) { $response.Stderr.Trim() } else { $response.Stdout.Trim() }
		Throw-Landing 2 'queue-lock.acquire-failed' "Could not acquire queue '$Order': $detail"
	}
	$script:QueueLocks.Add([pscustomobject]@{ Order = $Order; Acquired = $true; Released = $false; ReleaseResult = 'not-attempted' })
	$result.locks.queues.Add($script:QueueLocks[$script:QueueLocks.Count - 1])
	$response = Invoke-WorktreeCli @('plan', 'queue', 'status', '--repo', $result.identities.gitCommonDirectory, '--order', $Order, '--owner', $script:LandingOwner)
	$status = Get-JsonResponse $response "queue status $Order"
	if ($response.ExitCode -ne 0 -or -not $status.ownedByRequester) {
		Throw-Landing 2 'queue-lock.not-owned' "Queue '$Order' was acquired but is not owned by this transaction."
	}
}

function Test-QueueAbsent([string] $Order) {
	$response = Invoke-WorktreeCli @('plan', 'queue', 'status', '--repo', $result.identities.gitCommonDirectory, '--order', $Order, '--owner', $script:LandingOwner)
	$status = Get-JsonResponse $response "queue absence $Order"
	return $response.ExitCode -eq 2 -and -not $status.held
}

function Release-AcquiredQueueLocks {
	$allReleased = $true
	for ($index = $script:QueueLocks.Count - 1; $index -ge 0; --$index) {
		$lock = $script:QueueLocks[$index]
		if ($lock.Released) { continue }
		try {
			$statusResponse = Invoke-WorktreeCli @('plan', 'queue', 'status', '--repo', $result.identities.gitCommonDirectory, '--order', $lock.Order, '--owner', $script:LandingOwner)
			$status = Get-JsonResponse $statusResponse "queue status before release $($lock.Order)"
			if ($statusResponse.ExitCode -eq 2 -and -not $status.held) {
				$lock.Released = $true
				$lock.ReleaseResult = 'already-absent'
				continue
			}
			if ($statusResponse.ExitCode -ne 0 -or -not $status.ownedByRequester) {
				$lock.ReleaseResult = 'not-owner-or-unverifiable'
				$allReleased = $false
				$result.residuals.Add("Queue '$($lock.Order)' could not be owner-released.")
				continue
			}
			$unlockResponse = Invoke-WorktreeCli @('plan', 'queue', 'unlock', '--repo', $result.identities.gitCommonDirectory, '--order', $lock.Order, '--owner', $script:LandingOwner)
			if ($unlockResponse.ExitCode -ne 0 -or -not (Test-QueueAbsent $lock.Order)) {
				$lock.ReleaseResult = 'unlock-or-absence-check-failed'
				$allReleased = $false
				$result.residuals.Add("Queue '$($lock.Order)' was not proven absent after unlock.")
				continue
			}
			$lock.Released = $true
			$lock.ReleaseResult = 'released'
		}
		catch {
			$lock.ReleaseResult = 'release-error'
			$allReleased = $false
			$result.residuals.Add("Queue '$($lock.Order)' release error: $($_.Exception.Message)")
		}
	}
	$result.cleanup.queueReleaseComplete = $allReleased
	return $allReleased
}

function Release-LandingLockIfSafe {
	if (-not $script:LandingClaimed) { return }
	if (-not (Release-AcquiredQueueLocks)) { return }
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

function ConvertTo-OrderRelativePlanKey([string] $Order, [string] $Plan) {
	$normalizedOrder = $Order.Trim().Replace('\', '/')
	$normalizedPlan = $Plan.Trim().Replace('\', '/')
	while ($normalizedOrder.StartsWith('./', [StringComparison]::Ordinal)) { $normalizedOrder = $normalizedOrder.Substring(2) }
	while ($normalizedPlan.StartsWith('./', [StringComparison]::Ordinal)) { $normalizedPlan = $normalizedPlan.Substring(2) }
	if ([string]::IsNullOrWhiteSpace($normalizedOrder) -or [string]::IsNullOrWhiteSpace($normalizedPlan) -or
		$normalizedOrder -match '^[A-Za-z]:' -or $normalizedPlan -match '^[A-Za-z]:' -or
		$normalizedOrder.StartsWith('/', [StringComparison]::Ordinal) -or $normalizedPlan.StartsWith('/', [StringComparison]::Ordinal) -or
		$normalizedOrder -match '(^|/)(\.|\.\.)(/|$)' -or $normalizedPlan -match '(^|/)(\.|\.\.)(/|$)' -or
		$normalizedOrder.Contains('//') -or $normalizedPlan.Contains('//') -or $normalizedPlan.EndsWith('/', [StringComparison]::Ordinal)) {
		Throw-Landing 1 'input.plan-claim-invalid' 'PlanOrder and Plan must be normalized repository-relative paths.'
	}
	$orderDirectory = [IO.Path]::GetDirectoryName($normalizedOrder)
	if ([string]::IsNullOrWhiteSpace($orderDirectory)) {
		Throw-Landing 1 'input.plan-claim-invalid' 'PlanOrder must have a repository-relative parent directory.'
	}
	$orderDirectory = $orderDirectory.Replace('\', '/').TrimEnd('/')
	$prefix = $orderDirectory + '/'
	if ($normalizedPlan.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
		return $normalizedPlan.Substring($prefix.Length)
	}
	if ($normalizedPlan.StartsWith('Documents/', [StringComparison]::OrdinalIgnoreCase)) {
		Throw-Landing 1 'input.plan-claim-invalid' 'Plan is outside the selected PlanOrder directory.'
	}
	return $normalizedPlan
}

function Release-PlanRowClaim {
	if (-not $HasPlanRowClaim) { return }
	$response = Invoke-WorktreeCli @('plan', 'row', 'status', '--repo', $result.identities.gitCommonDirectory, '--order', $PlanOrder, '--plan', $script:PlanRowKey, '--owner', $SessionOwner)
	$status = Get-JsonResponse $response 'plan row status'
	if ($response.ExitCode -ne 0 -or -not $status.ownedByRequester) {
		Throw-Landing 2 'plan-row.not-owned' 'Final plan-row claim is absent or not owned by the session owner.'
	}
	$response = Invoke-WorktreeCli @('plan', 'row', 'unclaim', '--repo', $result.identities.gitCommonDirectory, '--order', $PlanOrder, '--plan', $script:PlanRowKey, '--owner', $SessionOwner)
	if ($response.ExitCode -ne 0) { Throw-Landing 2 'plan-row.unclaim-failed' 'Final plan-row unclaim failed.' }
	$response = Invoke-WorktreeCli @('plan', 'row', 'status', '--repo', $result.identities.gitCommonDirectory, '--order', $PlanOrder, '--plan', $script:PlanRowKey, '--owner', $SessionOwner)
	$status = Get-JsonResponse $response 'plan row absence'
	if ($response.ExitCode -ne 2 -or $status.held) { Throw-Landing 2 'plan-row.still-held' 'Final plan-row claim was not proven absent.' }
	$result.planRow.released = $true
}

function Get-FileSha256([string] $Path) {
	return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([IO.File]::ReadAllBytes($Path))).ToLowerInvariant()
}

function Assert-CompletedPlanReceipt([string] $TreeRoot) {
	if (-not $HasCompletedPlanClaim) { return }
	try { $receipt = $CompletedPlanReceipt | ConvertFrom-Json -Depth 16 -ErrorAction Stop }
	catch { Throw-Landing 1 'input.completed-receipt-invalid' 'CompletedPlanReceipt is not one WorktreeCli completion JSON object.' }
	if ($receipt.schemaVersion -ne 1 -or $receipt.operation -isnot [string] -or $receipt.plan -isnot [string] -or
		$receipt.claimOwner -isnot [string] -or $receipt.plansOrderSha256 -isnot [string] -or $receipt.featuresOrderSha256 -isnot [string]) {
		Throw-Landing 2 'completed-receipt.mismatch' 'Completed-plan receipt does not have the required WorktreeCli receipt fields.'
	}
	$expectedPlan = ((Split-Path -Parent $PlanOrder).Replace('\', '/').TrimEnd('/') + '/' + $script:PlanRowKey).ToLowerInvariant()
	if ($receipt.schemaVersion -ne 1 -or $receipt.operation -cne 'complete' -or -not $receipt.handled -or
		$receipt.plan.ToLowerInvariant() -cne $expectedPlan -or $receipt.claimOwner -cne $SessionOwner -or
		$receipt.plansOrderSha256 -cnotmatch '^[0-9a-f]{64}$' -or $receipt.featuresOrderSha256 -cnotmatch '^[0-9a-f]{64}$') {
		Throw-Landing 2 'completed-receipt.mismatch' 'Completed-plan receipt does not bind this owner, plan, and queue state.'
	}
	$plansOrder = Join-Path $TreeRoot 'Documents\Plans\Order.md'
	$featuresOrder = Join-Path $TreeRoot 'Documents\Features\Order.md'
	$completedPlan = Join-Path $TreeRoot ($expectedPlan.Replace('/', [IO.Path]::DirectorySeparatorChar))
	if ((Get-FileSha256 $plansOrder) -cne $receipt.plansOrderSha256 -or (Get-FileSha256 $featuresOrder) -cne $receipt.featuresOrderSha256 -or (Test-Path -LiteralPath $completedPlan)) {
		Throw-Landing 2 'completed-receipt.tree-mismatch' 'Landed primary queue bytes or completed-plan deletion differ from the verified receipt.'
	}
	$result.planRow.completedReceiptVerified = $true
}

function Write-LandingArtifact {
	$writer = Join-Path $PSScriptRoot '..\..\..\scripts\Write-AgentLandingArtifact.ps1'
	if (-not (Test-Path -LiteralPath $writer)) {
		$writer = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\Write-AgentLandingArtifact.ps1'
	}
	$client = if ([string]::IsNullOrWhiteSpace($env:BROKEN_ENGINE_AGENT_CLIENT)) { 'unknown' } else { $env:BROKEN_ENGINE_AGENT_CLIENT }
	$arguments = [Collections.Generic.List[string]]::new()
	foreach ($argument in @(
		'-NoProfile', '-File', $writer,
		'-Worktree', $script:CurrentIdentity.Worktree,
		'-Commit', $ApprovedSessionCommit,
		'-VerificationReportPath', $VerificationReportPath,
		'-VerificationReportSha256', $VerificationReportSha256,
		'-ManifestRange', ($ManifestRange -join ','),
		'-SessionOwner', $SessionOwner,
		'-SessionLabel', $SessionLabel,
		'-Client', $client
	)) { $arguments.Add($argument) }
	if ($HasCompletedPlanClaim) { $arguments.Add('-CompletedPlanReceipt'); $arguments.Add($CompletedPlanReceipt) }
	$response = Invoke-FinalizeNativeText 'pwsh.exe' $arguments.ToArray() $script:CurrentIdentity.Worktree
	if ($response.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($response.Stdout)) {
		Throw-Landing 2 'artifact.write-failed' "Landing artifact write failed: $($response.Stdout.Trim())$($response.Stderr.Trim())"
	}
	try { $artifact = $response.Stdout.Trim() | ConvertFrom-Json -Depth 16 -ErrorAction Stop }
	catch { Throw-Landing 2 'artifact.invalid-json' 'Landing artifact writer returned invalid JSON.' }
	if ($artifact.status -notin @('written', 'exists') -or $artifact.commit -cne $ApprovedSessionCommit) {
		Throw-Landing 2 'artifact.invalid-result' 'Landing artifact writer did not bind the landed commit.'
	}
	$result.artifact.status = $artifact.status
	$result.artifact.path = $artifact.artifactPath
	$result.artifact.sha256 = $artifact.sha256
	$result.artifact.transcriptStatus = $artifact.transcriptStatus
}

function Complete-LandedState {
	Assert-CompletedPlanReceipt $script:PrimaryIdentity.Worktree
	Release-PlanRowClaim
	$registration = Test-FinalizeWorktreeRegistration $script:PrimaryIdentity.Worktree $script:CurrentIdentity.Worktree $CurrentBranch $ApprovedSessionCommit
	if (-not $registration.Registered) { Throw-Landing 2 'session.registration-invalid' $registration.Message }
	if ((Invoke-FinalizeGit $script:CurrentIdentity.Worktree @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) {
		Throw-Landing 2 'session.dirty' 'Session worktree is dirty after landing.'
	}
	if (-not (Test-FinalizeGitSuccess $script:PrimaryIdentity.Worktree @('merge-base', '--is-ancestor', $ApprovedSessionCommit, (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', 'HEAD')).Trim()))) {
		Throw-Landing 2 'session.not-contained' 'Session tip is not contained in primary after landing.'
	}
	$result.status = 'landed'
	$result.code = 'ok'
	$result.message = if ($ArtifactOnly) { 'Landing artifact was recorded and all retained finalization claims were released.' } else { 'Primary advanced and all owner-held finalization claims were released.' }
}

try {
	if ($ApprovedSessionCommit -cnotmatch '^[0-9a-f]{40}$' -or $ExpectedCurrentTip -cnotmatch '^[0-9a-f]{40}$' -or $ExpectedPrimaryTip -cnotmatch '^[0-9a-f]{40}$') {
		Throw-Landing 1 'input.commit-invalid' 'Approved and expected commits must be lowercase 40-character object IDs.'
	}
	if ($ValidateOnly -and $ArtifactOnly) { Throw-Landing 1 'input.mode-conflict' 'ValidateOnly and ArtifactOnly are mutually exclusive.' }
	if ($HasCompletedPlanClaim -and -not $HasPlanRowClaim) { Throw-Landing 1 'input.completed-claim-invalid' 'HasCompletedPlanClaim requires HasPlanRowClaim.' }
	if ($HasCompletedPlanClaim -and [string]::IsNullOrWhiteSpace($CompletedPlanReceipt)) { Throw-Landing 1 'input.completed-receipt-required' 'HasCompletedPlanClaim requires CompletedPlanReceipt.' }
	if (-not [string]::IsNullOrWhiteSpace($CompletedPlanReceipt) -and -not $CompletedPlanReceipt.TrimStart().StartsWith('{', [StringComparison]::Ordinal)) {
		$receiptPath = Get-FinalizeRootPreservingFullPath $CompletedPlanReceipt
		if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) {
			Throw-Landing 1 'input.completed-receipt-path' "CompletedPlanReceipt is neither inline JSON nor an existing receipt file: '$receiptPath'."
		}
		try { $CompletedPlanReceipt = [Text.UTF8Encoding]::new($false, $true).GetString([IO.File]::ReadAllBytes($receiptPath)) }
		catch { Throw-Landing 1 'input.completed-receipt-path' "CompletedPlanReceipt file is not strict UTF-8 text: '$receiptPath'." }
	}
	if ($HasPlanRowClaim -and ([string]::IsNullOrWhiteSpace($PlanOrder) -or [string]::IsNullOrWhiteSpace($Plan))) {
		Throw-Landing 1 'input.plan-claim-invalid' 'Plan row release requires PlanOrder and Plan.'
	}
	if ($HasPlanRowClaim) {
		$script:PlanRowKey = ConvertTo-OrderRelativePlanKey $PlanOrder $Plan
		$result.planRow.plan = $script:PlanRowKey
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

	$manifestRows = @(Get-FinalizeManifestRows $script:CurrentIdentity.Worktree $ManifestComparisonBase)
	$queueChanging = Test-QueueChangingManifest $manifestRows
	$result.locks.queueChanging = $queueChanging
	if ($ValidateOnly) {
		Assert-PrimaryAdvanceState
		Assert-CompletedPlanReceipt $script:CurrentIdentity.Worktree
		$writer = Join-Path $PSScriptRoot '..\..\..\scripts\Write-AgentLandingArtifact.ps1'
		if (-not (Test-Path -LiteralPath $writer)) {
			$writer = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\Write-AgentLandingArtifact.ps1'
		}
		if (-not (Test-Path -LiteralPath $writer -PathType Leaf)) { Throw-Landing 2 'artifact.writer-missing' 'Landing artifact writer script was not found.' }
		$storeModule = Join-Path (Split-Path -Parent $writer) 'AgentArtifactStore.psm1'
		Import-Module $storeModule -Force -DisableNameChecking
		$result.artifact.path = Get-AgentLandingArtifactPath -Worktree $script:CurrentIdentity.Worktree -Commit $ApprovedSessionCommit
		$preflight = Invoke-Preflight 'pre-mutation' $ExpectedCurrentTip $ExpectedPrimaryTip $queueChanging
		if ($preflight.tips.current -cne $ApprovedSessionCommit) { Throw-Landing 2 'approval.session-tip-changed' 'Session tip is not the explicit user-approved commit.' }
		$result.identities.currentWorktree = [string] $preflight.identities.currentWorktree
		$result.identities.primaryWorktree = [string] $preflight.identities.primaryWorktree
		$result.identities.gitCommonDirectory = [string] $preflight.identities.gitCommonDirectory
		if ($HasPlanRowClaim) {
			$response = Invoke-FinalizeNativeText ([string] $preflight.worktreeCli.path) @('plan', 'row', 'status', '--repo', $result.identities.gitCommonDirectory, '--order', $PlanOrder, '--plan', $script:PlanRowKey, '--owner', $SessionOwner) $script:CurrentIdentity.Worktree
			$status = Get-JsonResponse $response 'plan row status'
			if ($response.ExitCode -ne 0 -or -not $status.ownedByRequester) {
				Throw-Landing 2 'plan-row.not-owned' 'Plan-row claim is absent or not owned by the session owner.'
			}
		}
		$result.status = 'validated'
		$result.code = 'ok'
		$result.message = 'All landing inputs and bindings validated; no lock was claimed and nothing was mutated.'
	}
	elseif ($ArtifactOnly) {
		if ($ExpectedCurrentTip -cne $ApprovedSessionCommit -or $ExpectedPrimaryTip -cne $ApprovedSessionCommit) {
			Throw-Landing 1 'artifact.retry-input-mismatch' 'Artifact retry requires ExpectedCurrentTip and ExpectedPrimaryTip to equal the landed commit.'
		}
		$preflight = Invoke-Preflight 'post-mutation' $ApprovedSessionCommit $ApprovedSessionCommit $queueChanging
		if ($preflight.tips.current -cne $ApprovedSessionCommit -or $preflight.tips.primary -cne $ApprovedSessionCommit) {
			Throw-Landing 2 'artifact.retry-tip-mismatch' 'Artifact retry requires the session and primary tips to equal the landed commit.'
		}
		$script:WorktreeCliPath = [string] $preflight.worktreeCli.path
		$result.identities.currentWorktree = [string] $preflight.identities.currentWorktree
		$result.identities.primaryWorktree = [string] $preflight.identities.primaryWorktree
		$result.identities.gitCommonDirectory = [string] $preflight.identities.gitCommonDirectory
		$result.primaryAdvanced = $true
		$result.tips.current = $ApprovedSessionCommit
		$result.tips.primary = $ApprovedSessionCommit
		Write-LandingArtifact
		Complete-LandedState
	}
	else {
		$preflight = Invoke-Preflight 'pre-mutation' $ExpectedCurrentTip $ExpectedPrimaryTip $queueChanging
		if ($preflight.tips.current -cne $ApprovedSessionCommit) { Throw-Landing 2 'approval.session-tip-changed' 'Session tip is not the explicit user-approved commit.' }
		$script:WorktreeCliPath = [string] $preflight.worktreeCli.path
		$result.identities.currentWorktree = [string] $preflight.identities.currentWorktree
		$result.identities.primaryWorktree = [string] $preflight.identities.primaryWorktree
		$result.identities.gitCommonDirectory = [string] $preflight.identities.gitCommonDirectory

		$tokenResponse = Invoke-WorktreeCli @('lock', 'token')
		if ($tokenResponse.ExitCode -ne 0 -or $tokenResponse.Stdout.Trim() -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') {
			Throw-Landing 1 'landing-lock.token-failed' 'WorktreeCli could not generate a canonical landing owner token.'
		}
		$script:LandingOwner = $tokenResponse.Stdout.Trim()
		$result.locks.landingOwner = $script:LandingOwner
		$claimResponse = Invoke-WorktreeCli @('lock', 'claim', '--repo', $result.identities.gitCommonDirectory, '--owner', $script:LandingOwner, '--session', $SessionLabel, '--worktree', $script:CurrentIdentity.Worktree, '--lease-seconds', '3600')
		if ($claimResponse.ExitCode -ne 0) { Throw-Landing 2 'landing-lock.claim-failed' "Landing lock claim failed: $($claimResponse.Stdout.Trim())$($claimResponse.Stderr.Trim())" }
		$script:LandingClaimed = $true
		$result.locks.landingClaimed = $true
		Assert-LandingOwner

		if ($queueChanging) {
			Acquire-QueueLock 'Documents/Features/Order.md'
			Acquire-QueueLock 'Documents/Plans/Order.md'
		}
		Assert-PrimaryAdvanceState
		$null = Invoke-Preflight 'pre-mutation' $ExpectedCurrentTip $ExpectedPrimaryTip $queueChanging
		Refresh-LandingOwner
		$rebase = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:PrimaryIdentity.Worktree, 'rebase', $ApprovedSessionCommit) $script:PrimaryIdentity.Worktree
		if ($rebase.ExitCode -ne 0) { Throw-Landing 1 'git.primary-rebase-failed' "Primary rebase failed: $($rebase.Stdout)$($rebase.Stderr)" }
		$result.primaryAdvanced = $true
		$result.tips.current = $ApprovedSessionCommit
		$result.tips.primary = $ApprovedSessionCommit
		Refresh-LandingOwner
		$null = Invoke-Preflight 'post-mutation' $ApprovedSessionCommit $ApprovedSessionCommit $queueChanging
		Write-LandingArtifact
		if (-not (Release-AcquiredQueueLocks)) { Throw-Landing 2 'queue-lock.release-failed' 'One or more acquired queue locks could not be proven released.' }
		Release-LandingLockIfSafe
		if ($script:LandingClaimed) { Throw-Landing 2 'landing-lock.release-failed' 'Landing lock could not be released after the primary advance.' }
		Complete-LandedState
	}
}
catch {
	$script:FailureExitCode = if ($_.Exception.Data.Contains('FinalizeExitCode')) { [int] $_.Exception.Data['FinalizeExitCode'] } else { 1 }
	$script:FailureCode = if ($_.Exception.Data.Contains('FinalizeCode')) { [string] $_.Exception.Data['FinalizeCode'] } else { 'internal.error' }
	$script:FailureMessage = $_.Exception.Message
}
finally {
	Release-AcquiredQueueLocks | Out-Null
	Release-LandingLockIfSafe
}

if ($script:FailureExitCode -ne 0) {
	$result.status = if ($script:FailureExitCode -eq 2) { 'blocked' } else { 'error' }
	$result.code = $script:FailureCode
	$result.message = $script:FailureMessage
}
if ($script:LandingClaimed -or -not $result.cleanup.queueReleaseComplete) {
	if ($result.status -eq 'landed') {
		$result.status = 'blocked'
		$result.code = 'cleanup.incomplete'
		$result.message = 'Landing completed but required lock cleanup was not proven complete.'
	}
	if ($result.status -eq 'error') { $result.code = 'cleanup.' + $result.code }
}
[Console]::Out.Write(($result | ConvertTo-Json -Depth 12 -Compress))
exit $(if ($result.status -in @('landed', 'validated')) { 0 } elseif ($result.status -eq 'blocked') { 2 } else { 1 })
