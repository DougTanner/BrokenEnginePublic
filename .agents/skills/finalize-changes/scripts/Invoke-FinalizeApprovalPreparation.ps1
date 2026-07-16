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
	[string] $WaitSeconds = '60',
	[switch] $HasPlanRowClaim,
	[switch] $HasCompletedPlanClaim,
	[switch] $QueueChangingLanding,
	[AllowEmptyString()][string] $FixtureSmartGitExecutable,
	[ValidateSet('none', 'compare-and-swap', 'postcondition', 'final-dirty', 'smartgit-launch')][string] $FixtureFailure = 'none'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workflowModule = Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1'
Import-Module $workflowModule -Force

$result = [ordered]@{
	schemaVersion = 'broken-engine-finalize-approval-preparation/v1'
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
		refUpdated = $false
		rollback = 'not-required'
	}
	preflight = [ordered]@{
		initial = $null
		final = $null
	}
	smartGit = [ordered]@{
		status = 'not-run'
		executable = $null
		arguments = @()
		manualCommand = $null
		processId = $null
		message = $null
	}
}

$script:CurrentIdentity = $null
$script:PrimaryIdentity = $null
$script:SessionRef = $null
$script:OriginalTip = $ExpectedCurrentTip
$script:ReplacementTip = $null
$script:RefUpdated = $false
$script:HasFixtureSmartGitExecutable = $PSBoundParameters.ContainsKey('FixtureSmartGitExecutable')

function Complete-Preparation([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message)
{
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 12 -Compress))
	exit $ExitCode
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

function Get-JsonResponse($Response, [string] $Operation)
{
	try
	{
		return $Response.Stdout | ConvertFrom-Json
	}
	catch
	{
		Throw-Preparation 1 'process.invalid-json' "$Operation returned invalid JSON: $($Response.Stdout.Trim())"
	}
}

function Invoke-Preflight([string] $CurrentTip)
{
	$preflight = Join-Path $PSScriptRoot 'Test-FinalizePreflight.ps1'
	$arguments = [Collections.Generic.List[string]]::new()
	foreach ($argument in @(
		'-NoProfile', '-File', $preflight,
		'-Mode', 'session-landing',
		'-Checkpoint', 'after-reconciliation',
		'-CurrentWorktree', $CurrentWorktree,
		'-PrimaryWorktree', $PrimaryWorktree,
		'-CurrentBranch', $CurrentBranch,
		'-PrimaryBranch', $PrimaryBranch,
		'-Baseline', $Baseline,
		'-ManifestComparisonBase', $ManifestComparisonBase,
		'-ExpectedCurrentTip', $CurrentTip,
		'-ExpectedPrimaryTip', $ExpectedPrimaryTip,
		'-VerificationReportPath', $VerificationReportPath,
		'-VerificationReportSha256', $VerificationReportSha256,
		'-ManifestRange'
	))
	{
		$arguments.Add($argument)
	}
	$arguments.Add(($ManifestRange -join ','))
	foreach ($argument in @('-SessionOwner', $SessionOwner, '-WaitSeconds', $WaitSeconds))
	{
		$arguments.Add($argument)
	}
	if ($HasPlanRowClaim) { $arguments.Add('-HasPlanRowClaim') }
	if ($HasCompletedPlanClaim) { $arguments.Add('-HasCompletedPlanClaim') }
	if ($QueueChangingLanding) { $arguments.Add('-QueueChangingLanding') }

	$response = Invoke-FinalizeNativeText 'pwsh.exe' $arguments.ToArray() $CurrentWorktree
	$preflightResult = Get-JsonResponse $response 'Finalization preflight'
	if ($response.ExitCode -ne 0 -or $preflightResult.status -cne 'pass' -or $preflightResult.code -cne 'ok')
	{
		$exitCode = if ($response.ExitCode -eq 2) { 2 } else { 1 }
		Throw-Preparation $exitCode "preflight.$($preflightResult.code)" "Finalization preflight failed: $($preflightResult.message)"
	}
	return $preflightResult
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
}

function ConvertTo-PowerShellLiteral([string] $Value)
{
	return "'" + $Value.Replace("'", "''") + "'"
}

function ConvertTo-ProcessArgument([string] $Value)
{
	if ($Value.Contains('"', [StringComparison]::Ordinal))
	{
		throw 'SmartGit arguments cannot contain a double quote.'
	}
	return '"' + $Value + '"'
}

function Invoke-SmartGit([string] $ApprovedTip)
{
	$standardExecutable = 'C:\Program Files\SmartGit\bin\smartgit.exe'
	$resolvedExecutable = $null
	if ($script:HasFixtureSmartGitExecutable)
	{
		$resolvedExecutable = $FixtureSmartGitExecutable
	}
	elseif (Test-Path -LiteralPath $standardExecutable -PathType Leaf)
	{
		$resolvedExecutable = $standardExecutable
	}
	else
	{
		$command = Get-Command smartgit.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
		if ($null -ne $command)
		{
			$resolvedExecutable = $command.Source
		}
	}

	$arguments = @('--log', $script:PrimaryIdentity, "--anchor-commit=$ApprovedTip")
	$manualExecutable = if ([string]::IsNullOrWhiteSpace($resolvedExecutable)) { $standardExecutable } else { $resolvedExecutable }
	$result.smartGit.arguments = $arguments
	$result.smartGit.manualCommand = ((@('&', (ConvertTo-PowerShellLiteral $manualExecutable)) + @($arguments | ForEach-Object { ConvertTo-PowerShellLiteral $_ })) -join ' ')
	if ([string]::IsNullOrWhiteSpace($resolvedExecutable) -or -not (Test-Path -LiteralPath $resolvedExecutable -PathType Leaf))
	{
		$result.smartGit.status = 'unavailable'
		$result.smartGit.message = 'SmartGit executable was not found.'
		return
	}

	$result.smartGit.executable = [IO.Path]::GetFullPath($resolvedExecutable)
	try
	{
		if ($FixtureFailure -ceq 'smartgit-launch')
		{
			throw 'Fixture forced a SmartGit launch failure.'
		}
		$processArguments = @($arguments | ForEach-Object { ConvertTo-ProcessArgument $_ })
		$process = Start-Process -FilePath $result.smartGit.executable -ArgumentList $processArguments -PassThru
		$result.smartGit.status = 'opened'
		$result.smartGit.processId = $process.Id
		$result.smartGit.message = 'SmartGit was opened for the approval candidate.'
		$process.Dispose()
	}
	catch
	{
		$result.smartGit.status = 'failed'
		$result.smartGit.message = $_.Exception.Message
	}
}

try
{
	foreach ($hash in @($Baseline, $ManifestComparisonBase, $ExpectedCurrentTip, $ExpectedPrimaryTip))
	{
		Assert-Input ($hash -cmatch '^[0-9a-f]{40}$') 'Commit inputs must be exactly 40 lowercase hexadecimal characters.'
	}
	Assert-Input ($VerificationReportSha256 -cmatch '^[0-9a-f]{64}$') 'VerificationReportSha256 must be exactly 64 lowercase hexadecimal characters.'
	Assert-Input ($SessionOwner -cmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') 'SessionOwner must be a canonical lowercase GUID.'
	Assert-Input (@($ManifestRange).Count -gt 0) 'At least one manifest range is required.'
	Assert-Input (-not $HasCompletedPlanClaim -or $HasPlanRowClaim) 'HasCompletedPlanClaim requires HasPlanRowClaim.'
	Assert-Input ($ManifestComparisonBase -ceq $ExpectedPrimaryTip) 'Approval preparation requires ManifestComparisonBase to equal the expected primary tip.'
	if ($FixtureFailure -cne 'none' -or $PSBoundParameters.ContainsKey('FixtureSmartGitExecutable'))
	{
		Assert-Input ($env:BROKEN_ENGINE_FINALIZE_APPROVAL_PREPARATION_FIXTURE -ceq '1') 'Fixture-only inputs require the finalization preparation fixture environment.'
	}

	$script:CurrentIdentity = Get-FinalizeExistingWindowsIdentity $CurrentWorktree 'Current worktree'
	$script:PrimaryIdentity = Get-FinalizeExistingWindowsIdentity $PrimaryWorktree 'Primary worktree'
	Assert-Input (-not $script:CurrentIdentity.Equals($script:PrimaryIdentity, [StringComparison]::OrdinalIgnoreCase)) 'Approval preparation requires distinct session and primary worktrees.'
	Assert-Input (Test-FinalizeGitSuccess $script:CurrentIdentity @('check-ref-format', '--branch', $CurrentBranch)) 'CurrentBranch is malformed.'
	$script:SessionRef = "refs/heads/$CurrentBranch"

	if (-not (Test-CleanSession))
	{
		Throw-Preparation 2 'git.session-dirty' 'Session worktree or index is not clean.'
	}
	$result.preflight.initial = Invoke-Preflight $ExpectedCurrentTip

	$actualBranch = Get-GitText @('branch', '--show-current')
	$actualTip = Get-GitText @('rev-parse', 'HEAD')
	if ($actualBranch -cne $CurrentBranch -or $actualTip -cne $ExpectedCurrentTip)
	{
		Throw-Preparation 2 'git.session-identity-changed' 'Session branch or tip changed after initial preflight.'
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
	$originalTree = Get-GitText @('rev-parse', "$actualTip^{tree}")
	$result.squash.originalTree = $originalTree

	if ($range.Count -eq 1)
	{
		$result.squash.disposition = 'one-commit-no-op'
		$result.squash.approvedTree = $originalTree
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
		$approvedRangeCount = [int](Get-GitText @('rev-list', '--count', "$ExpectedPrimaryTip..$replacementTip"))
		if ($resolvedTip -cne $replacementTip -or $parents.Count -ne 2 -or $parents[1] -cne $ExpectedPrimaryTip -or $approvedRangeCount -ne 1 -or $approvedTree -cne $originalTree -or -not (Test-CleanSession))
		{
			Throw-Preparation 2 'git.squash-postcondition-failed' 'Squashed session did not preserve the required parent, tree, range, ref, and clean-state invariants.'
		}
	}

	$result.preflight.final = Invoke-Preflight $result.tips.approvedSession
	if ($result.preflight.final.tips.current -cne $result.tips.approvedSession -or $result.preflight.final.tips.primary -cne $ExpectedPrimaryTip -or -not $result.preflight.final.manifest.equal)
	{
		Throw-Preparation 2 'preflight.final-identity-mismatch' 'Final preflight did not bind the approved session tip, primary tip, and verified manifest.'
	}
	if ($FixtureFailure -ceq 'final-dirty')
	{
		[IO.File]::WriteAllText((Join-Path $script:CurrentIdentity 'fixture-final-dirty.tmp'), 'fixture', [Text.UTF8Encoding]::new($false))
	}
	if (-not (Test-CleanSession))
	{
		Throw-Preparation 2 'git.session-dirty-after-preflight' 'Session worktree or index changed during final approval preparation.'
	}

	Invoke-SmartGit $result.tips.approvedSession
	Complete-Preparation 0 'pass' 'ok' 'Approval candidate is prepared and bound to final preflight.'
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
