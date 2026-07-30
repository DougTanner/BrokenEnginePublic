[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $RepositoryRoot,
	[Parameter(Mandatory)][string] $Worktree,
	[Parameter(Mandatory)][string] $WorktreeCliExecutable
)

# Re-parents a wrapper worktree onto a squashed primary tip so a session can resume after the user
# rewrites primary history. The immutable receipt baseline records only where the worktree was CREATED; a
# pre-claim primary advance or a manual fast-forward can move the branch's true fork point to a
# newer primary tip without rewriting it, so the fork point is recovered from the primary target-branch
# reflog (the newest past tip the session branch still contains). `git rebase --onto <newTip> <forkBound>
# <branch>` then replays only the commits above that fork point (the session's own work) - rebasing from the
# stale receipt baseline would replay squashed-away primary commits pulled in by the advance. WorktreeCli
# then moves the live claim (and its Temp receipts) onto the new tip and the receipt baseline is rewritten
# to match. The single result JSON is the sidecar's only stdout; all sub-process output is captured so it
# cannot corrupt that object.
#
# Order is start-rebase -> reparent-claims -> rewrite-receipt: the receipt rewrite is last so a crash
# before it leaves the durable baseline unchanged and a rerun re-detects and finishes. Rebase conflicts
# are expected and are not aborted (user decision); status is 'reparented-conflict' for both kinds. A
# mid-rebase conflict (rebaseInProgress true) leaves a detached HEAD that already descends from the new
# tip, so the claim/receipt still reparent and the resumed session resolves then `git rebase --continue`.
# An autostash pop-conflict after a completed rebase (rebaseInProgress false) exits 0 with the stash kept
# and HEAD attached, so the session resolves then `git stash drop`. The claim/receipt reparent in both.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Newest primary target-branch reflog entries hashed when recovering the fork bound. The fork point is a
# recent tip (the last pre-claim advance / fast-forward before the squash), so this is generous; the read
# is one git call and the entries only feed a HashSet, so a large bound costs nothing per reattach.
$RepairReflogScanLimit = 1000

Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'AgentWorktreeSession.psm1') -Force -DisableNameChecking

# A state-conflict is a recoverable precondition mismatch (wrong branch, unregistered worktree, an
# unrelated git operation, a baseline that no longer parents the worktree) and exits 2; a hard failure
# (rebase or CLI error) throws and exits 1. The distinguishing flag rides on the exception's Data bag.
function New-RepairStateConflict([string] $Message) {
	$exception = [System.InvalidOperationException]::new($Message)
	$exception.Data['RepairStateConflict'] = $true
	return $exception
}

function Complete-Repair([string] $Status, [string] $OldBaseline, [string] $NewBaseline, [object] $ReparentedClaims, [object] $UpdatedReceipts, [object] $ConflictFiles, [Nullable[bool]] $RebaseInProgress = $null, [string] $ForkBound = $null) {
	$result = [ordered]@{
		schemaVersion = 'broken-engine-baseline-reparent/v1'
		status = $Status
		oldBaseline = $OldBaseline
		newBaseline = $NewBaseline
		reparentedClaims = @($ReparentedClaims)
	}
	# oldBaseline stays the immutable receipt baseline for schema stability; forkBound is the true fork point
	# the rebase replayed from (>= oldBaseline after a pre-claim advance), present whenever the sidecar derived
	# it. conflictFiles and rebaseInProgress are present only on a reparented-conflict so callers can
	# distinguish an empty resolved list from "no conflict happened" and pick the recovery banner.
	# rebaseInProgress true is a mid-rebase conflict (resolve then `git rebase --continue`, no scheduler ops);
	# false is a stash-apply conflict after a completed rebase (resolve then `git stash drop`, HEAD attached so
	# scheduler ops are safe).
	if (-not [string]::IsNullOrEmpty($ForkBound)) { $result.forkBound = $ForkBound }
	if ($null -ne $ConflictFiles) { $result.conflictFiles = @($ConflictFiles) }
	if ($null -ne $RebaseInProgress) { $result.rebaseInProgress = [bool]$RebaseInProgress }
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 100 -Compress))
	exit 0
}

# Local mirrors of the un-exported AgentWorktreeSession git helpers (the module exports only the receipt
# and proof surface). Kept byte-parallel so the sidecar validates the same shapes the reattach proof does.
function Get-RepairGitValue([string] $Path, [string[]] $Arguments, [string] $Description) {
	$value = @(Invoke-AgentGit (@('-C', $Path) + $Arguments))
	if ($value.Count -ne 1 -or [string]::IsNullOrWhiteSpace($value[0])) { throw "Git returned no unique $Description for '$Path'." }
	return $value[0].Trim()
}

function Get-RepairGitPath([string] $Path, [string] $Marker) {
	return Get-RepairGitValue $Path @('rev-parse', '--path-format=absolute', '--git-path', $Marker) "Git path for '$Marker'"
}

function Test-RepairAncestor([string] $Repo, [string] $Ancestor, [string] $Descendant) {
	& git -C $Repo merge-base --is-ancestor $Ancestor $Descendant *> $null
	return $LASTEXITCODE -eq 0
}

# The branch's true fork point on the primary target branch. The receipt baseline is only where the worktree
# was created; a pre-claim advance or a manual fast-forward may have moved the branch onto a newer primary
# tip without rewriting the receipt. The primary target-branch reflog still records every commit that was
# once its tip after a squash, so intersect it with the commits the branch carries above the receipt
# baseline (`rev-list HEAD ^baseline`, closest-to-HEAD first): the newest such commit that was a primary tip
# is the fork point a fast-forward put there. Commits below the baseline - including a squash's own
# `reset --soft <base>` reflog entry - are excluded by `^baseline`, and the squashed tip is not an ancestor
# of HEAD so it never appears; when nothing intersects, the fork point is the receipt baseline itself. The
# caller derives this only after confirming the baseline parents the worktree HEAD, keeping the rev-list
# bounded to the branch's own post-baseline history.
function Get-RepairForkBound([object] $Primary, [object] $Receipt, [string] $WorktreeHead) {
	$reflogTips = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($entry in @(Invoke-AgentGit @('-C', $Primary.Root, 'reflog', 'show', $Receipt.targetBranch, '--format=%H', "--max-count=$RepairReflogScanLimit"))) {
		$commit = $entry.Trim()
		if ($commit -cmatch '^[0-9a-f]{40}$') { [void]$reflogTips.Add($commit) }
	}
	foreach ($entry in @(Invoke-AgentGit @('-C', $Primary.Root, 'rev-list', '--topo-order', $WorktreeHead, "^$($Receipt.baseline)"))) {
		$commit = $entry.Trim()
		if ($commit -cmatch '^[0-9a-f]{40}$' -and $reflogTips.Contains($commit)) { return $commit }
	}
	return $Receipt.baseline
}

# The tip a prior run's completed re-parent rebase landed the worktree HEAD on, or $null. After
# `rebase --onto <newTip> <forkBound>` completed, <newTip> (the squashed primary tip) is the newest primary
# target-branch reflog entry that is an ancestor of the worktree HEAD - any newer reflog tip is a later
# primary advance the rebased HEAD does not descend from, so it is skipped. Used to finish an interrupted
# repair whose rebase completed but whose claim/receipt mutation did not; returns $null (caller falls back
# to its fail-closed path) when no reflog tip parents HEAD, e.g. an unrelated detached/reset worktree.
function Get-RepairLandedBaseline([object] $Primary, [object] $Receipt, [string] $WorktreeHead) {
	foreach ($entry in @(Invoke-AgentGit @('-C', $Primary.Root, 'reflog', 'show', $Receipt.targetBranch, '--format=%H', "--max-count=$RepairReflogScanLimit"))) {
		$commit = $entry.Trim()
		if ($commit -cmatch '^[0-9a-f]{40}$' -and (Test-RepairAncestor $Primary.Root $commit $WorktreeHead)) { return $commit }
	}
	return $null
}

# HEAD attached on the given branch (not detached mid-rebase). `branch --show-current` is empty on a
# detached HEAD, so an exact branch match proves attachment.
function Test-RepairHeadAttachedOnBranch([string] $Worktree, [string] $Branch) {
	$lines = @(Invoke-AgentGit @('-C', $Worktree, 'branch', '--show-current'))
	$current = if ($lines.Count -ge 1) { $lines[0].Trim() } else { '' }
	return $current -ceq $Branch
}

# A stash entry kept by `rebase --autostash` when its pop conflicted. Git records it with the fixed reflog
# subject 'autostash', which distinguishes our kept autostash from an ordinary user stash so the pop-conflict
# re-entry detection cannot misfire on an unrelated stashed change.
function Test-RepairKeptAutostash([string] $Worktree) {
	foreach ($line in @(Invoke-AgentGit @('-C', $Worktree, 'stash', 'list'))) {
		if ($line -cmatch ':\s*autostash\s*$') { return $true }
	}
	return $false
}

# The onto commit of an in-progress re-parent rebase for this session, or $null. A re-parent is
# identified by its recorded head-name (the session branch) and an onto that is/was the primary tip
# (an ancestor of the current tip). Any other in-progress operation returns $null so it is rejected
# as an unrelated operation rather than silently finished.
function Get-RepairReparentRebaseOnto([string] $Worktree, [object] $Receipt, [object] $Primary) {
	foreach ($marker in @('rebase-merge', 'rebase-apply')) {
		$state = Get-RepairGitPath $Worktree $marker
		if (-not (Test-Path -LiteralPath $state)) { continue }
		$ontoPath = Join-Path $state 'onto'
		$headNamePath = Join-Path $state 'head-name'
		if ((Test-Path -LiteralPath $ontoPath) -and (Test-Path -LiteralPath $headNamePath)) {
			$onto = (Get-Content -LiteralPath $ontoPath -Raw).Trim()
			$headName = (Get-Content -LiteralPath $headNamePath -Raw).Trim()
			if ($onto -cmatch '^[0-9a-f]{40}$' -and $headName -ceq "refs/heads/$($Receipt.branch)" -and (Test-RepairAncestor $Primary.Root $onto $Primary.Head)) {
				return $onto
			}
		}
		return $null
	}
	return $null
}

function Assert-RepairNoGitOperation([string] $Worktree) {
	foreach ($marker in @('MERGE_HEAD', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'BISECT_LOG', 'rebase-merge', 'rebase-apply', 'sequencer')) {
		if (Test-Path -LiteralPath (Get-RepairGitPath $Worktree $marker)) { throw (New-RepairStateConflict "Git operation marker '$marker' is active in '$Worktree'.") }
	}
}

function Assert-RepairIdentity([object] $Primary, [object] $Receipt, [string] $Worktree) {
	if (-not $Receipt.primaryCheckout.Equals($Primary.Root, [StringComparison]::OrdinalIgnoreCase) -or
		-not $Receipt.gitCommonDirectory.Equals($Primary.CommonDirectory, [StringComparison]::OrdinalIgnoreCase) -or
		-not $Receipt.worktree.Equals($Worktree, [StringComparison]::OrdinalIgnoreCase)) {
		throw (New-RepairStateConflict 'Receipt repository identity does not match the requested re-parent.')
	}
}

# Preconditions guaranteeing `--onto` replays only session commits: primary on the receipt target
# branch, worktree registered on the receipt branch with no other git operation, and the fork bound
# an ancestor of the worktree HEAD. Full registration uniqueness is re-enforced by the reattach proof
# that Start runs after the repair. Returns the worktree HEAD.
function Assert-RepairPreconditions([object] $Primary, [object] $Receipt, [string] $Worktree, [string] $Bound) {
	if ($Primary.Branch -cne $Receipt.targetBranch) { throw (New-RepairStateConflict "Primary checkout is not on receipt target branch '$($Receipt.targetBranch)'.") }
	$worktreeTop = Get-AgentCanonicalPath (Get-RepairGitValue $Worktree @('rev-parse', '--show-toplevel') 'worktree top-level')
	if (-not $worktreeTop.Equals($Worktree, [StringComparison]::OrdinalIgnoreCase)) { throw (New-RepairStateConflict "Recorded worktree is not its Git top-level: '$Worktree'.") }
	$worktreeCommon = Get-AgentCanonicalPath (Get-RepairGitValue $Worktree @('rev-parse', '--path-format=absolute', '--git-common-dir') 'worktree Git common directory')
	if (-not $worktreeCommon.Equals($Primary.CommonDirectory, [StringComparison]::OrdinalIgnoreCase)) { throw (New-RepairStateConflict 'Recorded worktree does not share the primary Git common directory.') }
	$branchLines = @(Invoke-AgentGit @('-C', $Worktree, 'branch', '--show-current'))
	$worktreeBranch = if ($branchLines.Count -ge 1) { $branchLines[0].Trim() } else { '' }
	if ($worktreeBranch -cne $Receipt.branch) { throw (New-RepairStateConflict "Recorded worktree is not on receipt branch '$($Receipt.branch)' (HEAD may be detached).") }
	Assert-RepairNoGitOperation $Worktree
	$worktreeHead = Get-RepairGitValue $Worktree @('rev-parse', 'HEAD') 'worktree HEAD'
	if ($worktreeHead -cnotmatch '^[0-9a-f]{40}$') { throw (New-RepairStateConflict "Recorded worktree HEAD is malformed: '$worktreeHead'.") }
	# The fork bound (>= receipt baseline) must parent the worktree HEAD so `--onto` replays only the
	# session's own commits above it.
	if (-not (Test-RepairAncestor $Primary.Root $Bound $worktreeHead)) {
		throw (New-RepairStateConflict "Fork bound '$Bound' is not an ancestor of worktree HEAD '$worktreeHead'; cannot re-parent only session commits.")
	}
	return $worktreeHead
}

# Unmerged paths from the rebase's conflicted index - the exact files the resumed session must resolve.
function Get-RepairConflictFiles([string] $Worktree) {
	$unmerged = @('DD', 'AU', 'UD', 'UA', 'DU', 'AA', 'UU')
	$files = [Collections.Generic.List[string]]::new()
	foreach ($line in @(Invoke-AgentGit @('-C', $Worktree, 'status', '--porcelain'))) {
		if ($line.Length -ge 4 -and $unmerged -ccontains $line.Substring(0, 2)) { $files.Add($line.Substring(3)) }
	}
	return $files.ToArray()
}

function Invoke-RepairReparentClaims([string] $CommonDirectory, [string] $Worktree, [string] $NewBaseline) {
	# reparent-claims selects the live claims orphaned from the worktree HEAD itself, so it needs only the new
	# tip - a claim taken after a pre-claim primary advance carries a commit newer than this receipt baseline.
	$arguments = @('plan', 'reparent-claims', '--repo', $CommonDirectory, '--worktree', $Worktree, '--new-baseline', $NewBaseline)
	$output = @(& $WorktreeCliExecutable @arguments 2>&1)
	$exit = $LASTEXITCODE
	$text = ($output -join "`n")
	# A CLI failure after the rebase is a hard failure: the worktree state stays explicit and is not
	# un-rebased (an automatic un-rebase would discard the session's conflict resolution). The completed
	# rebase has already moved HEAD off the stale receipt baseline, so a rerun detects the unfinished
	# mutation (baseline no longer parents HEAD, no rebase in progress) and completes reparent-claims + the
	# receipt rewrite from the landed tip - it does not replay the rebase.
	if ($exit -ne 0) { throw "WorktreeCli plan reparent-claims failed (exit $exit): $text" }
	try { $json = ($text | ConvertFrom-Json -Depth 100 -ErrorAction Stop) }
	catch { throw "WorktreeCli plan reparent-claims did not return one JSON value: $text" }
	if ($json.operation -cne 'reparent-claims' -or $json.status -cne 'ok') { throw "WorktreeCli plan reparent-claims returned an unexpected result: $text" }
	return $json
}

try {
	$worktree = Get-AgentCanonicalPath $Worktree
	# The CLI is required to move the claim onto the new tip; check before any lease or rebase so a
	# missing tool never leaves a half-repaired worktree.
	if (-not (Test-Path -LiteralPath $WorktreeCliExecutable)) {
		throw "WorktreeCli executable not found at '$WorktreeCliExecutable'; run a fresh session first to rebuild AgentTools."
	}

	$primary = Get-AgentWorktreePrimaryIdentity $RepositoryRoot
	$receipt = (Read-AgentWorktreeSessionReceipt -Worktree $worktree).Value
	Assert-RepairIdentity $primary $receipt $worktree

	# An already-running re-parent rebase (crash or an earlier conflict not yet continued) takes
	# precedence over the not-needed test: even after the durable baseline was rewritten the session
	# still owns an unresolved rebase, so report 'reparented-conflict' and finish any claim/receipt
	# mutation the earlier run did not reach.
	$inProgressOnto = Get-RepairReparentRebaseOnto $worktree $receipt $primary
	if ($null -ne $inProgressOnto) {
		$conflictFiles = Get-RepairConflictFiles $worktree
		if ($receipt.baseline -ceq $inProgressOnto) {
			# Baseline already rewritten and claims already reparented by the earlier run; nothing to mutate.
			Complete-Repair 'reparented-conflict' $inProgressOnto $inProgressOnto @() @() $conflictFiles $true
		}
		$lease = Open-AgentWorktreeReceiptWriteLease $worktree
		try {
			$leased = Read-AgentWorktreeSessionReceipt -Worktree $worktree -ReadLease $lease
			$oldBaseline = $leased.Value.baseline
			if (Test-RepairAncestor $primary.Root $oldBaseline $inProgressOnto) {
				# The receipt was rewritten between detection and the lease; only the rebase remains.
				Complete-Repair 'reparented-conflict' $oldBaseline $inProgressOnto @() @() $conflictFiles $true
			}
			$reparent = Invoke-RepairReparentClaims $primary.CommonDirectory $worktree $inProgressOnto
			Update-AgentWorktreeSessionReceiptBaseline -Worktree $worktree -WriteLease $lease -ExpectedBytes $leased.Bytes -NewBaseline $inProgressOnto
			Complete-Repair 'reparented-conflict' $oldBaseline $inProgressOnto $reparent.reparentedClaims $reparent.updatedReceipts $conflictFiles $true
		}
		finally { $lease.ReceiptStream.Dispose(); $lease.IntegrityStream.Dispose() }
	}

	$worktreeHead = Get-RepairGitValue $worktree @('rev-parse', 'HEAD') 'worktree HEAD'
	$noRebaseInProgress = -not ((Test-Path -LiteralPath (Get-RepairGitPath $worktree 'rebase-merge')) -or (Test-Path -LiteralPath (Get-RepairGitPath $worktree 'rebase-apply')))
	# The receipt baseline still parents the worktree HEAD until a re-parent rebase has completed and moved
	# HEAD onto the new tip. When it no longer does and no rebase is in progress, a prior run's rebase already
	# completed but its claim/receipt mutation did not (reparent-claims threw on a transient scheduler-guard
	# busy, or the process died before the receipt rewrite). Read-AgentWorktreeSessionReceipt has already
	# failed a tampered receipt closed above, so this is a genuine unfinished re-parent, not tampering.
	$baselineParentsHead = Test-RepairAncestor $primary.Root $receipt.baseline $worktreeHead

	# Finish an interrupted repair whose rebase completed but whose mutation did not: reparent the claims onto
	# the tip the rebase landed on and rewrite the receipt to it, then report. Without this a rerun would
	# either re-report "scheduler ops safe" over an orphaned claim and a stale receipt the reattach proof then
	# rejects forever (pop-conflict variant), or throw the bound-not-ancestor state conflict (clean variant).
	if ($noRebaseInProgress -and -not $baselineParentsHead) {
		$landed = Get-RepairLandedBaseline $primary $receipt $worktreeHead
		# $null means no primary reflog tip parents HEAD (an unrelated detached/reset worktree, never an
		# unfinished re-parent): fall through so the precondition check rejects it, taking no lease and no mutation.
		if ($null -ne $landed) {
			$lease = Open-AgentWorktreeReceiptWriteLease $worktree
			try {
				$leased = Read-AgentWorktreeSessionReceipt -Worktree $worktree -ReadLease $lease
				$leasedHead = Get-RepairGitValue $worktree @('rev-parse', 'HEAD') 'worktree HEAD'
				$conflictFiles = @(Get-RepairConflictFiles $worktree)
				# A concurrent repairer may have finished the mutation between detection and the lease; if the
				# leased baseline now parents HEAD there is nothing to mutate, so only re-report from the tree.
				if (Test-RepairAncestor $primary.Root $leased.Value.baseline $leasedHead) {
					if ($conflictFiles.Count -gt 0) { Complete-Repair 'reparented-conflict' $leased.Value.baseline $leased.Value.baseline @() @() $conflictFiles $false }
					Complete-Repair 'reparented' $leased.Value.baseline $leased.Value.baseline @() @() $null
				}
				$landedLeased = Get-RepairLandedBaseline $primary $leased.Value $leasedHead
				if ($null -eq $landedLeased) { throw (New-RepairStateConflict "No primary reflog tip parents the worktree HEAD '$leasedHead'; cannot finish the interrupted re-parent.") }
				$reparent = Invoke-RepairReparentClaims $primary.CommonDirectory $worktree $landedLeased
				Update-AgentWorktreeSessionReceiptBaseline -Worktree $worktree -WriteLease $lease -ExpectedBytes $leased.Bytes -NewBaseline $landedLeased
				# A leftover pop-conflict keeps the reparented-conflict shape (rebaseInProgress false); a clean
				# completed rebase reports reparented. HEAD is attached either way, so no rebase --continue.
				if ($conflictFiles.Count -gt 0) { Complete-Repair 'reparented-conflict' $leased.Value.baseline $landedLeased $reparent.reparentedClaims $reparent.updatedReceipts $conflictFiles $false }
				Complete-Repair 'reparented' $leased.Value.baseline $landedLeased $reparent.reparentedClaims $reparent.updatedReceipts $null
			}
			finally { $lease.ReceiptStream.Dispose(); $lease.IntegrityStream.Dispose() }
		}
	}

	# A completed re-parent whose kept autostash conflicted on pop (rebaseInProgress false) already rewrote the
	# durable baseline and reparented the claims before returning (baseline now parents HEAD), so the not-needed
	# test below would swallow it - yet the working tree still carries the unresolved conflict and the kept
	# autostash. A crash-and-reattach into that state must reproduce the FIRST-TASK banner and the DataPacker
	# skip, so detect it here, before the not-needed test: unmerged index entries, HEAD attached on the receipt
	# branch, no rebase in progress, and the kept 'autostash' stash. The mutation already finished (the block
	# above handles the unfinished case), so only re-report.
	$popConflictFiles = @(Get-RepairConflictFiles $worktree)
	if ($popConflictFiles.Count -gt 0 -and $noRebaseInProgress -and $baselineParentsHead -and (Test-RepairHeadAttachedOnBranch $worktree $receipt.branch) -and (Test-RepairKeptAutostash $worktree)) {
		Complete-Repair 'reparented-conflict' $receipt.baseline $receipt.baseline @() @() $popConflictFiles $false
	}

	# The true fork point may be newer than the immutable receipt baseline (pre-claim advance / manual
	# fast-forward); derive it so both the needed/not-needed decision and the rebase see the real fork point.
	# Deriving it only when the baseline parents the worktree HEAD keeps the rev-list bounded to the branch's
	# own post-baseline history; an unrelated HEAD is left for the precondition check to reject.
	$bound = if ($baselineParentsHead) { Get-RepairForkBound $primary $receipt $worktreeHead } else { $receipt.baseline }

	# No repair when the fork bound is still reachable from the primary tip - a fresh-session shape, a
	# pre-claim advance that stayed on primary, or a two-wrapper race whose other side already re-parented.
	if (Test-RepairAncestor $primary.Root $bound $primary.Head) {
		Complete-Repair 'not-needed' $receipt.baseline $primary.Head @() @() $null $null $bound
	}

	# Fail fast on the read-only view before taking the exclusive write lease.
	Assert-RepairPreconditions $primary $receipt $worktree $bound | Out-Null

	$lease = Open-AgentWorktreeReceiptWriteLease $worktree
	try {
		# Re-derive everything under the lease: primary may have advanced (a concurrent land) and another
		# repairer may have already fixed the receipt. The captured new tip is used for both the rebase
		# --onto and the claim reparent so their new baseline is identical.
		$primary = Get-AgentWorktreePrimaryIdentity $RepositoryRoot
		$leased = Read-AgentWorktreeSessionReceipt -Worktree $worktree -ReadLease $lease
		$oldBaseline = $leased.Value.baseline
		$newBaseline = $primary.Head
		Assert-RepairIdentity $primary $leased.Value $worktree
		$worktreeHead = Get-RepairGitValue $worktree @('rev-parse', 'HEAD') 'worktree HEAD'
		$bound = if (Test-RepairAncestor $primary.Root $oldBaseline $worktreeHead) { Get-RepairForkBound $primary $leased.Value $worktreeHead } else { $oldBaseline }
		if (Test-RepairAncestor $primary.Root $bound $newBaseline) {
			Complete-Repair 'not-needed' $oldBaseline $newBaseline @() @() $null $null $bound
		}
		Assert-RepairPreconditions $primary $leased.Value $worktree $bound | Out-Null

		# `--onto <newTip> <forkBound>` replays only the commits above the true fork point; rebasing from the
		# stale receipt baseline would replay squashed-away primary commits pulled in by a pre-claim advance.
		$rebaseOutput = @(& git -C $worktree rebase --autostash --onto $newBaseline $bound $leased.Value.branch 2>&1)
		$rebaseExit = $LASTEXITCODE
		$conflictFiles = @()
		$status = 'reparented'
		$rebaseInProgress = $false
		if ($rebaseExit -ne 0) {
			# A conflict leaves a rebase-merge/rebase-apply state; a hard failure (nothing to replay,
			# bad arguments) leaves none and self-aborts, so surface it.
			$rebasing = (Test-Path -LiteralPath (Get-RepairGitPath $worktree 'rebase-merge')) -or (Test-Path -LiteralPath (Get-RepairGitPath $worktree 'rebase-apply'))
			if (-not $rebasing) { throw "git rebase --onto failed to re-parent '$($leased.Value.branch)': $($rebaseOutput -join '; ')." }
			$conflictFiles = Get-RepairConflictFiles $worktree
			$status = 'reparented-conflict'
			$rebaseInProgress = $true
		}
		else {
			# A completed rebase can still leave a conflicted, kept autostash: git prints the autostash-conflict
			# notice and exits 0 while storing the stash, so the plain exit code alone hides the conflict. The
			# unmerged working-tree index proves it. HEAD is already attached, so scheduler ops are safe; the
			# session resolves the files and `git stash drop`s the kept autostash.
			$popConflict = @(Get-RepairConflictFiles $worktree)
			if ($popConflict.Count -gt 0 -or ($rebaseOutput -join "`n") -match 'autostash resulted in conflicts') {
				$conflictFiles = $popConflict
				$status = 'reparented-conflict'
			}
		}

		# Works mid-conflict: the detached HEAD already descends from the new tip, so the claim's
		# baseline-ancestor-of-worktree-HEAD check holds before `git rebase --continue`.
		$reparent = Invoke-RepairReparentClaims $primary.CommonDirectory $worktree $newBaseline
		Update-AgentWorktreeSessionReceiptBaseline -Worktree $worktree -WriteLease $lease -ExpectedBytes $leased.Bytes -NewBaseline $newBaseline

		Complete-Repair $status $oldBaseline $newBaseline $reparent.reparentedClaims $reparent.updatedReceipts $(if ($status -ceq 'reparented-conflict') { $conflictFiles } else { $null }) $(if ($status -ceq 'reparented-conflict') { $rebaseInProgress } else { $null }) $bound
	}
	finally { $lease.ReceiptStream.Dispose(); $lease.IntegrityStream.Dispose() }
}
catch {
	[Console]::Error.WriteLine($_.Exception.Message)
	if ($_.Exception.Data.Contains('RepairStateConflict')) { exit 2 } else { exit 1 }
}
