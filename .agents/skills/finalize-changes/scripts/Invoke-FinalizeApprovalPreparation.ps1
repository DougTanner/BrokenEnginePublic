
# Single pre-approval mutation boundary for a session landing.
# Invoked once per landing after reconciliation has produced the final clean
# session tree, with the same identities, branches, and expected tips required by
# the shared structural sanity check.
#
# The command validates the original candidate, collapses a linear multi-commit
# session range to one deterministic tree-identical commit with the current primary
# tip as its sole parent, atomically replaces only the expected session ref, rolls
# back a replacement whose postconditions fail, and reruns the structural sanity
# check against the final tip. Callers never reconstruct its Git commands
# inline. The review window opens later: Show-FinalizeApprovalReview.ps1 owns the
# SmartGit launch and workflow step 4 calls it last, once the returned tip is bound
# into a fully staged landing.
#
# Success contract: exit 0, schema broken-engine-finalize-approval-preparation/v2,
# status pass, code ok, final sanity PASS, and `session.currentTip` and
# `candidate.commit` naming the exact same validated approval and landing
# candidate. The tree identity checks preserve
# content across a squash; the returned tip replaces the pre-squash session tip in
# every approval-bound field. A later primary advance does not rerun this script —
# rebase the approved candidate directly. A preparation blocker leaves primary
# unchanged; if a replacement occurred, rollback: restored-original is required
# before retrying from current state.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $CurrentWorktree,
	[Parameter(Mandatory)][string] $PrimaryWorktree,
	[Parameter(Mandatory)][string] $CurrentBranch,
	[Parameter(Mandatory)][string] $PrimaryBranch,
	[Parameter(Mandatory)][string] $ExpectedCurrentTip,
	[Parameter(Mandatory)][string] $ExpectedPrimaryTip,
	[string] $VerifiedCandidateCommit,
	[string] $VerifiedCandidateTree,
	[ValidateSet('none', 'compare-and-swap', 'postcondition', 'final-dirty', 'bounded-diagnostic')][string] $FixtureFailure = 'none'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$verifiedCandidateCommitBound = $PSBoundParameters.ContainsKey('VerifiedCandidateCommit')
$verifiedCandidateTreeBound = $PSBoundParameters.ContainsKey('VerifiedCandidateTree')
$workflowModule = Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1'
if (-not (Test-Path -LiteralPath $workflowModule)) {
	$workflowModule = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\FinalizeWorkflowCommon.psm1'
}
Import-Module $workflowModule -Force

$result = [ordered]@{
	schemaVersion = 'broken-engine-finalize-approval-preparation/v2'
	status = 'error'
	code = 'internal.error'
	message = 'Approval preparation did not complete.'
	tips = [ordered]@{
		originalSession = $ExpectedCurrentTip
		primary = $ExpectedPrimaryTip
		approvedSession = $null
	}
	squash = [ordered]@{
		disposition = 'not-run'
		commitCount = 0
		originalTree = $null
		approvedTree = $null
		replacementCommit = $null
		approvedParent = $null
		refUpdated = $false
		rollback = 'not-required'
	}
	verifiedCandidate = [ordered]@{ commit = $VerifiedCandidateCommit; tree = $VerifiedCandidateTree; matched = $false }
	sanity = [ordered]@{
		initial = $null
		final = $null
	}
}

$script:CurrentIdentity = $null
$script:PrimaryIdentity = $null
$script:SessionRef = $null
$script:OriginalTip = $ExpectedCurrentTip
$script:ReplacementTip = $null
$script:RefUpdated = $false

function Complete-Preparation([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message)
{
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	Write-Output ((New-ApprovalPreparationProjection) | ConvertTo-Json -Depth 8 -Compress)
	exit $ExitCode
}

function Get-BoundedApprovalText([AllowNull()] $Value, [int] $Limit)
{
	if ($null -eq $Value) { return [pscustomobject]@{ Text = $null; Length = 0; Truncated = $false } }
	$Value = [string]$Value
	return [pscustomobject]@{ Text = $(if ($Value.Length -gt $Limit) { $Value.Substring(0, $Limit) } else { $Value }); Length = $Value.Length; Truncated = ($Value.Length -gt $Limit) }
}

function Get-ApprovalGitObjectId([AllowNull()] $Value)
{
	if ($null -ne $Value -and [string]$Value -cmatch '^[0-9a-f]{40}$') { return [string]$Value }
	return $null
}

function New-ApprovalPreparationProjection
{
	$message = Get-BoundedApprovalText ([string]$result.message) 512
	$code = Get-BoundedApprovalText ([string]$result.code) 128
	$diagnostics = @()
	if ($result.status -cne 'pass')
	{
		$diagnosticMessage = Get-BoundedApprovalText ([string]$result.message) 512
		$diagnosticCode = Get-BoundedApprovalText ([string]$result.code) 128
		$diagnostics = @([ordered]@{ source = 'Invoke-FinalizeApprovalPreparation'; code = $diagnosticCode.Text; codeLength = $diagnosticCode.Length; codeTruncated = $diagnosticCode.Truncated; path = $null; pathLength = 0; pathTruncated = $false; message = $diagnosticMessage.Text; messageLength = $diagnosticMessage.Length; messageTruncated = $diagnosticMessage.Truncated })
	}
	$initialState = if ($null -ne $result.sanity.initial -and $result.sanity.initial.Ok) { 'pass' } else { 'not-run' }
	$finalState = if ($null -ne $result.sanity.final -and $result.sanity.final.Ok) { 'pass' } else { 'not-run' }
	return [ordered]@{
		schemaVersion = 'broken-engine-finalize-approval-preparation/v2'; status = $result.status; code = $code.Text
		message = $message.Text; messageLength = $message.Length; messageTruncated = $message.Truncated
		session = [ordered]@{ originalTip = Get-ApprovalGitObjectId $result.tips.originalSession; currentTip = Get-ApprovalGitObjectId $result.tips.approvedSession; primaryTip = Get-ApprovalGitObjectId $result.tips.primary }
		candidate = [ordered]@{ commit = Get-ApprovalGitObjectId $result.tips.approvedSession; tree = Get-ApprovalGitObjectId $result.squash.approvedTree; parent = Get-ApprovalGitObjectId $result.squash.approvedParent }
		squash = [ordered]@{ disposition = $result.squash.disposition; commitCount = $result.squash.commitCount; refUpdated = $result.squash.refUpdated; rollback = $result.squash.rollback }
		sanity = [ordered]@{ initial = $initialState; final = $finalState }
		verifiedCandidate = [ordered]@{ supplied = $verifiedCandidateCommitBound; matched = [bool]$result.verifiedCandidate.matched }
		diagnostics = [ordered]@{ totalCount = $diagnostics.Count; items = $diagnostics; truncated = $false; selector = $null; requery = $(if ($diagnostics.Count -gt 0) { 'Invoke-FinalizeApprovalPreparation' } else { $null }) }
	}
}

function Throw-Preparation([int] $ExitCode, [string] $Code, [string] $Message)
{
	$exception = [InvalidOperationException]::new($Message)
	$exception.Data['ExitCode'] = $ExitCode
	$exception.Data['Code'] = $Code
	throw $exception
}

function Assert-Input([bool] $Condition, [string] $Message)
{
	if (-not $Condition)
	{
		Throw-Preparation 1 'input.invalid' $Message
	}
}

function Invoke-LandingSanity([string] $CurrentTip)
{
	$sanity = Test-FinalizeLandingSanity -SessionWorktree $CurrentWorktree -PrimaryWorktree $PrimaryWorktree `
		-SessionBranch $CurrentBranch -PrimaryBranch $PrimaryBranch -ExpectedSessionTip $CurrentTip -ExpectedPrimaryTip $ExpectedPrimaryTip
	if (-not $sanity.Ok)
	{
		Throw-Preparation 2 "sanity.$($sanity.Code)" "Landing sanity failed: $($sanity.Message)"
	}
	return $sanity
}

function Get-GitText([string[]] $Arguments)
{
	return (Invoke-FinalizeGit $script:CurrentIdentity $Arguments).TrimEnd("`r", "`n")
}

function Get-CommitField([string] $Commit, [string] $Format)
{
	return Get-GitText @('show', '--no-show-signature', '-s', "--format=$Format", $Commit)
}

function New-ReplacementCommit([string] $Tree, [string] $Parent, [string] $SourceCommit)
{
	$message = Get-CommitField $SourceCommit '%B'
	$authorName = Get-CommitField $SourceCommit '%an'
	$authorEmail = Get-CommitField $SourceCommit '%ae'
	$authorDate = Get-CommitField $SourceCommit '%aI'
	$committerName = Get-CommitField $SourceCommit '%cn'
	$committerEmail = Get-CommitField $SourceCommit '%ce'
	$committerDate = Get-CommitField $SourceCommit '%cI'
	foreach ($value in @($message, $authorName, $authorEmail, $authorDate, $committerName, $committerEmail, $committerDate))
	{
		if ([string]::IsNullOrWhiteSpace($value))
		{
			Throw-Preparation 2 'git.commit-metadata-invalid' 'The oldest session commit does not provide complete message, author, and committer metadata.'
		}
	}

	$messagePath = Join-Path ([IO.Path]::GetTempPath()) ('BrokenEngineFinalizeMessage-' + [guid]::NewGuid().ToString('N') + '.txt')
	try
	{
		[IO.File]::WriteAllText($messagePath, $message.TrimEnd("`r", "`n") + "`n", [Text.UTF8Encoding]::new($false))
		$start = [Diagnostics.ProcessStartInfo]::new()
		$start.FileName = 'git.exe'
		$start.WorkingDirectory = $script:CurrentIdentity
		$start.UseShellExecute = $false
		$start.CreateNoWindow = $true
		$start.RedirectStandardOutput = $true
		$start.RedirectStandardError = $true
		$start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false, $true)
		$start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false, $true)
		foreach ($argument in @('-C', $script:CurrentIdentity, 'commit-tree', $Tree, '-p', $Parent, '-F', $messagePath))
		{
			[void] $start.ArgumentList.Add($argument)
		}
		$start.Environment['GIT_AUTHOR_NAME'] = $authorName
		$start.Environment['GIT_AUTHOR_EMAIL'] = $authorEmail
		$start.Environment['GIT_AUTHOR_DATE'] = $authorDate
		$start.Environment['GIT_COMMITTER_NAME'] = $committerName
		$start.Environment['GIT_COMMITTER_EMAIL'] = $committerEmail
		$start.Environment['GIT_COMMITTER_DATE'] = $committerDate

		$process = [Diagnostics.Process]::new()
		$process.StartInfo = $start
		if (-not $process.Start())
		{
			Throw-Preparation 1 'git.commit-tree-start-failed' 'Could not start git commit-tree.'
		}
		$stdoutTask = $process.StandardOutput.ReadToEndAsync()
		$stderrTask = $process.StandardError.ReadToEndAsync()
		$process.WaitForExit()
		$stdout = $stdoutTask.GetAwaiter().GetResult().Trim()
		$stderr = $stderrTask.GetAwaiter().GetResult().Trim()
		$exitCode = $process.ExitCode
		$process.Dispose()
		if ($exitCode -ne 0 -or $stdout -cnotmatch '^[0-9a-f]{40}$')
		{
			Throw-Preparation 1 'git.commit-tree-failed' "git commit-tree failed: $stderr"
		}
		return $stdout
	}
	finally
	{
		Remove-Item -LiteralPath $messagePath -Force -ErrorAction SilentlyContinue
	}
}

function Test-CleanSession
{
	return (Invoke-FinalizeGit $script:CurrentIdentity @('status', '--porcelain', '-z', '--untracked-files=all')).Length -eq 0
}

function Restore-OriginalRef
{
	if (-not $script:RefUpdated)
	{
		return
	}
	$result.squash.rollback = 'failed'
	$response = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:CurrentIdentity, 'update-ref', $script:SessionRef, $script:OriginalTip, $script:ReplacementTip) $script:CurrentIdentity
	if ($response.ExitCode -ne 0)
	{
		throw "Atomic rollback failed: $($response.Stderr.Trim())"
	}
	$restoredTip = Get-GitText @('rev-parse', $script:SessionRef)
	if ($restoredTip -cne $script:OriginalTip)
	{
		throw 'Atomic rollback did not restore the original session ref.'
	}
	$script:RefUpdated = $false
	$result.squash.refUpdated = $false
	$result.squash.rollback = 'restored-original'
	$result.tips.approvedSession = $script:OriginalTip
	$result.squash.approvedParent = $null
}

try
{
	foreach ($hash in @($ExpectedCurrentTip, $ExpectedPrimaryTip))
	{
		Assert-Input ($hash -cmatch '^[0-9a-f]{40}$') 'Commit inputs must be exactly 40 lowercase hexadecimal characters.'
	}
	Assert-Input ($verifiedCandidateCommitBound -eq $verifiedCandidateTreeBound) 'Verified candidate commit and tree must be supplied together.'
	if ($verifiedCandidateCommitBound) {
		Assert-Input ($VerifiedCandidateCommit -cmatch '^[0-9a-f]{40}$' -and $VerifiedCandidateTree -cmatch '^[0-9a-f]{40}$') 'Verified candidate identities must be lowercase 40-character object IDs.'
		Assert-Input (Test-FinalizeGitSuccess $CurrentWorktree @('rev-parse', '--verify', "$VerifiedCandidateCommit^{commit}")) 'Verified candidate commit does not exist.'
		Assert-Input ((Invoke-FinalizeGit $CurrentWorktree @('rev-parse', "$VerifiedCandidateCommit^{tree}")).Trim() -ceq $VerifiedCandidateTree) 'Verified candidate tree does not match its commit.'
	}
	if ($FixtureFailure -cne 'none')
	{
		Assert-Input ($env:BROKEN_ENGINE_FINALIZE_APPROVAL_PREPARATION_FIXTURE -ceq '1') 'Fixture-only inputs require the finalization preparation fixture environment.'
	}
	if ($FixtureFailure -ceq 'bounded-diagnostic') { Throw-Preparation 1 (('c' * 140)) (('m' * 600)) }

	$script:CurrentIdentity = Get-FinalizeExistingWindowsIdentity $CurrentWorktree 'Current worktree'
	$script:PrimaryIdentity = Get-FinalizeExistingWindowsIdentity $PrimaryWorktree 'Primary worktree'
	Assert-Input (-not $script:CurrentIdentity.Equals($script:PrimaryIdentity, [StringComparison]::OrdinalIgnoreCase)) 'Approval preparation requires distinct session and primary worktrees.'
	Assert-Input (Test-FinalizeGitSuccess $script:CurrentIdentity @('check-ref-format', '--branch', $CurrentBranch)) 'CurrentBranch is malformed.'
	$script:SessionRef = "refs/heads/$CurrentBranch"

	if (-not (Test-CleanSession))
	{
		Throw-Preparation 2 'git.session-dirty' 'Session worktree or index is not clean.'
	}
	$result.sanity.initial = Invoke-LandingSanity $ExpectedCurrentTip

	$actualBranch = Get-GitText @('branch', '--show-current')
	$actualTip = Get-GitText @('rev-parse', 'HEAD')
	if ($actualBranch -cne $CurrentBranch -or $actualTip -cne $ExpectedCurrentTip)
	{
		Throw-Preparation 2 'git.session-identity-changed' 'Session branch or tip changed after the initial sanity check.'
	}
	$script:OriginalTip = $actualTip
	$result.tips.originalSession = $actualTip
	$result.tips.primary = $ExpectedPrimaryTip

	$range = @((Get-GitText @('rev-list', '--reverse', "$ExpectedPrimaryTip..$actualTip")) -split "`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
	$result.squash.commitCount = $range.Count
	if ($range.Count -eq 0)
	{
		Throw-Preparation 2 'git.session-range-empty' 'Session has no approval candidate above primary.'
	}
	$mergeCommits = Get-GitText @('rev-list', '--min-parents=2', "$ExpectedPrimaryTip..$actualTip")
	if (-not [string]::IsNullOrWhiteSpace($mergeCommits))
	{
		Throw-Preparation 2 'git.session-range-has-merge' 'Session range contains a merge commit.'
	}
	# The squash grafts the session tree onto the primary tip, so a primary tip the session
	# does not already contain would silently drop the intervening primary commits.
	$ancestry = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:CurrentIdentity, 'merge-base', '--is-ancestor', $ExpectedPrimaryTip, $actualTip) $script:CurrentIdentity
	if ($ancestry.ExitCode -eq 1)
	{
		Throw-Preparation 2 'git.primary-not-ancestor' 'Session tip does not contain the current primary tip; rebase the session onto primary before approval preparation.'
	}
	if ($ancestry.ExitCode -ne 0)
	{
		throw "git merge-base --is-ancestor failed: $($ancestry.Stderr.Trim())."
	}
	$originalTree = Get-GitText @('rev-parse', "$actualTip^{tree}")
	if ($verifiedCandidateCommitBound -and $originalTree -cne $VerifiedCandidateTree) { Throw-Preparation 2 'candidate.tree-changed' 'Reconciled session tree no longer equals the reviewed candidate tree.' }
	if ($verifiedCandidateCommitBound) { $result.verifiedCandidate.matched = $true }
	$result.squash.originalTree = $originalTree

	if ($range.Count -eq 1)
	{
		$result.squash.disposition = 'one-commit-no-op'
		$result.squash.approvedTree = $originalTree
		$result.squash.approvedParent = Get-GitText @('rev-parse', "$actualTip^")
		$result.tips.approvedSession = $actualTip
	}
	else
	{
		$replacementTip = New-ReplacementCommit $originalTree $ExpectedPrimaryTip $range[0]
		$script:ReplacementTip = $replacementTip
		$result.squash.replacementCommit = $replacementTip
		$expectedOld = if ($FixtureFailure -ceq 'compare-and-swap') { '0000000000000000000000000000000000000000' } else { $actualTip }
		$update = Invoke-FinalizeNativeText 'git.exe' @('-C', $script:CurrentIdentity, 'update-ref', $script:SessionRef, $replacementTip, $expectedOld) $script:CurrentIdentity
		if ($update.ExitCode -ne 0)
		{
			Throw-Preparation 2 'git.compare-and-swap-failed' 'Session branch changed before its atomic replacement; the original tip remains selected.'
		}
		$script:RefUpdated = $true
		$result.squash.refUpdated = $true
		$result.squash.disposition = 'squashed'
		$result.tips.approvedSession = $replacementTip
		if ($FixtureFailure -ceq 'postcondition')
		{
			Throw-Preparation 2 'git.fixture-postcondition-failed' 'Fixture forced a post-replacement validation failure.'
		}

		$resolvedTip = Get-GitText @('rev-parse', $script:SessionRef)
		$approvedTree = Get-GitText @('rev-parse', "$replacementTip^{tree}")
		$result.squash.approvedTree = $approvedTree
		$parents = @((Get-GitText @('rev-list', '--parents', '-n', '1', $replacementTip)) -split ' ')
		$result.squash.approvedParent = $parents[1]
		$approvedRangeCount = [int](Get-GitText @('rev-list', '--count', "$ExpectedPrimaryTip..$replacementTip"))
		if ($resolvedTip -cne $replacementTip -or $parents.Count -ne 2 -or $parents[1] -cne $ExpectedPrimaryTip -or $approvedRangeCount -ne 1 -or $approvedTree -cne $originalTree -or -not (Test-CleanSession))
		{
			Throw-Preparation 2 'git.squash-postcondition-failed' 'Squashed session did not preserve the required parent, tree, range, ref, and clean-state invariants.'
		}
	}

	$result.sanity.final = Invoke-LandingSanity $result.tips.approvedSession
	if ($result.sanity.final.SessionTip -cne $result.tips.approvedSession -or $result.sanity.final.PrimaryTip -cne $ExpectedPrimaryTip)
	{
		Throw-Preparation 2 'sanity.final-identity-mismatch' 'The final sanity check did not bind the approved session tip and primary tip.'
	}
	if ($FixtureFailure -ceq 'final-dirty')
	{
		[IO.File]::WriteAllText((Join-Path $script:CurrentIdentity 'fixture-final-dirty.tmp'), 'fixture', [Text.UTF8Encoding]::new($false))
	}
	if (-not (Test-CleanSession))
	{
		Throw-Preparation 2 'git.session-dirty-after-sanity' 'Session worktree or index changed during final approval preparation.'
	}

	Complete-Preparation 0 'pass' 'ok' 'Approval candidate is prepared and bound to the final sanity check.'
}
catch
{
	$failure = $_.Exception
	$exitCode = if ($failure.Data.Contains('ExitCode')) { [int]$failure.Data['ExitCode'] } else { 1 }
	$code = if ($failure.Data.Contains('Code')) { [string]$failure.Data['Code'] } else { 'internal.error' }
	$message = $failure.Message
	if ($script:RefUpdated)
	{
		try
		{
			Restore-OriginalRef
		}
		catch
		{
			$result.squash.rollback = 'failed'
			Complete-Preparation 1 'error' 'git.rollback-failed' "$message Rollback blocker: $($_.Exception.Message)"
		}
	}
	$status = if ($exitCode -eq 2) { 'blocked' } else { 'error' }
	Complete-Preparation $exitCode $status $code $message
}
