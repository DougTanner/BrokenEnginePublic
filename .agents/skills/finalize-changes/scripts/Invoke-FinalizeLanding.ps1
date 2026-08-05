# Exclusive owner of post-confirmation landing: structural sanity, the landing lock lease,
# at most one internal rebase of the session branch onto a newly advanced primary, the guarded
# primary advance, and best-effort Plan claim deletion. Exit 0 means the caller reports LANDED;
# exit 2 may report a post-advance blocker but still carries the authoritative lock-cleanup state.
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
	# The caller's post-confirmation lease token. Supplied, the landing continues under that same
	# lease instead of minting one; omitted, a matching retained landing claim may be adopted.
	[string] $OwnerToken,
	[switch] $ReleasePlanClaim,
	[ValidateSet('none', 'compare-and-swap', 'post-reset', 'bounded-diagnostic', 'retry-patch-mismatch')][string] $FixtureFailure = 'none'
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
	schemaVersion = 'broken-engine-finalize-landing/v3'
	status = 'error'
	code = 'internal.error'
	message = 'Landing transaction did not complete.'
	primaryAdvanced = $false
	identities = [ordered]@{ currentWorktree = $null; primaryWorktree = $null; gitCommonDirectory = $null; currentBranch = $null; primaryBranch = $null }
	tips = [ordered]@{ approvedSession = $ApprovedSessionCommit; expectedCurrent = $ExpectedCurrentTip; expectedPrimary = $ExpectedPrimaryTip; current = $null; primary = $null }
	candidate = [ordered]@{ commit = $ApprovedSessionCommit; tree = $ApprovedCandidateTree; treeVerified = $false }
	landed = [ordered]@{ commit = $null; tree = $null; rebaseAttempts = 0 }
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
# What this invocation is actually landing right now. It starts as the user-confirmed candidate on
# the approved primary tip and is replaced, in Invoke-LandingRebaseOntoPrimary alone, by the
# rebased commit and its new base, so every later ancestry, tree, advance, and rollback check reads
# the state the retry produced instead of the stale anchors the caller passed in.
$script:LandingPrimaryTip = $ExpectedPrimaryTip
$script:LandingCommit = $ApprovedSessionCommit
$script:LandingTree = $ApprovedCandidateTree
$script:LandingOwner = $null
$script:LandingClaimed = $false
# The lease duration this landing needs to hold through the whole advance. WorktreeCli's refresh
# re-expires a lease with its own recorded duration and can never lengthen it, so a continued caller
# lease minted shorter than this could expire mid-advance and is refused instead.
$script:LandingLeaseSeconds = 3600
# Set only where the session branch could not be proven restored to the confirmed commit: while it is
# set the lease is retained unconditionally, because releasing it would expose an unproven worktree.
$script:LandingRestorationUnproven = $false
$script:PrimaryIdentity = $null
$script:CurrentIdentity = $null
$script:LandingSession = $null
$script:LandingOwnerAdopted = $false
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
		schemaVersion = 'broken-engine-finalize-landing/v3'; status = $result.status; code = $code.Text; message = $message.Text; messageLength = $message.Length; messageTruncated = $message.Truncated
		primaryAdvanced = [bool]$result.primaryAdvanced; candidate = [ordered]@{ commit = Get-LandingGitObjectId $result.candidate.commit; tree = Get-LandingGitObjectId $result.candidate.tree; treeVerified = [bool]$result.candidate.treeVerified }
		landed = [ordered]@{ commit = $(if ($result.status -ceq 'landed') { Get-LandingGitObjectId $result.landed.commit } else { $null }); tree = $(if ($result.status -ceq 'landed') { Get-LandingGitObjectId $result.landed.tree } else { $null }); rebaseAttempts = [int]$result.landed.rebaseAttempts }
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
	if ($script:LandingRestorationUnproven) {
		$result.residuals.Add('Landing lock retained: the confirmed session commit could not be proven restored.')
		return
	}
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

# The approved inputs bind this landing whether or not primary moved: the commit the user confirmed
# must still carry the tree the user confirmed before any advance, retry, or recovery path runs.
function Assert-ApprovedCandidateTree {
	if ((Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', "$ApprovedSessionCommit^{tree}")).Trim() -cne $ApprovedCandidateTree) {
		Throw-Landing 2 'candidate.tree-changed' 'Approved session commit no longer has the reviewed candidate tree.'
	}
	$result.candidate.treeVerified = $true
}

function Assert-PrimaryAdvanceState {
	$primary = Get-FinalizeGitIdentity $PrimaryWorktree 'Primary worktree'
	$current = Get-FinalizeGitIdentity $CurrentWorktree 'Session worktree'
	if ($primary.Worktree -cne $script:PrimaryIdentity.Worktree -or $current.Worktree -cne $script:CurrentIdentity.Worktree -or
		$primary.Branch -cne $PrimaryBranch -or $current.Branch -cne $CurrentBranch -or
		$primary.Head -cne $script:LandingPrimaryTip -or $current.Head -cne $script:LandingCommit) {
		Throw-Landing 2 'git.identity-changed' 'Primary or session identity changed after approval.'
	}
	if ((Invoke-FinalizeGit $primary.Worktree @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) {
		Throw-Landing 2 'git.primary-dirty' 'Primary worktree is not clean immediately before landing.'
	}
	if ((Invoke-FinalizeGit $primary.Worktree @('rev-parse', "$script:LandingCommit^{tree}")).Trim() -cne $script:LandingTree) {
		Throw-Landing 2 'candidate.tree-changed' 'Approved session commit no longer has the reviewed candidate tree.'
	}
	if (-not (Test-FinalizeGitSuccess $primary.Worktree @('merge-base', '--is-ancestor', $script:LandingPrimaryTip, $script:LandingCommit))) {
		Throw-Landing 2 'git.primary-not-ancestor' 'Approved session commit does not descend from the approved primary tip.'
	}
	if ((Invoke-FinalizeGit $primary.Worktree @('rev-list', "$($script:LandingCommit)..$($script:LandingPrimaryTip)")).Trim().Length -ne 0 -or
		(Invoke-FinalizeGit $primary.Worktree @('rev-list', '--min-parents=2', "$($script:LandingPrimaryTip)..$($script:LandingCommit)")).Trim().Length -ne 0) {
		Throw-Landing 2 'git.landing-history-invalid' 'Landing would replay primary commits or introduce a merge commit.'
	}
}

# Returns $false only for a lost compare-and-swap, which means primary moved under the held lease
# and the caller may rebase once; every other failure is terminal for this landing.
function Advance-PrimaryExactCandidate {
	$expectedCheckout = (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', 'HEAD')).Trim()
	$expectedStatus = Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('status', '--porcelain=v1', '-z', '--untracked-files=all')
	$expectedForCas = if ($FixtureFailure -ceq 'compare-and-swap') { '0000000000000000000000000000000000000000' } else { $script:LandingPrimaryTip }
	$advance = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:PrimaryIdentity.Worktree, 'update-ref', "refs/heads/$PrimaryBranch", $script:LandingCommit, $expectedForCas) $script:PrimaryIdentity.Worktree
	if ($advance.ExitCode -ne 0) { return $false }
	$result.primaryAdvanced = $true
	$result.tips.current = $script:LandingCommit
	$result.tips.primary = $script:LandingCommit
	try {
		$reset = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:PrimaryIdentity.Worktree, 'reset', '--hard', $script:LandingCommit) $script:PrimaryIdentity.Worktree
		if ($reset.ExitCode -ne 0) { throw "Primary checkout did not update to the exact candidate: $($reset.Stderr.Trim())" }
		if ($FixtureFailure -ceq 'post-reset') { throw 'Fixture forced post-reset failure.' }
		$actual = (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', "refs/heads/$PrimaryBranch")).Trim()
		$actualTree = (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', "$actual^{tree}")).Trim()
		if ($actual -cne $script:LandingCommit -or $actualTree -cne $script:LandingTree) { throw 'Primary ref does not equal the exact verified candidate and tree.' }
	}
	catch {
		$reason = $_.Exception.Message
		$rollback = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:PrimaryIdentity.Worktree, 'update-ref', "refs/heads/$PrimaryBranch", $script:LandingPrimaryTip, $script:LandingCommit) $script:PrimaryIdentity.Worktree
		if ($rollback.ExitCode -ne 0) { Throw-Landing 1 'git.rollback-failed' "Exact candidate advance postcondition failed and guarded rollback failed: $reason" }
		$restore = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:PrimaryIdentity.Worktree, 'reset', '--hard', $expectedCheckout) $script:PrimaryIdentity.Worktree
		if ($restore.ExitCode -ne 0 -or (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', "refs/heads/$PrimaryBranch")).Trim() -cne $script:LandingPrimaryTip -or (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', 'HEAD')).Trim() -cne $expectedCheckout -or (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('status', '--porcelain=v1', '-z', '--untracked-files=all')) -cne $expectedStatus) { Throw-Landing 1 'git.rollback-failed' "Exact candidate advance rollback did not restore the expected primary checkout: $reason" }
		$result.primaryAdvanced = $false
		Throw-Landing 2 'candidate.postcondition-failed' $reason
	}
	$result.landed.commit = $script:LandingCommit
	$result.landed.tree = $script:LandingTree
	return $true
}

function Start-LandingGitProcess([string[]] $Arguments, [bool] $RedirectInput) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = 'git.exe'
	$start.WorkingDirectory = $script:CurrentIdentity.Worktree
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.RedirectStandardInput = $RedirectInput
	foreach ($argument in $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start 'git.exe'." }
	return $process
}

# Patch text reaches git patch-id as the exact bytes git produced: the identity is compared across
# the rebase to decide whether the landing bytes are still the confirmed ones, so no text decoding
# may sit between the two commands. --verbatim keeps whitespace significant, so a commit differing
# from the confirmed one only in whitespace cannot pass as byte-identical; it implies --stable, so
# only hunk line numbers stay ignored and a rebase over another hunk still compares equal.
function Get-LandingPatchId([string] $Base, [string] $Tip) {
	$worktree = $script:CurrentIdentity.Worktree
	$patch = [IO.MemoryStream]::new()
	try {
		$diff = Start-LandingGitProcess @('-C', $worktree, 'diff-tree', '-p', '--no-color', '--no-ext-diff', '--full-index', '--binary', '--no-renames', '-r', $Base, $Tip) $false
		$diffErrorTask = $diff.StandardError.ReadToEndAsync()
		$diff.StandardOutput.BaseStream.CopyTo($patch)
		$diffError = $diffErrorTask.GetAwaiter().GetResult()
		$diff.WaitForExit()
		$diffExit = $diff.ExitCode
		$diff.Dispose()
		if ($diffExit -ne 0) { throw "git diff-tree failed for the landing patch: $($diffError.Trim())" }
		$identify = Start-LandingGitProcess @('patch-id', '--verbatim') $true
		$identifyOutTask = $identify.StandardOutput.ReadToEndAsync()
		$identifyErrorTask = $identify.StandardError.ReadToEndAsync()
		$patch.Position = 0
		$patch.CopyTo($identify.StandardInput.BaseStream)
		$identify.StandardInput.BaseStream.Flush()
		$identify.StandardInput.Close()
		$identifyOut = $identifyOutTask.GetAwaiter().GetResult()
		$identifyError = $identifyErrorTask.GetAwaiter().GetResult()
		$identify.WaitForExit()
		$identifyExit = $identify.ExitCode
		$identify.Dispose()
		if ($identifyExit -ne 0) { throw "git patch-id failed for the landing patch: $($identifyError.Trim())" }
		$identityValue = $identifyOut.Trim().Split(' ')[0]
		if ([string]::IsNullOrWhiteSpace($identityValue)) { throw 'git patch-id produced no identity for the landing patch.' }
		return $identityValue
	}
	finally { $patch.Dispose() }
}

# Path, mode, and status of every changed file, with the blob object IDs deliberately dropped: an
# upstream commit touching another hunk of the same file changes the pre-image ID without changing
# what this patch does, and content — including binary blobs — is already covered by patch-id.
function Get-LandingChangeGuard([string] $Base, [string] $Tip) {
	$fields = (Invoke-FinalizeGit $script:CurrentIdentity.Worktree @('diff', '--raw', '--no-renames', '-z', $Base, $Tip)).Split([char]0, [StringSplitOptions]::RemoveEmptyEntries)
	if ($fields.Count % 2 -ne 0) { throw 'git diff --raw produced an incomplete record for the landing patch.' }
	$records = [Collections.Generic.List[string]]::new()
	for ($index = 0; $index -lt $fields.Count; $index += 2) {
		$parts = $fields[$index].Split(' ')
		if ($parts.Count -ne 5) { throw "git diff --raw produced an unrecognized record: '$($fields[$index])'." }
		$records.Add("$($parts[0]) $($parts[1]) $($parts[4])`t$($fields[$index + 1])")
	}
	return $records -join "`n"
}

# Two independent signals: patch-id proves the applied content, and the change guard proves the
# patch still touches the same paths with the same modes and statuses.
function Get-LandingPatchIdentity([string] $Base, [string] $Tip) {
	return [pscustomobject]@{
		PatchId = Get-LandingPatchId $Base $Tip
		Changes = Get-LandingChangeGuard $Base $Tip
	}
}

# Restoration is proven, never assumed, so an unproven session worktree also keeps the lease: only
# this transaction knows the branch state it left behind.
function Throw-LandingRestorationUnproven([string] $Message) {
	$script:LandingRestorationUnproven = $true
	Throw-Landing 2 'rebase.abort-failed' $Message
}

# A blocked retry must leave the session branch exactly as the user confirmed it, which is the
# approved commit itself even after the rebase moved the branch to its replacement.
function Restore-LandingSessionBranch([string] $Code, [string] $Reason, [string] $Disposition = 'terminal', [int] $RetryAfterMilliseconds = 0) {
	$worktree = $script:CurrentIdentity.Worktree
	foreach ($marker in @('rebase-merge', 'rebase-apply')) {
		# `git rebase --abort` exits 128 when no rebase is in progress, so only a started one is aborted.
		if (Test-Path -LiteralPath (Invoke-FinalizeGit $worktree @('rev-parse', '--path-format=absolute', '--git-path', $marker)).Trim()) {
			$abort = Invoke-FinalizeNativeText 'git.exe' @('-C', $worktree, 'rebase', '--abort') $worktree
			if ($abort.ExitCode -ne 0) { Throw-LandingRestorationUnproven "$Reason Aborting the rebase failed: $($abort.Stderr.Trim())" }
			break
		}
	}
	if ((Invoke-FinalizeGit $worktree @('rev-parse', "refs/heads/$CurrentBranch")).Trim() -cne $ApprovedSessionCommit) {
		$restore = Invoke-FinalizeNativeText 'git.exe' @('-C', $worktree, 'reset', '--hard', $ApprovedSessionCommit) $worktree
		if ($restore.ExitCode -ne 0) { Throw-LandingRestorationUnproven "$Reason Restoring the confirmed session commit failed: $($restore.Stderr.Trim())" }
	}
	if ((Invoke-FinalizeGit $worktree @('rev-parse', 'HEAD')).Trim() -cne $ApprovedSessionCommit -or
		(Invoke-FinalizeGit $worktree @('rev-parse', "refs/heads/$CurrentBranch")).Trim() -cne $ApprovedSessionCommit -or
		(Invoke-FinalizeGit $worktree @('status', '--porcelain', '-z', '--untracked-files=all')).Length -ne 0) {
		Throw-LandingRestorationUnproven "$Reason The session worktree could not be proven restored to the confirmed commit."
	}
	Throw-Landing 2 $Code $Reason $Disposition $false $RetryAfterMilliseconds
}

# The single place the landing state moves off the caller's approved anchors: only a clean rebase
# whose patch is provably identical to the confirmed one becomes the new landing candidate, so a
# retry can never advance primary with bytes the user did not confirm.
function Invoke-LandingRebaseOntoPrimary {
	$worktree = $script:CurrentIdentity.Worktree
	$approvedIdentity = Get-LandingPatchIdentity $script:LandingPrimaryTip $script:LandingCommit
	$newPrimaryTip = (Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', "refs/heads/$PrimaryBranch")).Trim()
	$result.landed.rebaseAttempts = [int]$result.landed.rebaseAttempts + 1
	$rebase = Invoke-FinalizeNativeText 'git.exe' @('-C', $worktree, 'rebase', '--onto', $newPrimaryTip, $script:LandingPrimaryTip, $CurrentBranch) $worktree
	if ($rebase.ExitCode -ne 0) {
		Restore-LandingSessionBranch 'rebase.conflicted' "Rebasing the confirmed candidate onto the current primary tip did not apply cleanly: $($rebase.Stderr.Trim())"
	}
	$rebasedCommit = (Invoke-FinalizeGit $worktree @('rev-parse', "refs/heads/$CurrentBranch")).Trim()
	$rebasedIdentity = Get-LandingPatchIdentity $newPrimaryTip $rebasedCommit
	if ($FixtureFailure -ceq 'retry-patch-mismatch' -or $rebasedIdentity.PatchId -cne $approvedIdentity.PatchId -or $rebasedIdentity.Changes -cne $approvedIdentity.Changes) {
		Restore-LandingSessionBranch 'rebase.patch-not-identical' 'Rebasing onto the current primary tip did not reproduce the confirmed patch.'
	}
	$script:LandingPrimaryTip = $newPrimaryTip
	$script:LandingCommit = $rebasedCommit
	$script:LandingTree = (Invoke-FinalizeGit $worktree @('rev-parse', "$rebasedCommit^{tree}")).Trim()
}

# A crash after an internally rebased advance leaves primary, and the session branch the rebase
# moved, on the rebased commit instead of the confirmed one. That is this landing's own completed
# work only when the rebased commit's patch is provably the confirmed patch, so recovery rests on
# that proof alone and then continues from the rebased anchors.
function Test-LandingRebasedRecovery {
	$primaryHead = $script:PrimaryIdentity.Head
	if ($script:CurrentIdentity.Head -cne $primaryHead -or $primaryHead -ceq $ApprovedSessionCommit) { return $false }
	$worktree = $script:CurrentIdentity.Worktree
	foreach ($commit in @($ApprovedSessionCommit, $ExpectedPrimaryTip, $primaryHead)) {
		if (-not (Test-FinalizeGitSuccess $worktree @('rev-parse', '--verify', '--quiet', "$commit^{commit}"))) { return $false }
	}
	$parents = (Invoke-FinalizeGit $worktree @('rev-list', '--no-walk', '--parents', '--max-count=1', $primaryHead)).Trim().Split(' ')
	if ($parents.Count -ne 2) { return $false }
	$landedIdentity = Get-LandingPatchIdentity $parents[1] $primaryHead
	$approvedIdentity = Get-LandingPatchIdentity $ExpectedPrimaryTip $ApprovedSessionCommit
	if ($landedIdentity.PatchId -cne $approvedIdentity.PatchId -or $landedIdentity.Changes -cne $approvedIdentity.Changes) { return $false }
	Assert-ApprovedCandidateTree
	$script:LandingPrimaryTip = $parents[1]
	$script:LandingCommit = $primaryHead
	$script:LandingTree = (Invoke-FinalizeGit $worktree @('rev-parse', "$primaryHead^{tree}")).Trim()
	$result.landed.rebaseAttempts = 1
	return $true
}

# Returns $false when primary moved under the held lease, either before the attempt or between the
# checks and the compare-and-swap.
function Invoke-LandingAdvanceAttempt {
	if ((Invoke-FinalizeGit $script:PrimaryIdentity.Worktree @('rev-parse', "refs/heads/$PrimaryBranch")).Trim() -cne $script:LandingPrimaryTip) { return $false }
	Assert-PrimaryAdvanceState
	Refresh-LandingOwner
	return (Advance-PrimaryExactCandidate)
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
	$registration = Test-FinalizeWorktreeRegistration $script:PrimaryIdentity.Worktree $script:CurrentIdentity.Worktree $CurrentBranch $script:LandingCommit
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
	if (-not [string]::IsNullOrWhiteSpace($OwnerToken) -and $OwnerToken -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') {
		Throw-Landing 1 'input.owner-token-invalid' 'OwnerToken must be a canonical WorktreeCli lock owner token.'
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
	$script:LandingSession = if ([string]::IsNullOrWhiteSpace($OwnerToken)) { "$SessionLabel/landing" } else { $SessionLabel }
	# A transient operation claim with a fresh per-landing owner excludes AgentTools promotion
	# from swapping WorktreeCli.exe across this multi-invocation landing transaction. Registered
	# before the first WorktreeCli.exe use; released in cleanup alongside the landing lock.
	$landingOwner = [guid]::NewGuid().ToString()
	Register-WorktreeCliSession -RepositoryRoot $script:CurrentIdentity.Worktree -Owner $landingOwner -Label 'session landing' -Worktree $script:CurrentIdentity.Worktree | Out-Null
	$script:LandingTransientOwner = $landingOwner
	# Post-advance recovery, idempotent for a crash at any point after the advance: the confirmed
	# candidate is already on primary either as itself, or as the commit this landing's own internal
	# rebase produced.
	$recovered = $false
	if ($script:CurrentIdentity.Head -ceq $script:LandingCommit -and
		(Test-FinalizeGitSuccess $script:PrimaryIdentity.Worktree @('merge-base', '--is-ancestor', $script:LandingCommit, $script:PrimaryIdentity.Head))) {
		Assert-ApprovedCandidateTree
		$recovered = $true
	}
	elseif (Test-LandingRebasedRecovery) { $recovered = $true }
	if ($recovered) {
		[void] (Assert-LandingSanity $script:LandingCommit $script:PrimaryIdentity.Head)
		$result.primaryAdvanced = $true
		$result.landed.commit = $script:LandingCommit
		$result.landed.tree = $script:LandingTree
		# The crashed invocation's lease outlives its process, so recovery adopts and releases only a
		# live claim matching the supplied raw or omitted derived identity and canonical worktree.
		# Anything else is foreign and is left alone to expire on its own.
		$recoveredLock = Get-FinalizeLandingLockState $script:WorktreeCliPath $result.identities.gitCommonDirectory $script:CurrentIdentity.Worktree
		$recoveryOwner = $OwnerToken
		if ([string]::IsNullOrWhiteSpace($OwnerToken) -and $recoveredLock.Kind -ceq 'live' -and @($recoveredLock.Status.PSObject.Properties.Name) -ccontains 'owner') {
			$recoveryOwner = [string]$recoveredLock.Status.owner
		}
		if (($recoveredLock.Kind -ceq 'live') -and -not [string]::IsNullOrWhiteSpace($recoveryOwner) -and (Test-FinalizeLandingLockClaimIdentity $recoveredLock.Status $recoveryOwner $script:LandingSession $script:CurrentIdentity.Worktree)) {
			$script:LandingOwner = $recoveryOwner
			$result.locks.landingOwner = $script:LandingOwner
			$script:LandingClaimed = $true
			$result.locks.landingClaimed = $true
			Release-LandingLockIfSafe
		}
		Complete-LandedState
		Write-Output ((New-LandingProjection) | ConvertTo-Json -Depth 10 -Compress)
		exit 0
	}

	# The primary tip is deliberately not asserted here: an advance racing this landing is answered
	# by the post-claim check and the internal rebase, not by refusing before the lock is held.
	[void] (Assert-LandingSanity $ExpectedCurrentTip '')
	if ($ExpectedCurrentTip -cne $script:LandingCommit) { Throw-Landing 2 'approval.session-tip-changed' 'Session tip is not the explicit user-approved commit.' }

	if ([string]::IsNullOrWhiteSpace($OwnerToken)) {
		# Landing claims use a derived identity so the caller's raw reconciliation lease remains distinct.
		# Refresh preserves a lease's recorded duration, so a matching short lease cannot be adopted for
		# the full landing transaction.
		$lockState = Get-FinalizeLandingLockState $script:WorktreeCliPath $result.identities.gitCommonDirectory $script:CurrentIdentity.Worktree
		$leaseDurationSeconds = if (@($lockState.Status.PSObject.Properties.Name) -ccontains 'leaseDurationSeconds') { $lockState.Status.leaseDurationSeconds } else { $null }
		$leaseDurationIsInteger = $leaseDurationSeconds -is [int] -or $leaseDurationSeconds -is [long]
		if (($lockState.Kind -ceq 'live') -and (Test-FinalizeLandingLockClaimIdentity $lockState.Status ([string]$lockState.Status.owner) $script:LandingSession $script:CurrentIdentity.Worktree) -and $leaseDurationIsInteger -and [int64]$leaseDurationSeconds -ge $script:LandingLeaseSeconds) {
			$script:LandingOwner = [string]$lockState.Status.owner
			$script:LandingOwnerAdopted = $true
		}
		else {
			$tokenResponse = Invoke-WorktreeCli @('lock', 'token')
			if ($tokenResponse.ExitCode -ne 0 -or $tokenResponse.Stdout.Trim() -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') {
				Throw-Landing 1 'landing-lock.token-failed' 'WorktreeCli could not generate a canonical landing owner token.'
			}
			$script:LandingOwner = $tokenResponse.Stdout.Trim()
		}
		$result.locks.landingOwner = $script:LandingOwner
		$claimOutcome = Invoke-FinalizeLandingLockClaim -WorktreeCliExecutable $script:WorktreeCliPath -GitCommonDirectory $result.identities.gitCommonDirectory -Owner $script:LandingOwner -Session $script:LandingSession -Worktree $script:CurrentIdentity.Worktree -LeaseSeconds $script:LandingLeaseSeconds -WaitSeconds 300
	}
	else {
		# Lease continuity: a live lease under the supplied token whose recorded session and worktree
		# match this landing identity, and which was minted for at least the landing lease duration,
		# is the same actor continuing, so it is used as is. Anything else — another session or
		# worktree, a shorter lease, expired, or absent — stays foreign contention, and no fresh
		# token is ever minted behind the caller's back.
		$script:LandingOwner = $OwnerToken
		$result.locks.landingOwner = $script:LandingOwner
		$lockState = Get-FinalizeLandingLockState $script:WorktreeCliPath $result.identities.gitCommonDirectory $script:CurrentIdentity.Worktree
		$sameActor = ($lockState.Kind -ceq 'live') -and (Test-FinalizeLandingLockClaimIdentity $lockState.Status $script:LandingOwner $SessionLabel $script:CurrentIdentity.Worktree)
		$leaseSeconds = if ($sameActor -and @($lockState.Status.PSObject.Properties.Name) -ccontains 'leaseDurationSeconds') { $lockState.Status.leaseDurationSeconds -as [int] } else { $null }
		$claimOutcome = if ($sameActor -and $null -ne $leaseSeconds -and $leaseSeconds -ge $script:LandingLeaseSeconds) {
			[pscustomobject]@{ Claimed = $true; Code = 'ok'; Message = 'Landing continues under the caller lease already live for this session and worktree.'; Disposition = 'terminal'; RequiresUserAuthority = $false; RetryAfterMilliseconds = 0; Owner = $script:LandingOwner; Lock = $lockState.Status; Attempts = 1 }
		}
		else {
			$refusal = if ($sameActor) { "The supplied owner token's lease is shorter than the $($script:LandingLeaseSeconds)-second landing lease and cannot be extended." } else { 'The supplied owner token is not a live landing lease for this session and worktree.' }
			[pscustomobject]@{ Claimed = $false; Code = 'landing-lock.retryable-wait'; Message = $refusal; Disposition = 'retryable-wait'; RequiresUserAuthority = $false; RetryAfterMilliseconds = 500; Owner = $script:LandingOwner; Lock = $lockState.Status; Attempts = 1 }
		}
	}
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
	if ($script:LandingOwnerAdopted) { Refresh-LandingOwner }
	Assert-LandingOwner
	Assert-ApprovedCandidateTree

	# At most one rebase-and-retry per invocation, entirely under the held lease: a second stale
	# result means contention this landing should not keep fighting. The retry left the branch on the
	# rebased commit, so exhaustion restores the confirmed one the documented rerun expects.
	if (-not (Invoke-LandingAdvanceAttempt)) {
		Invoke-LandingRebaseOntoPrimary
		if (-not (Invoke-LandingAdvanceAttempt)) {
			Restore-LandingSessionBranch 'landing.retry-exhausted' 'Primary advanced again after the rebased candidate was prepared; this landing made its one retry.' 'retryable-wait' 500
		}
	}
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
