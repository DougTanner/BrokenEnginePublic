[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $RepositoryRoot
)

$ErrorActionPreference = 'Stop'

function Invoke-Git {
	param([string[]] $Arguments)
	$output = & git @Arguments 2>&1
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $output" }
	return $output
}

function Get-CanonicalPath([string] $Path) {
	return [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

function Get-LinkTarget([System.IO.DirectoryInfo] $Item) {
	$target = $Item.Target
	if ($target -is [array]) { $target = $target[0] }
	if ([string]::IsNullOrWhiteSpace($target)) { throw "Unable to read link target for '$($Item.FullName)'." }
	if (-not [System.IO.Path]::IsPathRooted($target)) { $target = Join-Path $Item.Parent.FullName $target }
	return Get-CanonicalPath $target
}

function Assert-PopulatedDirectory([string] $Path, [string] $Description) {
	$item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
	if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		throw "$Description must be an ordinary directory: '$Path'."
	}
	if (-not (Get-ChildItem -LiteralPath $Path -Force | Select-Object -First 1)) {
		throw "$Description is empty: '$Path'."
	}
}

function Ensure-DirectoryLink([string] $Destination, [string] $Target) {
	$expected = Get-CanonicalPath $Target
	if (Test-Path -LiteralPath $Destination) {
		$item = Get-Item -LiteralPath $Destination -Force
		if (-not $item.PSIsContainer) {
			throw "Provisioning destination is not a directory link: '$Destination'."
		}
		if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
			if (Get-ChildItem -LiteralPath $Destination -Force | Select-Object -First 1) {
				throw "Provisioning destination is populated: '$Destination'."
			}
			Remove-Item -LiteralPath $Destination
		}
		else {
			$actual = Get-LinkTarget $item
			if (-not $actual.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) {
				throw "Provisioning link '$Destination' targets '$actual'; expected '$expected'."
			}
			return
		}
	}
	$parent = Split-Path -Parent $Destination
	New-Item -ItemType Directory -Path $parent -Force | Out-Null
	New-Item -ItemType SymbolicLink -Path $Destination -Target $expected | Out-Null
}

$root = Get-CanonicalPath $RepositoryRoot
if (-not [System.IO.Path]::IsPathRooted($RepositoryRoot)) { throw 'RepositoryRoot must be absolute.' }
if (@(Invoke-Git @('-C', $root, 'rev-parse', '--show-toplevel'))[0].Trim() -ne $root) { throw "RepositoryRoot is not the repository root: '$root'." }

$commonDir = Get-CanonicalPath (@(Invoke-Git @('-C', $root, 'rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
$records = @(); $record = $null
foreach ($line in (Invoke-Git @('-C', $root, 'worktree', 'list', '--porcelain'))) {
	if ($line -match '^worktree (.+)$') { if ($record) { $records += $record }; $record = @{ Path = Get-CanonicalPath $Matches[1] } }
}
if ($record) { $records += $record }
$primary = @($records | Where-Object { (Get-CanonicalPath (Join-Path $_.Path '.git')).Equals($commonDir, [StringComparison]::OrdinalIgnoreCase) })
if ($primary.Count -ne 1) { throw "Expected exactly one primary checkout; found $($primary.Count)." }
$primaryRoot = $primary[0].Path
$current = @($records | Where-Object { $_.Path.Equals($root, [StringComparison]::OrdinalIgnoreCase) })
if ($current.Count -ne 1) { throw "RepositoryRoot is not a registered worktree: '$root'." }

$modulePaths = @()
foreach ($line in (Invoke-Git @('-C', $root, 'config', '--file', (Join-Path $root '.gitmodules'), '--get-regexp', '^submodule\..*\.path$'))) {
	$parts = $line -split '\s+', 2
	if ($parts.Count -eq 2) { $modulePaths += $parts[1].Trim() }
}
if ($modulePaths.Count -ne 20) { throw "Expected 20 submodule paths; found $($modulePaths.Count)." }

$libraryRelativeRoot = 'ThirdParty/Prebuilts/Platforms/VisualStudio2026/Output'
$primaryOutput = Join-Path $primaryRoot $libraryRelativeRoot
Assert-PopulatedDirectory $primaryOutput 'Primary ThirdParty Output'
foreach ($configuration in @('Debug', 'Profile', 'Release')) {
	$library = Join-Path $primaryOutput "ThirdParty.$configuration.lib"
	if (-not (Test-Path -LiteralPath $library -PathType Leaf) -or (Get-Item -LiteralPath $library).Length -eq 0) { throw "Required primary library is missing or empty: '$library'." }
}

foreach ($relativePath in $modulePaths) {
	$source = Join-Path $primaryRoot $relativePath
	Assert-PopulatedDirectory $source "Primary submodule '$relativePath'"
	$primaryPin = (@(Invoke-Git @('-C', $primaryRoot, 'ls-tree', 'HEAD', '--', $relativePath))[0] -split '\s+')[2]
	$currentPin = (@(Invoke-Git @('-C', $root, 'ls-tree', 'HEAD', '--', $relativePath))[0] -split '\s+')[2]
	$sourceHead = @(Invoke-Git @('-C', $source, 'rev-parse', 'HEAD'))[0].Trim()
	if ([string]::IsNullOrWhiteSpace($primaryPin) -or $primaryPin -ne $currentPin -or $primaryPin -ne $sourceHead) {
		throw "Submodule '$relativePath' pin mismatch (primary=$primaryPin, worktree=$currentPin, source=$sourceHead)."
	}
	if (-not $root.Equals($primaryRoot, [StringComparison]::OrdinalIgnoreCase)) { Ensure-DirectoryLink (Join-Path $root $relativePath) $source }
}

if (-not $root.Equals($primaryRoot, [StringComparison]::OrdinalIgnoreCase)) { Ensure-DirectoryLink (Join-Path $root $libraryRelativeRoot) $primaryOutput }
Write-Host "ThirdParty provisioning validated for '$root' using primary '$primaryRoot'."
