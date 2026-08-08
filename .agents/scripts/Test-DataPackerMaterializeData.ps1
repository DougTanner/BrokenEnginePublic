[CmdletBinding()]
param(
	[string] $DataPackerExecutable,
	[string] $RepositoryRoot = (Join-Path $PSScriptRoot '..\..')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-True([bool] $Condition, [string] $Message) {
	if (-not $Condition) { throw "ASSERT: $Message" }
}

function Invoke-Git([string[]] $Arguments) {
	$output = @(& git.exe @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
}

function Get-DirectoryBytes([string] $Path) {
	$entries = foreach ($file in @(Get-ChildItem -LiteralPath $Path -File -Recurse | Sort-Object FullName)) {
		$relative = [IO.Path]::GetRelativePath($Path, $file.FullName).Replace('\', '/')
		$bytes = [Convert]::ToBase64String([IO.File]::ReadAllBytes($file.FullName))
		"$relative=$bytes"
	}
	return $entries -join "`n"
}

function Test-ReparsePoint([string] $Path) {
	if (-not (Test-Path -LiteralPath $Path)) { return $false }
	return [bool]((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)
}

function Assert-OrdinaryDirectory([string] $Path, [string] $Message) {
	Assert-True (Test-Path -LiteralPath $Path -PathType Container) "$Message (missing directory)."
	Assert-True (-not (Test-ReparsePoint $Path)) "$Message (still a reparse point)."
}

function Get-LinkTarget([string] $Path) {
	$item = Get-Item -LiteralPath $Path -Force
	Assert-True ([bool]($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) "Expected '$Path' to be a reparse point."
	$target = [string] @($item.Target)[0]
	if (-not [IO.Path]::IsPathRooted($target)) { $target = Join-Path $item.Parent.FullName $target }
	return [IO.Path]::GetFullPath($target)
}

function Assert-DirectoryLink([string] $Path, [string] $Target, [string] $Message) {
	Assert-True (Test-ReparsePoint $Path) "$Message (link missing)."
	Assert-True ((Get-LinkTarget $Path) -ieq [IO.Path]::GetFullPath($Target)) "$Message (target changed)."
}

function Remove-FixturePath([string] $Path) {
	if (-not (Test-Path -LiteralPath $Path)) { return }
	$resolved = [IO.Path]::GetFullPath($Path)
	Assert-True ($resolved.StartsWith($script:ScratchPrefix, [StringComparison]::OrdinalIgnoreCase)) "Refusing to remove non-fixture path '$resolved'."
	if (Test-ReparsePoint $resolved) { Remove-Item -LiteralPath $resolved -Force }
	else { Remove-Item -LiteralPath $resolved -Recurse -Force }
}

function New-DirectoryLink([string] $Path, [string] $Target) {
	New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
	New-Item -ItemType SymbolicLink -Path $Path -Target $Target | Out-Null
	Assert-DirectoryLink $Path $Target "Failed to create fixture link '$Path'"
}

function Invoke-Materialize([int] $ExpectedExitCode, [string] $EngineData, [string] $ProjectData, [string] $OutputData) {
	$output = @(& $script:DataPacker --materialize-data $EngineData $ProjectData $OutputData 2>&1)
	$exitCode = $LASTEXITCODE
	Assert-True ($exitCode -eq $ExpectedExitCode) "DataPacker exit $exitCode, expected $ExpectedExitCode. Output: $($output -join '; ')"
}

$sourceRoot = [IO.Path]::GetFullPath($RepositoryRoot)
if ([string]::IsNullOrWhiteSpace($DataPackerExecutable)) {
	$DataPackerExecutable = Join-Path $sourceRoot 'DataPacker\Platforms\VisualStudio2026\Output\DataPacker.exe'
}
Assert-True (Test-Path -LiteralPath $DataPackerExecutable -PathType Leaf) "Built DataPacker not found at '$DataPackerExecutable'. Pass -DataPackerExecutable; this fixture does not build it."
$script:DataPacker = (Resolve-Path -LiteralPath $DataPackerExecutable).Path

$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$scratch = Join-Path $tempRoot ('broken-engine-materialize-data-' + [guid]::NewGuid().ToString())
$script:ScratchPrefix = [IO.Path]::GetFullPath($scratch).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$oldForbidExpensiveExport = $env:BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT
$oldForbidGaeaExport = $env:BT_DATAPACKER_FORBID_GAEA_EXPORT

try {
	$env:BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT = '1'
	$env:BT_DATAPACKER_FORBID_GAEA_EXPORT = '1'
	New-Item -ItemType Directory -Path $scratch | Out-Null
	$primary = Join-Path $scratch 'primary'
	$worktree = Join-Path $scratch 'worktree'
	Invoke-Git @('init', '-q', $primary) | Out-Null
	Invoke-Git @('-C', $primary, 'config', 'user.email', 'fixture@example.test') | Out-Null
	Invoke-Git @('-C', $primary, 'config', 'user.name', 'fixture') | Out-Null

	$primaryEngineData = Join-Path $primary 'Engine\Data'
	$primaryProjectData = Join-Path $primary 'Projects\FixtureProject\Data'
	$primaryThirdParty = Join-Path $primary 'ThirdParty'
	New-Item -ItemType Directory -Path $primaryEngineData, $primaryProjectData, $primaryThirdParty -Force | Out-Null
	[IO.File]::WriteAllText((Join-Path $primaryEngineData 'engine-input.txt'), 'engine input', [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText((Join-Path $primaryProjectData 'project-input.txt'), 'project input', [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText((Join-Path $primaryThirdParty 'fixture.txt'), 'third party', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $primary, 'add', 'Engine/Data', 'Projects/FixtureProject/Data', 'ThirdParty') | Out-Null
	Invoke-Git @('-C', $primary, 'commit', '-qm', 'fixture baseline') | Out-Null
	Invoke-Git @('-C', $primary, 'worktree', 'add', '-q', '-b', 'fixture-worktree', $worktree) | Out-Null

	$worktreeEngineData = Join-Path $worktree 'Engine\Data'
	$worktreeProjectData = Join-Path $worktree 'Projects\FixtureProject\Data'
	$outputParent = Join-Path $worktree 'Projects\FixtureProject\Platforms\VisualStudio2026\Output'
	$outputData = Join-Path $outputParent 'Data'
	$outputAttribution = Join-Path $outputParent 'Attribution'
	$primaryOutputParent = Join-Path $primary 'Projects\FixtureProject\Platforms\VisualStudio2026\Output'
	$primaryData = Join-Path $primaryOutputParent 'Data'
	$primaryAttribution = Join-Path $primaryOutputParent 'Attribution'
	New-Item -ItemType Directory -Path (Join-Path $primaryData 'Nested'), $primaryAttribution -Force | Out-Null
	[IO.File]::WriteAllBytes((Join-Path $primaryData 'Nested\payload.bin'), [byte[]](0, 1, 2, 127, 128, 254, 255))
	[IO.File]::WriteAllText((Join-Path $primaryData 'sentinel.txt'), 'primary data sentinel', [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText((Join-Path $primaryAttribution 'license.txt'), 'attribution sentinel', [Text.UTF8Encoding]::new($false))
	$primaryBytes = Get-DirectoryBytes $primaryData
	$attributionBytes = Get-DirectoryBytes $primaryAttribution

	# Exact arity rejects before any output initialization.
	$arityOutput = @(& $script:DataPacker --materialize-data $worktreeEngineData $worktreeProjectData 2>&1)
	Assert-True ($LASTEXITCODE -eq 1) "Invalid materialize arity did not fail: $($arityOutput -join '; ')"
	Assert-True (-not (Test-Path -LiteralPath $outputData)) 'Invalid arity created Data.'
	Assert-True (-not (Test-Path -LiteralPath $outputAttribution)) 'Invalid arity created Attribution.'

	# Clean output: constructor discovers primary Data, then the explicit EnsureLocal call materializes it.
	Invoke-Materialize 0 $worktreeEngineData $worktreeProjectData $outputData
	Assert-OrdinaryDirectory $outputData 'Clean materialization did not produce ordinary Data'
	Assert-True ((Get-DirectoryBytes $outputData) -ceq $primaryBytes) 'Clean materialization changed Data bytes.'
	Assert-True ((Get-DirectoryBytes $primaryData) -ceq $primaryBytes) 'Clean materialization changed primary Data bytes.'
	Assert-True (-not (Test-Path -LiteralPath $outputAttribution)) 'Clean materialization created absent Attribution.'
	Write-Host 'PASS clean materialization and absent Attribution isolation'

	# Warm recognized links: Data materializes; Attribution remains the exact recognized primary link.
	Remove-FixturePath $outputData
	New-DirectoryLink $outputData $primaryData
	New-DirectoryLink $outputAttribution $primaryAttribution
	Invoke-Materialize 0 $worktreeEngineData $worktreeProjectData $outputData
	Assert-OrdinaryDirectory $outputData 'Warm materialization did not produce ordinary Data'
	Assert-True ((Get-DirectoryBytes $outputData) -ceq $primaryBytes) 'Warm materialization changed Data bytes.'
	Assert-DirectoryLink $outputAttribution $primaryAttribution 'Warm materialization changed Attribution link'
	Assert-True ((Get-DirectoryBytes $primaryAttribution) -ceq $attributionBytes) 'Warm materialization changed Attribution bytes.'
	Assert-True ((Get-DirectoryBytes $primaryData) -ceq $primaryBytes) 'Warm materialization changed primary Data bytes.'
	Write-Host 'PASS warm materialization and recognized Attribution isolation'

	# Already-local mode is idempotent and still does not inspect or replace Attribution.
	$localBytes = Get-DirectoryBytes $outputData
	Invoke-Materialize 0 $worktreeEngineData $worktreeProjectData $outputData
	Assert-OrdinaryDirectory $outputData 'Already-local materialization changed Data type'
	Assert-True ((Get-DirectoryBytes $outputData) -ceq $localBytes) 'Already-local materialization changed Data bytes.'
	Assert-DirectoryLink $outputAttribution $primaryAttribution 'Already-local materialization changed Attribution link'
	Write-Host 'PASS already-local idempotence'

	# An unexpected destination reparse point fails closed without replacing either link or source bytes.
	$unexpectedData = Join-Path $scratch 'unexpected-data'
	New-Item -ItemType Directory -Path $unexpectedData | Out-Null
	[IO.File]::WriteAllText((Join-Path $unexpectedData 'unexpected.txt'), 'unexpected target', [Text.UTF8Encoding]::new($false))
	$unexpectedBytes = Get-DirectoryBytes $unexpectedData
	Remove-FixturePath $outputData
	New-DirectoryLink $outputData $unexpectedData
	Invoke-Materialize 1 $worktreeEngineData $worktreeProjectData $outputData
	Assert-DirectoryLink $outputData $unexpectedData 'Unexpected Data reparse was mutated'
	Assert-True ((Get-DirectoryBytes $unexpectedData) -ceq $unexpectedBytes) 'Unexpected reparse target bytes changed.'
	Assert-True ((Get-DirectoryBytes $primaryData) -ceq $primaryBytes) 'Unexpected reparse case changed primary Data bytes.'
	Assert-DirectoryLink $outputAttribution $primaryAttribution 'Unexpected reparse case changed Attribution link'
	Write-Host 'PASS unexpected destination reparse rejection'

	# A primary source that becomes a reparse point is rejected before copy and preserves the recognized output link.
	Remove-FixturePath $outputData
	New-DirectoryLink $outputData $primaryData
	$primaryDataBacking = Join-Path $primaryOutputParent 'DataBacking'
	Move-Item -LiteralPath $primaryData -Destination $primaryDataBacking
	New-DirectoryLink $primaryData $primaryDataBacking
	Invoke-Materialize 1 $worktreeEngineData $worktreeProjectData $outputData
	Assert-DirectoryLink $outputData $primaryData 'Reparse primary source rejection changed Data link'
	Assert-DirectoryLink $primaryData $primaryDataBacking 'Reparse primary source rejection changed source link'
	Assert-True ((Get-DirectoryBytes $primaryDataBacking) -ceq $primaryBytes) 'Reparse primary source rejection changed source bytes.'
	Assert-DirectoryLink $outputAttribution $primaryAttribution 'Reparse primary source rejection changed Attribution link'
	Remove-FixturePath $primaryData
	Move-Item -LiteralPath $primaryDataBacking -Destination $primaryData
	Write-Host 'PASS primary source reparse rejection'

	# A sparse file makes the low-space cancellation path deterministic without consuming its logical size.
	Remove-FixturePath $outputData
	New-DirectoryLink $outputData $primaryData
	$drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($primaryData))
	$warningBytes = 10L * 1024 * 1024 * 1024
	$sparseLength = [Math]::Max(1L * 1024 * 1024, $drive.AvailableFreeSpace - $warningBytes + 1024 * 1024)
	$conservativeAllocation = $sparseLength + 1024 * 1024
	$reserveBytes = [Math]::Max(1L * 1024 * 1024 * 1024, [long][Math]::Ceiling($conservativeAllocation * 0.05))
	$sparseFile = Join-Path $primaryData 'cancellation-sparse.bin'
	$sparseReady = $conservativeAllocation + $reserveBytes -le $drive.AvailableFreeSpace
	$sparseOutput = @('sparse-file setup not attempted because the volume cannot satisfy the low-space warning preconditions')
	if ($sparseReady) {
		[IO.File]::WriteAllBytes($sparseFile, [byte[]](0))
		$sparseOutput = @(& fsutil.exe sparse setflag $sparseFile 2>&1)
		$sparseReady = $LASTEXITCODE -eq 0
		if ($sparseReady) {
			$stream = [IO.File]::Open($sparseFile, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::Read)
			try { $stream.SetLength($sparseLength) }
			finally { $stream.Dispose() }
			$sparseReady = (Get-Item -LiteralPath $sparseFile).Length -eq $sparseLength
		}
	}
	if ($sparseReady) {
		Invoke-Materialize 1 $worktreeEngineData $worktreeProjectData $outputData
		Assert-DirectoryLink $outputData $primaryData 'Low-space cancellation changed Data link'
		Assert-True ((Get-Item -LiteralPath $sparseFile).Length -eq $sparseLength) 'Low-space cancellation changed sparse source length.'
		Assert-DirectoryLink $outputAttribution $primaryAttribution 'Low-space cancellation changed Attribution link'
		Write-Host 'PASS low-space cancellation preserves links and source'
	}
	else {
		Write-Warning "LIMITATION: cancellation setup requires sparse-file support and enough free space; fsutil output: $($sparseOutput -join '; ')"
		Write-Host 'INCOMPLETE DataPacker --materialize-data fixture (low-space cancellation skipped)'
		exit 2
	}

	Write-Host 'PASS DataPacker --materialize-data fixture'
}
finally {
	$env:BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT = $oldForbidExpensiveExport
	$env:BT_DATAPACKER_FORBID_GAEA_EXPORT = $oldForbidGaeaExport
	if (Test-Path -LiteralPath $scratch) {
		$resolvedScratch = [IO.Path]::GetFullPath($scratch)
		$expectedPrefix = $tempRoot + [IO.Path]::DirectorySeparatorChar
		Assert-True ($resolvedScratch.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) "Refusing to clean non-temporary fixture root '$resolvedScratch'."
		Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
	}
}
