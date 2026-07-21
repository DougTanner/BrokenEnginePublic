# Exclusive owner of the post-approval session-landing transaction: every lock,
# Git, cleanup, queue-publication, and row-release action. Callers pass the exact
# approval-covered session commit, the current/primary identities and expected
# tips, wrapper owner/session label, any retained row-claim locator, and any
# post-landing plan-order add-request paths. None of these operations may be
# assembled as inline PowerShell by a caller.
#
# Transaction: runs the single structural pre-mutation preflight, derives the
# canonical Git common directory, claims the PC-global landing lock, refreshes
# owner-only leases around every mutation, proves primary is an ancestor with no
# commits to replay (`git merge-base --is-ancestor`, empty rev-lists, no
# multi-parent commits), advances primary with `git rebase <approved-session-commit>`
# (a pure fast-forward ref advance), and conditionally releases the landing lock.
# The machine-local plan queue never appears in the session diff, so this
# transaction takes no Plans/Features queue locks — WorktreeCli takes them
# internally. After the advance it publishes the queue: `plan order complete`
# against primary (the plan file is already deleted in the landed tree),
# `plan order add --worktree <session>` for each supplied add-request (the session
# tip equals the landed commit, so plan files and Temp/ requests resolve there),
# then `plan order validate` against primary whenever a complete or add ran, and
# finally releases the retained row claim. Every publication step is safe to rerun
# after a crash. Reinvoke this sidecar with the same approval-bound inputs when
# the session remains at ApprovedSessionCommit and primary contains it; its
# post-advance recovery path revalidates request hashes, resumes idempotent
# publication, and releases the row claim without replaying the primary advance.
#
# Universal acquired-lock safe-stop rule (owned here): on any cancellation,
# blocker, or failure, the landing claim follows the clear-worktree
# release/active-retain rule — visited on the normal path and in the finally
# block, owner-checked, released only while still owned, and retained and reported
# when it cannot be proven released.
#
# Result contract: broken-engine-finalize-landing/v1 JSON; exit 0 before the
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
	[switch] $HasPlanRowClaim,
	[string] $PlanOrder,
	[string] $Plan,
	[Parameter(Mandatory)][ValidateSet('none', 'list')][string] $PlanAddRequestDisposition,
	[string[]] $PlanAddRequestPaths,
	[string[]] $PlanAddRequestSha256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$planAddRequestPathsBound = $PSBoundParameters.ContainsKey('PlanAddRequestPaths')
$planAddRequestSha256Bound = $PSBoundParameters.ContainsKey('PlanAddRequestSha256')

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
	locks = [ordered]@{ landingOwner = $null; landingClaimed = $false; landingReleased = $false }
	planRow = [ordered]@{ requested = $HasPlanRowClaim.IsPresent; released = $false; order = $PlanOrder; plan = $Plan }
	queuePublication = [ordered]@{ requestDisposition = $PlanAddRequestDisposition; requests = @(); completed = $false; complete = $null; added = [Collections.Generic.List[object]]::new(); validated = $false }
	cleanup = [ordered]@{ worktreesClear = $null; worktreeProblems = @() }
	residuals = [Collections.Generic.List[string]]::new()
}
$script:WorktreeCliPath = $null
$script:LandingOwner = $null
$script:LandingClaimed = $false
$script:PrimaryIdentity = $null
$script:CurrentIdentity = $null
$script:PlanRowKey = $null
$script:CanonicalPlanKey = $null
$script:FailureExitCode = 0
$script:FailureCode = $null
$script:FailureMessage = $null
$script:CertifiedPlanAddRequests = @()
$script:PlanCompletionTerminalProven = $false

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

function New-PlanAddRequestItemsJson {
	$paths = @()
	$hashes = @()
	if ($planAddRequestPathsBound) { $paths = @($PlanAddRequestPaths) }
	if ($planAddRequestSha256Bound) { $hashes = @($PlanAddRequestSha256) }
	if ($PlanAddRequestDisposition -ceq 'none') {
		if ($paths.Count -ne 0 -or $hashes.Count -ne 0) { Throw-Landing 1 'input.plan-add-request-invalid' 'PlanAddRequestDisposition none forbids request paths and hashes.' }
		return $null
	}
	if ($paths.Count -eq 0 -or $hashes.Count -ne $paths.Count) { Throw-Landing 1 'input.plan-add-request-invalid' 'PlanAddRequestDisposition list requires one SHA-256 per request path.' }
	$items = [Collections.Generic.List[object]]::new()
	for ($index = 0; $index -lt $paths.Count; ++$index) {
		$path = $paths[$index]
		$sha256 = $hashes[$index]
		if ([string]::IsNullOrWhiteSpace($path) -or [string]::IsNullOrWhiteSpace($sha256)) { Throw-Landing 1 'input.plan-add-request-invalid' "Plan-add request path and SHA-256 at index $index must not be null or blank." }
		if ($sha256 -cnotmatch '^[0-9a-f]{64}$') { Throw-Landing 1 'input.plan-add-request-invalid' "Plan-add request SHA-256 at index $index must be 64 lowercase hexadecimal characters." }
		$items.Add([ordered]@{ path = $path; sha256 = $sha256 })
	}
	return ConvertTo-Json -InputObject $items.ToArray() -Depth 4 -Compress
}

function Assert-PlanAddRequestResult($PreflightResult) {
	if ($null -eq $PreflightResult.planAddRequests -or $PreflightResult.planAddRequests.disposition -cne $PlanAddRequestDisposition) {
		Throw-Landing 1 'preflight.request-disposition-invalid' 'Finalization preflight returned a different plan-add request disposition.'
	}
	$paths = @()
	$hashes = @()
	if ($planAddRequestPathsBound) { $paths = @($PlanAddRequestPaths) }
	if ($planAddRequestSha256Bound) { $hashes = @($PlanAddRequestSha256) }
	$items = @($PreflightResult.planAddRequests.items)
	if ($items.Count -ne $paths.Count) { Throw-Landing 1 'preflight.request-count-invalid' 'Finalization preflight returned a different plan-add request count.' }
	for ($index = 0; $index -lt $items.Count; ++$index) {
		$item = $items[$index]
		if ($null -eq $item) { Throw-Landing 1 'preflight.request-identity-invalid' "Finalization preflight returned a null plan-add request at index $index." }
		$properties = @($item.PSObject.Properties.Name)
		if (@('suppliedPath', 'repositoryRelativePath', 'identity', 'sha256') | Where-Object { $properties -cnotcontains $_ }) {
			Throw-Landing 1 'preflight.request-identity-invalid' "Finalization preflight omitted plan-add request identity fields at index $index."
		}
		if ([string]::IsNullOrWhiteSpace([string]$item.suppliedPath) -or
			[string]::IsNullOrWhiteSpace([string]$item.repositoryRelativePath) -or
			[string]::IsNullOrWhiteSpace([string]$item.identity) -or
			[string]$item.suppliedPath -cne $paths[$index] -or [string]$item.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
			[string]$item.sha256 -cne $hashes[$index]) {
			Throw-Landing 1 'preflight.request-identity-invalid' "Finalization preflight returned an invalid, reordered, or re-paired plan-add request at index $index."
		}
	}
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
	if ($HasPlanRowClaim) { $arguments.Add('-HasPlanRowClaim') }
	foreach ($argument in @('-PlanAddRequestDisposition', $PlanAddRequestDisposition)) { $arguments.Add($argument) }
	$requestItemsJson = New-PlanAddRequestItemsJson
	if ($null -ne $requestItemsJson) {
		foreach ($argument in @('-PlanAddRequestItemsJson', $requestItemsJson)) { $arguments.Add($argument) }
	}
	$response = Invoke-FinalizeNativeText 'pwsh.exe' $arguments.ToArray() $script:CurrentIdentity.Worktree
	$preflightResult = Get-JsonResponse $response "finalization preflight $Checkpoint"
	if ($response.ExitCode -ne 0 -or $preflightResult.status -cne 'pass' -or $preflightResult.code -cne 'ok') {
		$exitCode = if ($response.ExitCode -eq 2) { 2 } else { 1 }
		Throw-Landing $exitCode "preflight.$($preflightResult.code)" "$Checkpoint preflight failed: $($preflightResult.message)"
	}
	Assert-PlanAddRequestResult $preflightResult
	return $preflightResult
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

# Accepts the plan in either form (canonical repository-relative or order-relative) and returns both keys:
# Relative for `plan row` commands, Canonical for `plan order complete` (WorktreeCli rejects an order-relative
# key there).
function Resolve-PlanKeys([string] $Order, [string] $Plan) {
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
		return @{ Relative = $normalizedPlan.Substring($prefix.Length); Canonical = $normalizedPlan }
	}
	if ($normalizedPlan.StartsWith('Documents/', [StringComparison]::OrdinalIgnoreCase)) {
		Throw-Landing 1 'input.plan-claim-invalid' 'Plan is outside the selected PlanOrder directory.'
	}
	return @{ Relative = $normalizedPlan; Canonical = $prefix + $normalizedPlan }
}

function Resolve-PlanRowStatus([int] $ExitCode, $Status) {
	$properties = @($Status.PSObject.Properties.Name)
	if ($ExitCode -eq 0) {
		if (-not ($properties -ccontains 'ownedByRequester') -or $Status.ownedByRequester -isnot [bool]) {
			Throw-Landing 2 'plan-row.status-failed' 'Final plan-row claim state is unreadable or invalid.'
		}
		if ($Status.ownedByRequester) { return 'owned' }
		Throw-Landing 2 'plan-row.not-owned' 'Final plan-row claim is owned by another session.'
	}
	if ($ExitCode -eq 2) {
		if ($properties -ccontains 'held' -and $Status.held -is [bool] -and $Status.held -eq $false) { return 'absent' }
		Throw-Landing 2 'plan-row.status-failed' 'Final plan-row claim state is unreadable or invalid.'
	}
	Throw-Landing 1 'plan-row.status-failed' 'Final plan-row claim state is unreadable or invalid.'
}

function Get-PlanRowStatus {
	$response = Invoke-WorktreeCli @('plan', 'row', 'status', '--repo', $result.identities.gitCommonDirectory, '--order', $PlanOrder, '--plan', $script:PlanRowKey, '--owner', $SessionOwner)
	$status = Get-JsonResponse $response 'plan row status'
	return [pscustomobject]@{ State = Resolve-PlanRowStatus $response.ExitCode $status }
}

function Test-PlanCompletionTerminal {
	$rowStatus = Get-PlanRowStatus
	if ($rowStatus.State -ceq 'owned') { return $false }

	$response = Invoke-WorktreeCli @('plan', 'order', 'validate', '--repo', $result.identities.gitCommonDirectory, '--worktree', $script:PrimaryIdentity.Worktree)
	$validation = Get-JsonResponse $response 'terminal plan completion validation'
	if ($response.ExitCode -ne 0 -or -not $validation.ok) {
		Throw-Landing 2 'queue.complete-terminal-unproven' 'Absent plan-row claim does not have a valid terminal queue state.'
	}
	$targetRows = @($validation.rows | Where-Object { $_.plan -ceq $script:CanonicalPlanKey })
	if ($targetRows.Count -ne 0) {
		Throw-Landing 2 'queue.complete-terminal-unproven' 'Absent plan-row claim still has a live queue row.'
	}
	$primaryPlanPath = Join-Path $script:PrimaryIdentity.Worktree $script:CanonicalPlanKey
	if (Test-Path -LiteralPath $primaryPlanPath) {
		Throw-Landing 2 'queue.complete-terminal-unproven' 'Absent plan-row claim still has a primary plan file.'
	}
	if ((Get-PlanRowStatus).State -cne 'absent') {
		Throw-Landing 2 'queue.complete-terminal-unproven' 'Plan-row claim changed while proving terminal completion.'
	}

	$script:PlanCompletionTerminalProven = $true
	$result.queuePublication.complete = [ordered]@{
		schemaVersion = 1
		operation = 'complete'
		handled = $true
		alreadyTerminal = $true
		plan = $script:CanonicalPlanKey
	}
	$result.queuePublication.completed = $true
	$result.planRow.released = $true
	return $true
}

function Release-PlanRowClaim {
	if (-not $HasPlanRowClaim) { return }
	if ($script:PlanCompletionTerminalProven) { return }
	if ((Get-PlanRowStatus).State -cne 'owned') { Throw-Landing 2 'plan-row.not-owned' 'Final plan-row claim is absent or not owned by the session owner.' }
	$response = Invoke-WorktreeCli @('plan', 'row', 'unclaim', '--repo', $result.identities.gitCommonDirectory, '--order', $PlanOrder, '--plan', $script:PlanRowKey, '--owner', $SessionOwner)
	if ($response.ExitCode -ne 0) { Throw-Landing 2 'plan-row.unclaim-failed' 'Final plan-row unclaim failed.' }
	if ((Get-PlanRowStatus).State -cne 'absent') { Throw-Landing 2 'plan-row.still-held' 'Final plan-row claim was not proven absent.' }
	$result.planRow.released = $true
}

function Complete-LandedState {
	$publishRan = $false
	if ($HasPlanRowClaim) {
		if (-not (Test-PlanCompletionTerminal)) {
			$response = Invoke-WorktreeCli @('plan', 'order', 'complete', '--repo', $result.identities.gitCommonDirectory, '--worktree', $script:PrimaryIdentity.Worktree, '--owner', $SessionOwner, '--session', $SessionLabel, '--plan', $script:CanonicalPlanKey)
			$completion = Get-JsonResponse $response 'post-landing plan order complete'
			$result.queuePublication.complete = $completion
			if ($response.ExitCode -ne 0 -and -not ($completion.PSObject.Properties.Name -ccontains 'handled' -and $completion.handled)) {
				Throw-Landing $(if ($response.ExitCode -eq 2) { 2 } else { 1 }) 'queue.complete-failed' "Post-landing plan order complete failed: $($response.Stdout.Trim())$($response.Stderr.Trim())"
			}
			$result.queuePublication.completed = $true
		}
		$publishRan = $true
	}
	foreach ($requestItem in @($script:CertifiedPlanAddRequests)) {
		$request = [string]$requestItem.repositoryRelativePath
		$requestSha256 = [string]$requestItem.sha256
		if ([string]::IsNullOrWhiteSpace($request) -or $requestSha256 -cnotmatch '^[0-9a-f]{64}$') { Throw-Landing 1 'queue.certification-invalid' 'Certified plan-add request is incomplete before publication.' }
		$response = Invoke-WorktreeCli @('plan', 'order', 'add', '--repo', $result.identities.gitCommonDirectory, '--worktree', $script:CurrentIdentity.Worktree, '--owner', $SessionOwner, '--session', $SessionLabel, '--request', $request, '--request-sha256', $requestSha256)
		$added = Get-JsonResponse $response "post-landing plan order add $request"
		$result.queuePublication.added.Add($added)
		if ($response.ExitCode -ne 0 -and -not ($added.PSObject.Properties.Name -ccontains 'handled' -and $added.handled)) {
			Throw-Landing $(if ($response.ExitCode -eq 2) { 2 } else { 1 }) 'queue.add-failed' "Post-landing plan order add '$request' failed: $($response.Stdout.Trim())$($response.Stderr.Trim())"
		}
		$publishRan = $true
	}
	if ($publishRan) {
		$response = Invoke-WorktreeCli @('plan', 'order', 'validate', '--repo', $result.identities.gitCommonDirectory, '--worktree', $script:PrimaryIdentity.Worktree)
		$validation = Get-JsonResponse $response 'landed queue validation'
		if ($response.ExitCode -ne 0 -or -not $validation.ok) {
			Throw-Landing 2 'queue.landed-validation-failed' 'Landed primary queue validation failed.'
		}
		$result.queuePublication.validated = $true
	}
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
	$result.message = 'Primary advanced and all owner-held finalization claims were released.'
}

try {
	if ($ApprovedSessionCommit -cnotmatch '^[0-9a-f]{40}$' -or $ExpectedCurrentTip -cnotmatch '^[0-9a-f]{40}$' -or $ExpectedPrimaryTip -cnotmatch '^[0-9a-f]{40}$') {
		Throw-Landing 1 'input.commit-invalid' 'Approved and expected commits must be lowercase 40-character object IDs.'
	}
	if ($HasPlanRowClaim -and ([string]::IsNullOrWhiteSpace($PlanOrder) -or [string]::IsNullOrWhiteSpace($Plan))) {
		Throw-Landing 1 'input.plan-claim-invalid' 'Plan row release requires PlanOrder and Plan.'
	}
	if ($HasPlanRowClaim) {
		$planKeys = Resolve-PlanKeys $PlanOrder $Plan
		$script:PlanRowKey = $planKeys.Relative
		$script:CanonicalPlanKey = $planKeys.Canonical
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
	if ($script:CurrentIdentity.Head -ceq $ApprovedSessionCommit -and
		(Test-FinalizeGitSuccess $script:PrimaryIdentity.Worktree @('merge-base', '--is-ancestor', $ApprovedSessionCommit, $script:PrimaryIdentity.Head))) {
		$preflight = Invoke-Preflight 'post-advance-recovery' $ApprovedSessionCommit $script:PrimaryIdentity.Head
		$script:WorktreeCliPath = [string]$preflight.worktreeCli.path
		$result.queuePublication.requests = @($preflight.planAddRequests.items)
		$script:CertifiedPlanAddRequests = @($preflight.planAddRequests.items)
		$result.primaryAdvanced = $true
		Complete-LandedState
		[Console]::Out.Write(($result | ConvertTo-Json -Depth 12 -Compress))
		exit 0
	}

	$preflight = Invoke-Preflight 'pre-mutation' $ExpectedCurrentTip $ExpectedPrimaryTip
	if ($preflight.tips.current -cne $ApprovedSessionCommit) { Throw-Landing 2 'approval.session-tip-changed' 'Session tip is not the explicit user-approved commit.' }
	$script:WorktreeCliPath = [string] $preflight.worktreeCli.path
	$result.queuePublication.requests = @($preflight.planAddRequests.items)
	$script:CertifiedPlanAddRequests = @($preflight.planAddRequests.items)
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
}
finally {
	Release-LandingLockIfSafe
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
