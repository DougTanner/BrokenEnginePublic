[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$preparation = Join-Path $PSScriptRoot 'Invoke-FinalizeApprovalPreparation.ps1'
$reportWriter = Join-Path $PSScriptRoot '..\..\..\scripts\Write-AgentVerificationReport.ps1'
$reportAllocator = Join-Path $PSScriptRoot '..\..\..\scripts\New-AgentReportPath.ps1'
$fixtureRoot = $null
$fixturePrimary = $null
$fixtureBaseline = $null
$fixturePrimaryBranch = $null
$sourceWorktree = $null
$environmentBackup = @{}

function Assert-Condition([bool] $Condition, [string] $Message)
{
	if (-not $Condition)
	{
		throw $Message
	}
}

function Assert-TemporaryPath([string] $Path)
{
	$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
	$candidate = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
	if (-not $candidate.StartsWith($tempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))
	{
		throw "Fixture path escapes the system temporary directory: '$candidate'."
	}
}

function Invoke-Git([string] $WorkingDirectory, [string[]] $Arguments)
{
	$output = @(& git -C $WorkingDirectory @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0)
	{
		throw "git $($Arguments -join ' ') failed: $($output -join '; ')."
	}
	return ,$output
}

function Get-GitText([string] $WorkingDirectory, [string[]] $Arguments)
{
	return ((Invoke-Git $WorkingDirectory $Arguments) -join "`n").Trim()
}

function Add-Commit([string] $Worktree, [string] $Name, [string] $Content)
{
	$path = Join-Path $Worktree $Name
	[IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
	[IO.File]::WriteAllText($path, $Content + "`n", [Text.UTF8Encoding]::new($false))
	Invoke-Git $Worktree @('add', '--', $Name) | Out-Null
	Invoke-Git $Worktree @('commit', '-m', "fixture $Name") | Out-Null
	return Get-GitText $Worktree @('rev-parse', 'HEAD')
}

function New-Session([string] $Name)
{
	$branch = 'fixture/finalize-approval-' + $Name + '-' + [guid]::NewGuid().ToString('N')
	$worktree = Join-Path $fixtureRoot ('session-' + $Name)
	Invoke-Git $fixturePrimary @('worktree', 'add', '-b', $branch, $worktree, $fixtureBaseline) | Out-Null
	$output = Join-Path $worktree 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	[IO.Directory]::CreateDirectory((Split-Path -Parent $output)) | Out-Null
	if (Test-Path -LiteralPath $output)
	{
		Remove-Item -LiteralPath $output -Force
	}
	New-Item -ItemType SymbolicLink -Path $output -Target (Join-Path $fixturePrimary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output') | Out-Null
	return [pscustomobject]@{ Name = $Name; Branch = $branch; Worktree = $worktree; ExtraBranches = [Collections.Generic.List[string]]::new() }
}

function Remove-Session($Session)
{
	if ($null -eq $Session)
	{
		return
	}
	if (Test-Path -LiteralPath $Session.Worktree)
	{
		Invoke-Git $fixturePrimary @('worktree', 'remove', '--force', $Session.Worktree) | Out-Null
	}
	foreach ($branch in @($Session.ExtraBranches))
	{
		Invoke-Git $fixturePrimary @('branch', '--delete', '--force', $branch) | Out-Null
	}
	Invoke-Git $fixturePrimary @('branch', '--delete', '--force', $Session.Branch) | Out-Null
}

function Write-ManifestReport([string] $Worktree, [string] $ComparisonBase, [string] $Purpose)
{
	$reportPath = (& pwsh -NoProfile -File $reportAllocator -Worktree $Worktree -Purpose $Purpose).Trim()
	$output = & pwsh -NoProfile -File $reportWriter `
		-ReportPath $reportPath `
		-Worktree $Worktree `
		-Baseline $ComparisonBase `
		-PlanIntent 'finalize approval preparation fixture' `
		-AcceptanceLedgerLine '- fixture | process | PASS | disposable repository state prepared' `
		-QueueReceiptOrResidualLine 'none' 2>$null
	Assert-Condition ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($output)) 'Verification report writer failed.'
	$result = $output | ConvertFrom-Json
	Assert-Condition ($result.status -ceq 'pass' -and $result.code -ceq 'ok') 'Verification report writer returned a non-passing result.'
	return [pscustomobject]@{ Path = $result.reportPath; Sha256 = $result.sha256; Range = $result.manifest.range }
}

function Invoke-Preparation($Session, $Report, [string] $PrimaryTip, [string] $SmartGitExecutable, [string] $Failure = 'none')
{
	$owner = [guid]::NewGuid().ToString()
	$module = Join-Path $Session.Worktree '.agents\scripts\WorktreeCliSessionExclusion.psm1'
	Import-Module $module -Force
	Register-WorktreeCliSession -RepositoryRoot $fixturePrimary -Owner $owner -Label "approval preparation $($Session.Name) fixture" -Worktree $Session.Worktree -LegacySessionsClosed | Out-Null
	$sessionTip = Get-GitText $Session.Worktree @('rev-parse', 'HEAD')
	$env:BROKEN_ENGINE_WORKTREE_PATH = $Session.Worktree
	$env:BROKEN_ENGINE_SESSION_BRANCH = $Session.Branch
	$env:BROKEN_ENGINE_PRIMARY_CHECKOUT = $fixturePrimary
	$env:BROKEN_ENGINE_TARGET_BRANCH = $fixturePrimaryBranch
	$env:BROKEN_ENGINE_BASELINE = $fixtureBaseline
	$env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER = $owner
	$env:BROKEN_ENGINE_FINALIZE_APPROVAL_PREPARATION_FIXTURE = '1'
	try
	{
		$arguments = @(
			'-CurrentWorktree', $Session.Worktree,
			'-PrimaryWorktree', $fixturePrimary,
			'-CurrentBranch', $Session.Branch,
			'-PrimaryBranch', $fixturePrimaryBranch,
			'-Baseline', $fixtureBaseline,
			'-ManifestComparisonBase', $PrimaryTip,
			'-ExpectedCurrentTip', $sessionTip,
			'-ExpectedPrimaryTip', $PrimaryTip,
			'-VerificationReportPath', $Report.Path,
			'-VerificationReportSha256', $Report.Sha256,
			'-ManifestRange', $Report.Range,
			'-SessionOwner', $owner,
			'-WaitSeconds', '1',
			'-FixtureSmartGitExecutable', $SmartGitExecutable,
			'-FixtureFailure', $Failure
		)
		$output = & pwsh -NoProfile -File $preparation @arguments 2>$null
		$exitCode = $LASTEXITCODE
		Assert-Condition (-not [string]::IsNullOrWhiteSpace($output)) 'Approval preparation returned no JSON.'
		return [pscustomobject]@{ ExitCode = $exitCode; Result = $output | ConvertFrom-Json; OriginalTip = $sessionTip }
	}
	finally
	{
		Unregister-WorktreeCliSession -RepositoryRoot $fixturePrimary -Owner $owner
	}
}

function Assert-Result($Fixture, [int] $ExitCode, [string] $Code)
{
	Assert-Condition ($Fixture.ExitCode -eq $ExitCode -and $Fixture.Result.code -ceq $Code) "Expected exit $ExitCode and '$Code'; got exit $($Fixture.ExitCode), '$($Fixture.Result.code)': $($Fixture.Result.message)"
}

foreach ($name in @(
	'BROKEN_ENGINE_WORKTREE_PATH',
	'BROKEN_ENGINE_SESSION_BRANCH',
	'BROKEN_ENGINE_PRIMARY_CHECKOUT',
	'BROKEN_ENGINE_TARGET_BRANCH',
	'BROKEN_ENGINE_BASELINE',
	'BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER',
	'BROKEN_ENGINE_FINALIZE_APPROVAL_PREPARATION_FIXTURE',
	'LOCALAPPDATA'
))
{
	$environmentBackup[$name] = [Environment]::GetEnvironmentVariable($name)
}

try
{
	$sourceWorktree = Get-GitText (Get-Location).Path @('rev-parse', '--show-toplevel')
	$sourceCommon = Get-GitText $sourceWorktree @('rev-parse', '--path-format=absolute', '--git-common-dir')
	$sourcePrimary = Split-Path -Parent $sourceCommon
	$sourceWorktreeCli = Join-Path $sourcePrimary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	Assert-Condition (Test-Path -LiteralPath $sourceWorktreeCli -PathType Leaf) "Fixture source WorktreeCli is missing: '$sourceWorktreeCli'."

	$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('BrokenEngineFinalizeApproval-' + [guid]::NewGuid().ToString('N'))
	Assert-TemporaryPath $fixtureRoot
	$env:LOCALAPPDATA = Join-Path $fixtureRoot 'LocalAppData'
	$fixturePrimary = Join-Path $fixtureRoot 'primary with space'
	[IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
	$cloneOutput = @(& git clone --quiet $sourcePrimary $fixturePrimary 2>&1)
	Assert-Condition ($LASTEXITCODE -eq 0) "Disposable fixture clone failed: $($cloneOutput -join '; ')."
	Invoke-Git $fixturePrimary @('config', 'user.name', 'Broken Engine Fixture') | Out-Null
	Invoke-Git $fixturePrimary @('config', 'user.email', 'fixture@example.invalid') | Out-Null
	$fixtureBaseline = Get-GitText $fixturePrimary @('rev-parse', 'HEAD')
	$fixturePrimaryBranch = Get-GitText $fixturePrimary @('branch', '--show-current')
	$primaryOutput = Join-Path $fixturePrimary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	[IO.Directory]::CreateDirectory($primaryOutput) | Out-Null
	Copy-Item -LiteralPath $sourceWorktreeCli -Destination (Join-Path $primaryOutput 'WorktreeCli.exe')

	$capturePath = Join-Path $fixtureRoot 'smartgit-arguments.txt'
	$fakeSmartGit = Join-Path $fixtureRoot 'fake-smartgit.cmd'
	$fakeSmartGitText = '@echo off' + "`r`n" + '>"' + $capturePath + '" echo %~1^|%~2^|%~3' + "`r`n"
	[IO.File]::WriteAllText($fakeSmartGit, $fakeSmartGitText, [Text.ASCIIEncoding]::new())
	$session = New-Session 'multi'
	try
	{
		Add-Commit $session.Worktree 'Fixture/a.txt' 'a' | Out-Null
		Add-Commit $session.Worktree 'Fixture/b.txt' 'b' | Out-Null
		Assert-Condition ((Get-GitText $session.Worktree @('rev-list', '--count', "$fixtureBaseline..HEAD")) -ceq '2') 'Multi-commit fixture setup did not create two session commits.'
		$originalTree = Get-GitText $session.Worktree @('rev-parse', 'HEAD^{tree}')
		$report = Write-ManifestReport $session.Worktree $fixtureBaseline 'finalize-approval-multi'
		$fixture = Invoke-Preparation $session $report $fixtureBaseline $fakeSmartGit
		Assert-Result $fixture 0 'ok'
		Assert-Condition ($fixture.Result.squash.disposition -ceq 'squashed' -and $fixture.Result.squash.originalTree -ceq $originalTree -and $fixture.Result.squash.approvedTree -ceq $originalTree) "Multi-commit fixture did not preserve the tree through squash: expected '$originalTree', disposition '$($fixture.Result.squash.disposition)', original '$($fixture.Result.squash.originalTree)', approved '$($fixture.Result.squash.approvedTree)'."
		Assert-Condition ((Get-GitText $session.Worktree @('rev-list', '--count', "$fixtureBaseline..HEAD")) -ceq '1') 'Multi-commit fixture did not produce one approval commit.'
		Assert-Condition ($fixture.Result.preflight.final.manifest.equal -eq $true) 'Multi-commit fixture did not preserve the verification manifest.'
		Assert-Condition (@($fixture.Result.smartGit.arguments).Count -eq 3 -and $fixture.Result.smartGit.arguments[0] -ceq '--log' -and $fixture.Result.smartGit.arguments[1] -ceq $fixturePrimary -and $fixture.Result.smartGit.arguments[2] -ceq "--anchor-commit=$($fixture.Result.tips.approvedSession)") 'SmartGit argument vector did not use primary and the squashed tip.'
		for ($attempt = 0; $attempt -lt 50 -and -not (Test-Path -LiteralPath $capturePath); ++$attempt) { Start-Sleep -Milliseconds 100 }
		Assert-Condition (Test-Path -LiteralPath $capturePath) 'Fake SmartGit launcher did not observe the argument vector.'
		$capture = (Get-Content -LiteralPath $capturePath -Raw).Trim()
		Assert-Condition ($capture -ceq "--log|$fixturePrimary|--anchor-commit=$($fixture.Result.tips.approvedSession)") "Fake SmartGit received split or reordered arguments: '$capture'."
	}
	finally { Remove-Session $session }

	$session = New-Session 'one'
	try
	{
		Add-Commit $session.Worktree 'Fixture/one.txt' 'one' | Out-Null
		$report = Write-ManifestReport $session.Worktree $fixtureBaseline 'finalize-approval-one'
		$fixture = Invoke-Preparation $session $report $fixtureBaseline ''
		Assert-Result $fixture 0 'ok'
		Assert-Condition ($fixture.Result.squash.disposition -ceq 'one-commit-no-op' -and $fixture.Result.tips.approvedSession -ceq $fixture.OriginalTip) 'One-commit fixture rewrote its candidate.'
		Assert-Condition ($fixture.Result.smartGit.status -ceq 'unavailable' -and -not [string]::IsNullOrWhiteSpace($fixture.Result.smartGit.manualCommand)) 'Missing SmartGit was not observable with a manual command.'
	}
	finally { Remove-Session $session }

	$session = New-Session 'dirty'
	try
	{
		Add-Commit $session.Worktree 'Fixture/dirty.txt' 'clean commit' | Out-Null
		$report = Write-ManifestReport $session.Worktree $fixtureBaseline 'finalize-approval-dirty'
		[IO.File]::WriteAllText((Join-Path $session.Worktree 'dirty.tmp'), 'dirty', [Text.UTF8Encoding]::new($false))
		$fixture = Invoke-Preparation $session $report $fixtureBaseline ''
		Assert-Result $fixture 2 'git.session-dirty'
		Assert-Condition ((Get-GitText $session.Worktree @('rev-parse', 'HEAD')) -ceq $fixture.OriginalTip) 'Dirty fixture moved the session ref.'
	}
	finally { Remove-Session $session }

	$session = New-Session 'merge'
	try
	{
		$sideBranch = 'fixture/finalize-approval-side-' + [guid]::NewGuid().ToString('N')
		$session.ExtraBranches.Add($sideBranch)
		Invoke-Git $session.Worktree @('switch', '-c', $sideBranch) | Out-Null
		Add-Commit $session.Worktree 'Fixture/side.txt' 'side' | Out-Null
		Invoke-Git $session.Worktree @('switch', $session.Branch) | Out-Null
		Add-Commit $session.Worktree 'Fixture/main.txt' 'main' | Out-Null
		Invoke-Git $session.Worktree @('merge', '--no-ff', $sideBranch, '-m', 'fixture merge') | Out-Null
		$report = Write-ManifestReport $session.Worktree $fixtureBaseline 'finalize-approval-merge'
		$fixture = Invoke-Preparation $session $report $fixtureBaseline ''
		Assert-Result $fixture 2 'git.session-range-has-merge'
		Assert-Condition ((Get-GitText $session.Worktree @('rev-parse', 'HEAD')) -ceq $fixture.OriginalTip) 'Merge fixture moved the session ref.'
	}
	finally { Remove-Session $session }

	foreach ($failure in @('compare-and-swap', 'postcondition'))
	{
		$session = New-Session $failure
		try
		{
			Add-Commit $session.Worktree "Fixture/$failure-a.txt" 'a' | Out-Null
			Add-Commit $session.Worktree "Fixture/$failure-b.txt" 'b' | Out-Null
			$report = Write-ManifestReport $session.Worktree $fixtureBaseline "finalize-approval-$failure"
			$fixture = Invoke-Preparation $session $report $fixtureBaseline '' $failure
			$expectedCode = if ($failure -ceq 'compare-and-swap') { 'git.compare-and-swap-failed' } else { 'git.fixture-postcondition-failed' }
			Assert-Result $fixture 2 $expectedCode
			Assert-Condition ((Get-GitText $session.Worktree @('rev-parse', 'HEAD')) -ceq $fixture.OriginalTip) "$failure fixture did not preserve the original ref."
			if ($failure -ceq 'postcondition')
			{
				Assert-Condition ($fixture.Result.squash.rollback -ceq 'restored-original') 'Postcondition fixture did not report successful rollback.'
			}
		}
		finally { Remove-Session $session }
	}

	$session = New-Session 'launch-failed'
	try
	{
		Add-Commit $session.Worktree 'Fixture/launch.txt' 'launch' | Out-Null
		$report = Write-ManifestReport $session.Worktree $fixtureBaseline 'finalize-approval-launch-failed'
		$fixture = Invoke-Preparation $session $report $fixtureBaseline $fakeSmartGit 'smartgit-launch'
		Assert-Result $fixture 0 'ok'
		Assert-Condition ($fixture.Result.smartGit.status -ceq 'failed' -and (Get-GitText $session.Worktree @('rev-parse', 'HEAD')) -ceq $fixture.OriginalTip) 'Failed SmartGit launch was not visible and non-mutating.'
	}
	finally { Remove-Session $session }

	$session = New-Session 'final-dirty'
	try
	{
		Add-Commit $session.Worktree 'Fixture/final-dirty.txt' 'final dirty' | Out-Null
		$report = Write-ManifestReport $session.Worktree $fixtureBaseline 'finalize-approval-final-dirty'
		$fixture = Invoke-Preparation $session $report $fixtureBaseline '' 'final-dirty'
		Assert-Result $fixture 2 'git.session-dirty-after-preflight'
		Assert-Condition ((Get-GitText $session.Worktree @('rev-parse', 'HEAD')) -ceq $fixture.OriginalTip) 'Final-dirty fixture moved the one-commit session ref.'
		Remove-Item -LiteralPath (Join-Path $session.Worktree 'fixture-final-dirty.tmp') -Force
	}
	finally { Remove-Session $session }

	$session = New-Session 'final-dirty-squash'
	try
	{
		Add-Commit $session.Worktree 'Fixture/final-dirty-squash-a.txt' 'a' | Out-Null
		Add-Commit $session.Worktree 'Fixture/final-dirty-squash-b.txt' 'b' | Out-Null
		$report = Write-ManifestReport $session.Worktree $fixtureBaseline 'finalize-approval-final-dirty-squash'
		$fixture = Invoke-Preparation $session $report $fixtureBaseline '' 'final-dirty'
		Assert-Result $fixture 2 'git.session-dirty-after-preflight'
		Assert-Condition ((Get-GitText $session.Worktree @('rev-parse', 'HEAD')) -ceq $fixture.OriginalTip) 'Final-dirty squash fixture did not restore the original session ref.'
		Assert-Condition ($fixture.Result.squash.rollback -ceq 'restored-original' -and $fixture.Result.squash.refUpdated -eq $false -and $fixture.Result.tips.approvedSession -ceq $fixture.OriginalTip) 'Final-dirty squash fixture misreported successful ref rollback.'
		Remove-Item -LiteralPath (Join-Path $session.Worktree 'fixture-final-dirty.tmp') -Force
	}
	finally { Remove-Session $session }

	$session = New-Session 'divergent'
	try
	{
		Add-Commit $session.Worktree 'Fixture/session.txt' 'session' | Out-Null
		Add-Commit $fixturePrimary 'Fixture/primary.txt' 'primary' | Out-Null
		$divergentPrimaryTip = Get-GitText $fixturePrimary @('rev-parse', 'HEAD')
		$report = Write-ManifestReport $session.Worktree $divergentPrimaryTip 'finalize-approval-divergent'
		$fixture = Invoke-Preparation $session $report $divergentPrimaryTip ''
		Assert-Result $fixture 2 'preflight.git.session-not-rebased'
		Assert-Condition ((Get-GitText $session.Worktree @('rev-parse', 'HEAD')) -ceq $fixture.OriginalTip) 'Divergent fixture moved the session ref.'
	}
	finally { Remove-Session $session }

	[ordered]@{
		schemaVersion = 'broken-engine-finalize-approval-preparation-fixtures/v1'
		status = 'pass'
		cases = @('multi-commit-squash', 'one-commit-no-op', 'dirty-block', 'merge-block', 'compare-and-swap-failure', 'postcondition-rollback', 'smartgit-launch-failed', 'final-dirty-block', 'final-dirty-squash-rollback', 'divergent-block')
	} | ConvertTo-Json -Depth 4 -Compress
	exit 0
}
finally
{
	if ($null -ne $fixturePrimary -and (Test-Path -LiteralPath $fixturePrimary))
	{
		try { Get-WorktreeCliExclusionStatus -RepositoryRoot $fixturePrimary | Out-Null } catch { Write-Warning "Could not clean fixture session claims: $($_.Exception.Message)" }
	}
	if ($null -ne $fixtureRoot -and (Test-Path -LiteralPath $fixtureRoot))
	{
		Assert-TemporaryPath $fixtureRoot
		Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
	}
	foreach ($entry in $environmentBackup.GetEnumerator())
	{
		if ($null -eq $entry.Value) { Remove-Item "Env:$($entry.Key)" -ErrorAction SilentlyContinue }
		else { Set-Item "Env:$($entry.Key)" $entry.Value }
	}
}
