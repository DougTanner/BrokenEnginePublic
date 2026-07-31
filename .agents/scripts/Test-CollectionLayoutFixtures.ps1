# Deterministic fixtures for Test-CollectionLayout.ps1: one clean collection, one failing case
# per audited rule, three unparseable cases asserting the blocked outcome, one empty-input case
# asserting the error outcome, and one over-cap case asserting output truncation. Every case
# asserts status, code, exit code, and the complete emitted rule sequence with its total count.
# Every fixture is a self-contained temporary header plus a substitute Frame::kiVersion sum,
# created and removed by this runner. Run whenever Test-CollectionLayout.ps1 changes.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:AuditorScript = Join-Path $PSScriptRoot 'Test-CollectionLayout.ps1'
$script:Root = Join-Path ([IO.Path]::GetTempPath()) "BrokenEngineCollectionLayoutFixtures\$([guid]::NewGuid().ToString('N').Substring(0, 8))"
$script:Failures = [Collections.Generic.List[string]]::new()
$script:CaseCount = 0

function Assert-True([bool] $Condition, [string] $Name)
{
	if (-not $Condition) { $null = $script:Failures.Add($Name); Write-Host "FAIL $Name" } else { Write-Host "pass $Name" }
}

function New-FixtureHeader([string] $Body)
{
	return @"
#pragma once

namespace engine
{

struct FixtureInterpolate : public Collection<FixtureInterpolate, CollectionFlags::kIdToIndex>
{
	static constexpr int64_t kiVersion = 1;

	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

$Body
};

} // namespace engine
"@
}

function Invoke-Case([string] $Name, [string] $Body, [string[]] $SumTerms, [string] $PathArgument = '')
{
	$script:CaseCount++
	$caseRoot = Join-Path $script:Root $Name
	$null = New-Item -ItemType Directory -Force $caseRoot
	$headerPath = Join-Path $caseRoot 'Fixture.h'
	$framePath = Join-Path $caseRoot 'Frame.cpp'
	[IO.File]::WriteAllText($headerPath, (New-FixtureHeader $Body))
	$terms = ($SumTerms | ForEach-Object { " + $_" }) -join ''
	[IO.File]::WriteAllText($framePath, "namespace game`n{`n`nconst int64_t Frame::kiVersion = 122 + engine::kiNavDataVersion$terms;`n`n}`n")

	$pathValue = $(if ([string]::IsNullOrEmpty($PathArgument)) { $headerPath } else { $PathArgument })
	$stdout = @(& pwsh -NoProfile -ExecutionPolicy Bypass -File $script:AuditorScript -Path $pathValue -FrameSource $framePath 2>$null)
	$exitCode = $LASTEXITCODE
	$text = ($stdout -join '').Trim()
	$json = $null
	try { $json = $text | ConvertFrom-Json -Depth 16 -ErrorAction Stop } catch { }
	return [pscustomobject]@{
		Name = $Name
		ExitCode = $exitCode
		Text = $text
		Json = $json
		Bytes = [Text.Encoding]::UTF8.GetByteCount($text)
	}
}

function Assert-Case($Result, [string] $ExpectedStatus, [int] $ExpectedExit, [string] $ExpectedCode, [string[]] $ExpectedRules, [int] $ExpectedTotal = -1)
{
	$name = $Result.Name
	Assert-True ($null -ne $Result.Json) "$name stdout parses as one JSON object"
	if ($null -eq $Result.Json) { return }
	Assert-True ($Result.Json.schemaVersion -ceq 'broken-engine-collection-layout/v1') "$name schemaVersion"
	Assert-True ($Result.Json.status -ceq $ExpectedStatus) "$name status=$ExpectedStatus (was $($Result.Json.status))"
	Assert-True ($Result.Json.code -ceq $ExpectedCode) "$name code=$ExpectedCode (was $($Result.Json.code))"
	Assert-True ($Result.ExitCode -eq $ExpectedExit) "$name exit=$ExpectedExit (was $($Result.ExitCode))"
	# The complete emitted sequence, so a rule reported the wrong number of times fails.
	$rules = @($Result.Json.violations.items | ForEach-Object { $_.rule })
	$expected = @($ExpectedRules)
	Assert-True (($rules -join ',') -ceq ($expected -join ',')) "$name rules=[$($expected -join ',')] (was [$($rules -join ',')])"
	$expectedTotal = $(if ($ExpectedTotal -lt 0) { $expected.Count } else { $ExpectedTotal })
	Assert-True ($Result.Json.violations.totalCount -eq $expectedTotal) "$name totalCount=$expectedTotal (was $($Result.Json.violations.totalCount))"
	Assert-True ($Result.Bytes -le 8192) "$name stdout within the byte cap"
}

# Split shape with a C-array-of-pointer column, a CRC subset that deliberately repeats a shared
# column, and a persistent subset. The repeated pVecPositions must not read as a duplicate.
$cleanBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;
	float* pfTrailTimes[4] = {};
#if defined(BT_CLIENT)
	float* __restrict pfClientOnly = nullptr;
#endif

	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pfIntensities, rSelf.pfTrailTimes);
	}
#if defined(BT_CLIENT)
	auto ClientMembers(this auto&& rSelf) { return std::tie(rSelf.pfClientOnly); }
#endif
	auto Members(this auto&& rSelf)
	{
#if defined(BT_CLIENT)
		return std::tuple_cat(rSelf.SharedMembers(), rSelf.ClientMembers());
#else
		return rSelf.SharedMembers();
#endif
	}
	auto SharedCrcMembers(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }
	auto PersistentMembers(this auto&& rSelf) { return std::tie(rSelf.pfIntensities); }
'@

# A declared column absent from the effective Members() tuple.
$missingBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;

	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }
'@

# The same column listed twice in the effective Members() tuple.
$duplicateBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;

	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pVecPositions, rSelf.pfIntensities); }
'@

# A tuple entry left behind after its column was removed, so it names nothing declared.
$unresolvedBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;

	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pfRemoved); }
'@

# The split accessors duplicate a column instead of partitioning it. Members() lists the columns
# directly so the overlap does not also register as a Members() duplicate.
$overlapBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;

	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }
	auto ClientMembers(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pfIntensities); }
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pfIntensities); }
'@

# The split accessors do not cover everything the effective Members() tuple yields.
$partitionBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;

	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pfIntensities); }
'@

# The shared accessor lists a column twice while the effective Members() tuple lists it once, so
# containment alone would not see the mismatch.
$multiplicityBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;

	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pVecPositions, rSelf.pfIntensities); }
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pfIntensities); }
'@

# A CRC entry left behind after its column was removed, so it no longer resolves in SharedMembers.
$sharedCrcBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;

	auto SharedMembers(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }
	auto Members(this auto&& rSelf) { return rSelf.SharedMembers(); }
	auto SharedCrcMembers(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pfRemoved); }
'@

# A persistent entry left behind after its column was removed.
$persistentBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;

	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }
	auto PersistentMembers(this auto&& rSelf) { return std::tie(rSelf.pfRemoved); }
'@

# A BT_CLIENT-guarded column smuggled into the shared tuple under its own guard.
$clientGuardedBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;
#if defined(BT_CLIENT)
	float* __restrict pfClientOnly = nullptr;
#endif

	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions
#if defined(BT_CLIENT)
			, rSelf.pfClientOnly
#endif
		);
	}
	auto Members(this auto&& rSelf) { return rSelf.SharedMembers(); }
'@

# An unguarded column listed under a BT_CLIENT guard.
$parityBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;
	float* __restrict pfIntensities = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions
#if defined(BT_CLIENT)
			, rSelf.pfIntensities
#endif
		);
	}
'@

# An accessor shape the parser cannot resolve.
$unparseableShapeBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;

	auto Members(this auto&& rSelf) { return MakeMembers(rSelf); }
'@

# A tuple argument that passes one element instead of a whole column.
$unparseableEntryBody = @'
	XMVECTOR* pVecVisiblePositions[4] = {};

	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecVisiblePositions[0]); }
'@

# A guard form the parser cannot resolve.
$unparseableGuardBody = @'
	XMVECTOR* __restrict pVecPositions = nullptr;
#ifdef BT_CLIENT
#endif

	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions); }
'@

$overCapDeclarations = ((0..39) | ForEach-Object { "`tfloat* __restrict pfColumn$_ = nullptr;" }) -join "`n"
$overCapBody = "$overCapDeclarations`n`n`tauto Members(this auto&& rSelf) { return std::tie(); }"

try
{
	$null = New-Item -ItemType Directory -Force $script:Root

	Assert-Case (Invoke-Case 'clean' $cleanBody @('FixtureInterpolate::kiVersion')) 'pass' 0 'ok' @()
	Assert-Case (Invoke-Case 'members-missing' $missingBody @('FixtureInterpolate::kiVersion')) 'failed' 1 'layout.violations' @('members.missing')
	Assert-Case (Invoke-Case 'members-duplicate' $duplicateBody @('FixtureInterpolate::kiVersion')) 'failed' 1 'layout.violations' @('members.duplicate')
	Assert-Case (Invoke-Case 'members-unresolved' $unresolvedBody @('FixtureInterpolate::kiVersion')) 'failed' 1 'layout.violations' @('members.unresolved')
	Assert-Case (Invoke-Case 'partition-overlap' $overlapBody @('FixtureInterpolate::kiVersion')) 'failed' 1 'layout.violations' @('partition.overlap', 'partition.mismatch')
	Assert-Case (Invoke-Case 'partition-mismatch' $partitionBody @('FixtureInterpolate::kiVersion')) 'failed' 1 'layout.violations' @('partition.mismatch')
	Assert-Case (Invoke-Case 'partition-multiplicity' $multiplicityBody @('FixtureInterpolate::kiVersion')) 'failed' 1 'layout.violations' @('partition.mismatch')
	Assert-Case (Invoke-Case 'sharedcrc-not-subset' $sharedCrcBody @('FixtureInterpolate::kiVersion')) 'failed' 1 'layout.violations' @('sharedcrc.not-subset')
	Assert-Case (Invoke-Case 'persistent-not-subset' $persistentBody @('FixtureInterpolate::kiVersion')) 'failed' 1 'layout.violations' @('persistent.not-subset')
	Assert-Case (Invoke-Case 'shared-client-guarded' $clientGuardedBody @('FixtureInterpolate::kiVersion')) 'failed' 1 'layout.violations' @('shared.client-guarded')
	Assert-Case (Invoke-Case 'guard-parity' $parityBody @('FixtureInterpolate::kiVersion')) 'failed' 1 'layout.violations' @('guard.parity-mismatch')
	Assert-Case (Invoke-Case 'version-term-missing' $cleanBody @()) 'failed' 1 'layout.violations' @('version.term-missing')
	Assert-Case (Invoke-Case 'version-term-unresolved' $cleanBody @('FixtureInterpolate::kiVersion', 'GhostPostRender::kiVersion')) 'failed' 1 'layout.violations' @('version.term-unresolved')
	Assert-Case (Invoke-Case 'unparseable-shape' $unparseableShapeBody @('FixtureInterpolate::kiVersion')) 'blocked' 2 'parse.blocked' @('parse.unrecognized-shape')
	Assert-Case (Invoke-Case 'unparseable-entry' $unparseableEntryBody @('FixtureInterpolate::kiVersion')) 'blocked' 2 'parse.blocked' @('parse.unrecognized-entry')
	Assert-Case (Invoke-Case 'unparseable-guard' $unparseableGuardBody @('FixtureInterpolate::kiVersion')) 'blocked' 2 'parse.blocked' @('parse.unrecognized-guard', 'parse.unrecognized-guard')
	Assert-Case (Invoke-Case 'empty-path' $cleanBody @('FixtureInterpolate::kiVersion') ';') 'error' 1 'input.path-invalid' @()

	# The emitted item count is whatever the byte cap allows for this machine's temporary path length,
	# so this case pins the rule of every emitted item, the item-count cap, and the counters instead.
	$overCap = Invoke-Case 'over-cap' $overCapBody @('FixtureInterpolate::kiVersion')
	$overCapItems = $(if ($null -eq $overCap.Json) { @() } else { @($overCap.Json.violations.items) })
	Assert-Case $overCap 'failed' 1 'layout.violations' @($overCapItems | ForEach-Object { 'members.missing' }) 40
	if ($null -ne $overCap.Json)
	{
		Assert-True ($overCapItems.Count -gt 0 -and $overCapItems.Count -le 32) "over-cap emitted 1..32 items (was $($overCapItems.Count))"
		Assert-True ($overCap.Json.violations.totalCount -eq 40) "over-cap totalCount=40 (was $($overCap.Json.violations.totalCount))"
		Assert-True ($overCap.Json.violations.truncated -eq $true) 'over-cap truncation flag set'
		Assert-True ($overCap.Json.violations.omittedCount -eq ($overCap.Json.violations.totalCount - $overCapItems.Count)) 'over-cap omittedCount matches emitted items'
	}
}
finally
{
	Remove-Item -LiteralPath $script:Root -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "cases=$script:CaseCount failures=$($script:Failures.Count)"
if ($script:Failures.Count -gt 0)
{
	foreach ($failure in $script:Failures) { Write-Host "  $failure" }
	exit 1
}

exit 0
