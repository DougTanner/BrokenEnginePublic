[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$preflight = Join-Path $PSScriptRoot 'Test-FinalizePreflight.ps1'
$writer = Join-Path $PSScriptRoot '..\..\..\scripts\Write-AgentVerificationReport.ps1'
$reportAllocator = Join-Path $PSScriptRoot '..\..\..\scripts\New-AgentReportPath.ps1'
$fixtureRoot = $null
$fixturePrimary = $null
$fixtureSession = $null
$fixtureBranch = $null
$fixtureClaimOwner = $null
$fixtureClaimRegistered = $false
$fixtureModule = $null

function Assert-Condition([bool] $Condition, [string] $Message) {
	if (-not $Condition) { throw $Message }
}

function Invoke-Git([string] $WorkingDirectory, [string[]] $Arguments) {
	$output = @(& git -C $WorkingDirectory @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')." }
	return ,$output
}

function Assert-TemporaryPath([string] $Path) {
	$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
	$candidate = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
	$prefix = $tempRoot + [IO.Path]::DirectorySeparatorChar
	if (-not $candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Fixture cleanup path escapes the system temporary directory: '$candidate'."
	}
}

function Invoke-CheckpointFixture([string] $Checkpoint) {
	$output = & pwsh -NoProfile -File $preflight -Mode primary-commit -Checkpoint $Checkpoint 2>$null
	$exitCode = $LASTEXITCODE
	if ([string]::IsNullOrWhiteSpace($output)) { throw "Preflight returned no JSON for checkpoint '$Checkpoint'." }
	$result = $output | ConvertFrom-Json
	return [pscustomobject]@{ ExitCode = $exitCode; Result = $result }
}

function Set-SessionOutputLink([string] $Target) {
	$sessionOutput = Join-Path $fixtureSession 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	if (Test-Path -LiteralPath $sessionOutput) { Remove-Item -LiteralPath $sessionOutput -Force }
	New-Item -ItemType SymbolicLink -Path $sessionOutput -Target $Target | Out-Null
}

function Write-ManifestReport([string] $ComparisonBase) {
	$relativeFile = 'Fixture Files/space name.txt'
	$absoluteFile = Join-Path $fixtureSession ($relativeFile.Replace('/', [IO.Path]::DirectorySeparatorChar))
	New-Item -ItemType Directory -Path (Split-Path -Parent $absoluteFile) -Force | Out-Null
	[IO.File]::WriteAllText($absoluteFile, 'fixture' + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
	Remove-Item -LiteralPath (Join-Path $fixtureSession '.editorconfig') -Force

	$reportPath = (& pwsh -NoProfile -File $reportAllocator -Worktree $fixtureSession -Purpose 'finalize-preflight-fixture').Trim()
	$output = & pwsh -NoProfile -File $writer `
		-ReportPath $reportPath `
		-Worktree $fixtureSession `
		-Baseline $ComparisonBase `
		-PlanIntent 'finalize preflight fixture' `
		-AcceptanceLedgerLine '- fixture | writer | PASS | manifest is generated' `
		-QueueReceiptOrResidualLine 'none' 2>$null
	if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($output)) { throw 'Verification report writer failed for fixture manifest.' }
	$writerResult = $output | ConvertFrom-Json
	Assert-Condition ($writerResult.status -ceq 'pass' -and $writerResult.code -ceq 'ok') 'Verification report writer returned a non-passing fixture result.'
	$manifestText = Get-Content -LiteralPath $reportPath -Raw
	Assert-Condition ($manifestText.Contains(".editorconfig$([char]9)DELETED", [StringComparison]::Ordinal)) 'Writer did not emit a real tab-delimited deletion row.'
	Assert-Condition (-not $manifestText.Contains('\t', [StringComparison]::Ordinal)) 'Writer emitted a literal backslash-t sequence.'
	return [pscustomobject]@{ Path = $writerResult.reportPath; Sha256 = $writerResult.sha256; Range = $writerResult.manifest.range; ComparisonBase = $ComparisonBase }
}

function Write-EmbeddedTabManifestReport([string] $ComparisonBase) {
	$reportPath = (& pwsh -NoProfile -File $reportAllocator -Worktree $fixtureSession -Purpose 'finalize-preflight-tabbed').Trim()
	$rows = @(
		('.editorconfig' + [char]9 + 'DELETED'),
		('Fixture Files/embedded' + [char]9 + 'tab.txt' + [char]9 + 'DELETED')
	)
	[IO.File]::WriteAllText($reportPath, ($rows -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
	$hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([IO.File]::ReadAllBytes($reportPath))).ToLowerInvariant()
	return [pscustomobject]@{ Path = $reportPath; Sha256 = $hash; Range = 'L1-L2'; ComparisonBase = $ComparisonBase }
}

function Write-LiteralTabEscapeManifestReport([string] $ComparisonBase) {
	$reportPath = (& pwsh -NoProfile -File $reportAllocator -Worktree $fixtureSession -Purpose 'finalize-preflight-literal-tab').Trim()
	[IO.File]::WriteAllText($reportPath, '.editorconfig\tDELETED' + "`n", [Text.UTF8Encoding]::new($false))
	$hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([IO.File]::ReadAllBytes($reportPath))).ToLowerInvariant()
	return [pscustomobject]@{ Path = $reportPath; Sha256 = $hash; Range = 'L1-L1'; ComparisonBase = $ComparisonBase }
}

function Invoke-SessionPreflight($Manifest, [string] $Owner) {
	$output = & pwsh -NoProfile -File $preflight `
		-Mode session-landing `
		-Checkpoint initial `
		-CurrentWorktree $fixtureSession.ToUpperInvariant().Replace('\', '/') `
		-PrimaryWorktree $fixturePrimary.Replace('\', '/') `
		-CurrentBranch $fixtureBranch `
		-PrimaryBranch $fixturePrimaryBranch `
		-Baseline $fixtureBaseline `
		-ManifestComparisonBase $Manifest.ComparisonBase `
		-VerificationReportPath $Manifest.Path.Replace('\', '/') `
		-VerificationReportSha256 $Manifest.Sha256 `
		-ManifestRange $Manifest.Range `
		-SessionOwner $Owner `
		-WaitSeconds 60 `
		-HasPlanRowClaim `
		-HasCompletedPlanClaim `
		-QueueChangingLanding 2>$null
	$exitCode = $LASTEXITCODE
	if ([string]::IsNullOrWhiteSpace($output)) { throw 'Session preflight returned no JSON.' }
	return [pscustomobject]@{ ExitCode = $exitCode; Result = $output | ConvertFrom-Json }
}

function Assert-PreflightResult($Fixture, [int] $ExpectedExitCode, [string] $ExpectedCode) {
	if ($Fixture.ExitCode -ne $ExpectedExitCode -or $Fixture.Result.code -cne $ExpectedCode) {
		throw "Preflight expected exit $ExpectedExitCode and code '$ExpectedCode'; got exit $($Fixture.ExitCode), status '$($Fixture.Result.status)', code '$($Fixture.Result.code)', message '$($Fixture.Result.message)'."
	}
}

foreach ($checkpoint in @('after-lock', 'after-approval', 'pre-approval-lock-release', 'pre-claim-release')) {
	$fixture = Invoke-CheckpointFixture $checkpoint
	if ($fixture.ExitCode -ne 1 -or $fixture.Result.code -cne 'input.invalid' -or $fixture.Result.message -notmatch 'Checkpoint is invalid') {
		throw "Retired checkpoint '$checkpoint' was not rejected as invalid."
	}
}

foreach ($checkpoint in @('initial', 'after-reconciliation', 'pre-mutation', 'post-mutation')) {
	$fixture = Invoke-CheckpointFixture $checkpoint
	if ($fixture.ExitCode -ne 1 -or $fixture.Result.code -cne 'input.invalid' -or $fixture.Result.message -match 'Checkpoint is invalid') {
		throw "Supported checkpoint '$checkpoint' was not accepted before required-input validation."
	}
}

$environmentBackup = @{}
foreach ($name in @(
	'BROKEN_ENGINE_WORKTREE_PATH',
	'BROKEN_ENGINE_SESSION_BRANCH',
	'BROKEN_ENGINE_PRIMARY_CHECKOUT',
	'BROKEN_ENGINE_TARGET_BRANCH',
	'BROKEN_ENGINE_BASELINE',
	'BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER',
	'LOCALAPPDATA'
)) {
	$environmentBackup[$name] = [Environment]::GetEnvironmentVariable($name)
}

try {
	$sourceWorktree = (Invoke-Git (Get-Location).Path @('rev-parse', '--show-toplevel'))[0].Trim()
	$sourceCommonDirectory = (Invoke-Git $sourceWorktree @('rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim()
	$sourcePrimary = Split-Path -Parent $sourceCommonDirectory
	$sourcePrimaryBranch = (Invoke-Git $sourcePrimary @('branch', '--show-current'))[0].Trim()
	Assert-Condition (-not [string]::IsNullOrWhiteSpace($sourcePrimaryBranch)) 'Fixture source primary checkout has no attached branch.'
	$sourceWorktreeCli = Join-Path $sourcePrimary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	Assert-Condition (Test-Path -LiteralPath $sourceWorktreeCli -PathType Leaf) "Fixture source WorktreeCli is missing: '$sourceWorktreeCli'."

	$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('BrokenEngineFinalizePreflight-' + [guid]::NewGuid().ToString())
	Assert-TemporaryPath $fixtureRoot
	$env:LOCALAPPDATA = Join-Path $fixtureRoot 'LocalAppData'
	$fixturePrimary = Join-Path $fixtureRoot 'primary'
	$fixtureSession = Join-Path $fixtureRoot 'session'
	$fixtureBranch = 'fixture/finalize-preflight-' + [guid]::NewGuid().ToString('N')
	New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
	$cloneOutput = @(& git clone --branch $sourcePrimaryBranch $sourcePrimary $fixturePrimary 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "Disposable fixture clone failed: $($cloneOutput -join '; ')." }
	$fixtureBaseline = (Invoke-Git $fixturePrimary @('rev-parse', 'HEAD'))[0].Trim()
	$fixturePrimaryBranch = (Invoke-Git $fixturePrimary @('branch', '--show-current'))[0].Trim()
	Invoke-Git $fixturePrimary @('worktree', 'add', '-b', $fixtureBranch, $fixtureSession, $fixtureBaseline) | Out-Null

	$fixturePrimaryOutput = Join-Path $fixturePrimary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	$fixtureSessionOutput = Join-Path $fixtureSession 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	New-Item -ItemType Directory -Path $fixturePrimaryOutput -Force | Out-Null
	Copy-Item -LiteralPath $sourceWorktreeCli -Destination (Join-Path $fixturePrimaryOutput 'WorktreeCli.exe')
	New-Item -ItemType Directory -Path (Split-Path -Parent $fixtureSessionOutput) -Force | Out-Null
	Set-SessionOutputLink $fixturePrimaryOutput

	$fixtureModule = Join-Path $fixtureSession '.agents\scripts\WorktreeCliSessionExclusion.psm1'
	Copy-Item -LiteralPath (Join-Path $sourceWorktree '.agents\scripts\WorktreeCliSessionExclusion.psm1') -Destination $fixtureModule
	foreach ($scriptName in @('AgentArtifactStore.psm1', 'Read-AgentReportSection.ps1')) {
		Copy-Item -LiteralPath (Join-Path $sourceWorktree ('.agents\scripts\' + $scriptName)) -Destination (Join-Path $fixtureSession ('.agents\scripts\' + $scriptName))
	}
	Import-Module $fixtureModule -Force
	$fixtureClaimOwner = [guid]::NewGuid().ToString()
	Register-WorktreeCliSession -RepositoryRoot $fixturePrimary -Owner $fixtureClaimOwner -Label 'finalize preflight fixture' -Worktree $fixtureSession -LegacySessionsClosed | Out-Null
	$fixtureClaimRegistered = $true
	$env:BROKEN_ENGINE_WORKTREE_PATH = $fixtureSession
	$env:BROKEN_ENGINE_SESSION_BRANCH = $fixtureBranch
	$env:BROKEN_ENGINE_PRIMARY_CHECKOUT = $fixturePrimary
	$env:BROKEN_ENGINE_TARGET_BRANCH = $fixturePrimaryBranch
	$env:BROKEN_ENGINE_BASELINE = $fixtureBaseline
	$env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER = $fixtureClaimOwner
	$manifest = Write-ManifestReport $fixtureBaseline

	$pass = Invoke-SessionPreflight $manifest $fixtureClaimOwner
	Assert-PreflightResult $pass 0 'ok'
	Assert-Condition ($pass.Result.manifest.equal -and $pass.Result.worktreeCli.capabilityResult -ceq 'pass' -and $pass.Result.claim.classification -ceq 'expected-live') 'Mixed-path session preflight did not preserve manifest, WorktreeCli, and live-claim evidence.'
	$expectedTargetBranch = $env:BROKEN_ENGINE_TARGET_BRANCH
	try {
		$env:BROKEN_ENGINE_TARGET_BRANCH = $expectedTargetBranch + '-mismatch'
		Assert-PreflightResult (Invoke-SessionPreflight $manifest $fixtureClaimOwner) 1 'input.invalid'
	}
	finally {
		$env:BROKEN_ENGINE_TARGET_BRANCH = $expectedTargetBranch
	}
	Assert-PreflightResult (Invoke-SessionPreflight (Write-EmbeddedTabManifestReport $fixtureBaseline) $fixtureClaimOwner) 2 'manifest.mismatch'
	Assert-PreflightResult (Invoke-SessionPreflight (Write-LiteralTabEscapeManifestReport $fixtureBaseline) $fixtureClaimOwner) 2 'manifest.row-malformed'

	$wrongOutput = Join-Path $fixtureRoot 'wrong-output'
	New-Item -ItemType Directory -Path $wrongOutput | Out-Null
	Set-SessionOutputLink $wrongOutput
	Assert-PreflightResult (Invoke-SessionPreflight $manifest $fixtureClaimOwner) 2 'worktreecli.output-wrong-target'
	Set-SessionOutputLink $fixturePrimaryOutput

	Remove-Item -LiteralPath (Join-Path $fixturePrimaryOutput 'WorktreeCli.exe') -Force
	Assert-PreflightResult (Invoke-SessionPreflight $manifest $fixtureClaimOwner) 2 'worktreecli.executable-missing'
	Copy-Item -LiteralPath $sourceWorktreeCli -Destination (Join-Path $fixturePrimaryOutput 'WorktreeCli.exe')

	Unregister-WorktreeCliSession -RepositoryRoot $fixturePrimary -Owner $fixtureClaimOwner
	$fixtureClaimRegistered = $false
	Register-WorktreeCliSession -RepositoryRoot $fixturePrimary -Owner $fixtureClaimOwner -Label 'finalize preflight mismatch fixture' -Worktree $fixturePrimary | Out-Null
	$fixtureClaimRegistered = $true
	Assert-PreflightResult (Invoke-SessionPreflight $manifest $fixtureClaimOwner) 2 'claim.mismatch'
	Unregister-WorktreeCliSession -RepositoryRoot $fixturePrimary -Owner $fixtureClaimOwner
	$fixtureClaimRegistered = $false

	$staleOwner = [guid]::NewGuid().ToString()
	$staleRegistrar = Join-Path $fixtureRoot 'RegisterStaleClaim.ps1'
	[IO.File]::WriteAllText($staleRegistrar, @"
param([string] `$Module, [string] `$Repository, [string] `$Owner, [string] `$Worktree)
Import-Module `$Module -Force
Register-WorktreeCliSession -RepositoryRoot `$Repository -Owner `$Owner -Label 'finalize preflight stale fixture' -Worktree `$Worktree | Out-Null
"@, [Text.UTF8Encoding]::new($false))
	& pwsh -NoProfile -File $staleRegistrar -Module $fixtureModule -Repository $fixturePrimary -Owner $staleOwner -Worktree $fixtureSession
	Assert-Condition ($LASTEXITCODE -eq 0) 'Stale-claim registrar failed.'
	$env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER = $staleOwner
	Assert-PreflightResult (Invoke-SessionPreflight $manifest $staleOwner) 2 'claim.stale'
	$fixtureClaimOwner = [guid]::NewGuid().ToString()
	Register-WorktreeCliSession -RepositoryRoot $fixturePrimary -Owner $fixtureClaimOwner -Label 'finalize preflight stale cleanup fixture' -Worktree $fixtureSession | Out-Null
	$fixtureClaimRegistered = $true
	$persistedLedger = Get-Content -Raw -LiteralPath (Get-WorktreeCliRepositoryIdentity $fixturePrimary).LedgerPath | ConvertFrom-Json
	Assert-Condition (@($persistedLedger.sessions | Where-Object { $_.owner -ceq $staleOwner }).Count -eq 0) 'Dead fixture wrapper claim was not removed by the next persisted session registration.'

	$operationMarker = (Invoke-Git $fixtureSession @('rev-parse', '--git-path', 'MERGE_HEAD'))[0].Trim()
	[IO.File]::WriteAllText($operationMarker, 'fixture', [Text.UTF8Encoding]::new($false))
	Assert-PreflightResult (Invoke-SessionPreflight $manifest $staleOwner) 2 'git.operation-active'
	Remove-Item -LiteralPath $operationMarker -Force
}
finally {
	if ($fixtureClaimRegistered) {
		try { Unregister-WorktreeCliSession -RepositoryRoot $fixturePrimary -Owner $fixtureClaimOwner } catch { Write-Warning "Could not release fixture wrapper claim '$fixtureClaimOwner': $($_.Exception.Message)" }
	}
	if ($null -ne $fixturePrimary -and (Test-Path -LiteralPath $fixturePrimary)) {
		try { Get-WorktreeCliExclusionStatus -RepositoryRoot $fixturePrimary | Out-Null } catch { Write-Warning "Could not clean stale fixture claims: $($_.Exception.Message)" }
	}
	if ($null -ne $fixtureSession -and (Test-Path -LiteralPath $fixtureSession)) {
		try { Invoke-Git $fixturePrimary @('worktree', 'remove', '--force', $fixtureSession) | Out-Null } catch { Write-Warning "Could not remove disposable fixture worktree '$fixtureSession': $($_.Exception.Message)" }
	}
	if ($null -ne $fixturePrimary -and (Test-Path -LiteralPath $fixturePrimary) -and -not [string]::IsNullOrWhiteSpace($fixtureBranch)) {
		try { Invoke-Git $fixturePrimary @('branch', '--delete', '--force', $fixtureBranch) | Out-Null } catch { Write-Warning "Could not delete disposable fixture branch '$fixtureBranch': $($_.Exception.Message)" }
	}
	if ($null -ne $fixtureRoot -and (Test-Path -LiteralPath $fixtureRoot)) {
		Assert-TemporaryPath $fixtureRoot
		Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
	}
	foreach ($entry in $environmentBackup.GetEnumerator()) {
		if ($null -eq $entry.Value) { Remove-Item "Env:$($entry.Key)" -ErrorAction SilentlyContinue }
		else { Set-Item "Env:$($entry.Key)" $entry.Value }
	}
}

Write-Output 'PASS: finalization preflight disposable repository and worktree fixtures passed.'
$global:LASTEXITCODE = 0
