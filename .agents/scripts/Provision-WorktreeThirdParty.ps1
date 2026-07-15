[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $RepositoryRoot,
	[int] $WaitSeconds = 660,
	[switch] $LegacySessionsClosed
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'AgentCliSessionExclusion.psm1') -Force

function Invoke-Git {
	param([string[]] $Arguments)
	$output = & git @Arguments 2>&1
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $output" }
	return $output
}

function Get-CanonicalPath([string] $Path) {
	return [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

function Get-LinkTarget([System.IO.FileSystemInfo] $Item) {
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

function Assert-DirectoryLinkDestination([string] $Destination, [string] $Target) {
	$expected = Get-CanonicalPath $Target
	$item = Get-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
	if ($null -eq $item) { return }
	if (-not $item.PSIsContainer) {
		throw "Provisioning destination is not a directory link: '$Destination'."
	}
	if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		if (Get-ChildItem -LiteralPath $Destination -Force | Select-Object -First 1) {
			throw "Provisioning destination is populated: '$Destination'."
		}
		return
	}
	$actual = Get-LinkTarget $item
	if (-not $actual.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Provisioning link '$Destination' targets '$actual'; expected '$expected'."
	}
}

function Ensure-DirectoryLink([string] $Destination, [string] $Target, [System.Collections.Generic.List[object]] $CreatedArtifacts) {
	$expected = Get-CanonicalPath $Target
	$item = Get-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
	if ($null -ne $item) {
		if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { return }
		$CreatedArtifacts.Add([PSCustomObject]@{ Action = 'RestoreEmptyDirectory'; Path = $Destination })
		Remove-Item -LiteralPath $Destination
	}
	$parent = Split-Path -Parent $Destination
	New-Item -ItemType Directory -Path $parent -Force | Out-Null
	$CreatedArtifacts.Add([PSCustomObject]@{ Action = 'RemovePath'; Path = $Destination })
	New-Item -ItemType SymbolicLink -Path $Destination -Target $expected | Out-Null
}

function Get-LinkFarmEntries([string] $Source, [string] $RelativePath) {
	$entries = @()
	foreach ($item in (Get-ChildItem -LiteralPath $Source -Force | Where-Object { $_.Name -ne '.git' })) {
		if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
			throw "Primary submodule '$RelativePath' has unsupported top-level reparse point '$($item.FullName)'."
		}
		if ($item.PSIsContainer) {
			$nestedGit = Get-ChildItem -LiteralPath $item.FullName -Force -Recurse -Filter '.git' | Select-Object -First 1
			if ($nestedGit) {
				throw "Primary submodule '$RelativePath' directory '$($item.Name)' exposes nested Git metadata '$($nestedGit.FullName)'."
			}
			$nestedReparsePoint = Get-ChildItem -LiteralPath $item.FullName -Force -Recurse -Attributes ReparsePoint | Select-Object -First 1
			if ($nestedReparsePoint) {
				throw "Primary submodule '$RelativePath' directory '$($item.Name)' contains unsupported reparse point '$($nestedReparsePoint.FullName)'."
			}
			$entries += [PSCustomObject]@{ Name = $item.Name; Source = $item.FullName; IsDirectory = $true }
		}
		else {
			$entries += [PSCustomObject]@{ Name = $item.Name; Source = $item.FullName; IsDirectory = $false }
		}
	}
	return $entries
}

function Assert-LinkFarmDestination([string] $Destination, [object[]] $Entries) {
	$rootItem = Get-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
	if ($null -eq $rootItem) { return }
	if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		throw "Submodule provisioning root must be an ordinary directory: '$Destination'."
	}
	$expected = @{}
	foreach ($entry in $Entries) { $expected[$entry.Name] = $entry }
	foreach ($item in (Get-ChildItem -LiteralPath $Destination -Force)) {
		if (-not $expected.ContainsKey($item.Name)) {
			throw "Submodule provisioning root contains unexpected entry '$($item.FullName)'."
		}
		$entry = $expected[$item.Name]
		if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.PSIsContainer -ne $entry.IsDirectory) {
			$kind = if ($entry.IsDirectory) { 'directory' } else { 'file' }
			throw "Submodule provisioning entry must be a $kind link: '$($item.FullName)'."
		}
		$actual = Get-LinkTarget $item
		$expectedTarget = Get-CanonicalPath $entry.Source
		if (-not $actual.Equals($expectedTarget, [StringComparison]::OrdinalIgnoreCase)) {
			throw "Provisioning link '$($item.FullName)' targets '$actual'; expected '$expectedTarget'."
		}
	}
}

function Ensure-LinkFarm([string] $Destination, [object[]] $Entries, [System.Collections.Generic.List[object]] $CreatedArtifacts) {
	if ($null -eq (Get-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue)) {
		$CreatedArtifacts.Add([PSCustomObject]@{ Action = 'RemovePath'; Path = $Destination })
		New-Item -ItemType Directory -Path $Destination -Force | Out-Null
	}
	foreach ($entry in $Entries) {
		$target = Join-Path $Destination $entry.Name
		if ($null -ne (Get-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue)) { continue }
		$CreatedArtifacts.Add([PSCustomObject]@{ Action = 'RemovePath'; Path = $target })
		New-Item -ItemType SymbolicLink -Path $target -Target (Get-CanonicalPath $entry.Source) | Out-Null
	}
}

function Undo-ProvisioningArtifacts([System.Collections.Generic.List[object]] $CreatedArtifacts) {
	for ($index = $CreatedArtifacts.Count - 1; $index -ge 0; --$index) {
		$artifact = $CreatedArtifacts[$index]
		if ($artifact.Action -eq 'RemovePath') {
			if ($null -ne (Get-Item -LiteralPath $artifact.Path -Force -ErrorAction SilentlyContinue)) {
				Remove-Item -LiteralPath $artifact.Path -Force
			}
		}
		elseif ($artifact.Action -eq 'RestoreEmptyDirectory') {
			New-Item -ItemType Directory -Path $artifact.Path | Out-Null
		}
	}
}

function Get-GitlinkPin([string] $CheckoutRoot, [string] $RelativePath, [string] $Description) {
	$treeEntries = @(Invoke-Git @('-C', $CheckoutRoot, 'ls-tree', 'HEAD', '--', $RelativePath))
	if ($treeEntries.Count -ne 1) { throw "$Description has no unique tree entry for '$RelativePath'." }
	$parts = $treeEntries[0] -split '\s+', 4
	if ($parts.Count -ne 4 -or $parts[0] -ne '160000' -or $parts[1] -ne 'commit' -or [string]::IsNullOrWhiteSpace($parts[2])) {
		throw "$Description path '$RelativePath' is not a gitlink."
	}
	return $parts[2]
}

$root = Get-CanonicalPath $RepositoryRoot
if (-not [System.IO.Path]::IsPathRooted($RepositoryRoot)) { throw 'RepositoryRoot must be absolute.' }
$topLevel = Get-CanonicalPath (@(Invoke-Git @('-C', $root, 'rev-parse', '--show-toplevel'))[0].Trim())
if (-not $topLevel.Equals($root, [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot is not the repository root: '$root'." }

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

$transientClaim = $null
$owner = $env:BROKEN_ENGINE_AGENTCLI_SESSION_OWNER
if ([string]::IsNullOrWhiteSpace($owner)) {
	$owner = [guid]::NewGuid().ToString()
	$transientClaim = Register-AgentCliSession -RepositoryRoot $root -Owner $owner -Label 'transient provisioner' -Worktree $root -WaitSeconds $WaitSeconds -LegacySessionsClosed:$LegacySessionsClosed
}
else { Assert-AgentCliSessionOwner -RepositoryRoot $root -Owner $owner }

try {

$modulePaths = @()
foreach ($line in (Invoke-Git @('-C', $root, 'config', '--file', (Join-Path $root '.gitmodules'), '--get-regexp', '^submodule\..*\.path$'))) {
	$parts = $line -split '\s+', 2
	if ($parts.Count -eq 2) { $modulePaths += $parts[1].Trim() }
}
if ($modulePaths.Count -ne 20) { throw "Expected 20 submodule paths; found $($modulePaths.Count)." }
$thirdPartyRoot = Get-CanonicalPath (Join-Path $root 'ThirdParty')
$thirdPartyPrefix = $thirdPartyRoot + [System.IO.Path]::DirectorySeparatorChar
$seenModulePaths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($relativePath in $modulePaths) {
	if ([System.IO.Path]::IsPathRooted($relativePath) -or ($relativePath -split '[/\\]') -contains '..' -or ($relativePath -split '[/\\]') -contains '.') {
		throw "Submodule path must be a direct relative path without traversal: '$relativePath'."
	}
	$canonicalModulePath = Get-CanonicalPath (Join-Path $root $relativePath)
	if (-not $canonicalModulePath.StartsWith($thirdPartyPrefix, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Submodule path escapes ThirdParty: '$relativePath'."
	}
	if (-not $seenModulePaths.Add($canonicalModulePath)) { throw "Duplicate submodule path: '$relativePath'." }
}

$libraryRelativeRoot = 'ThirdParty/Prebuilts/Platforms/VisualStudio2026/Output'
$primaryOutput = Join-Path $primaryRoot $libraryRelativeRoot
Assert-PopulatedDirectory $primaryOutput 'Primary ThirdParty Output'
foreach ($configuration in @('Debug', 'Profile', 'Release')) {
	$library = Join-Path $primaryOutput "ThirdParty.$configuration.lib"
	if (-not (Test-Path -LiteralPath $library -PathType Leaf) -or (Get-Item -LiteralPath $library).Length -eq 0) { throw "Required primary library is missing or empty: '$library'." }
}
$agentCliOutputRelativeRoot = 'Tools/AgentCli/Platforms/VisualStudio2026/Output'
$primaryAgentCliOutput = Join-Path $primaryRoot $agentCliOutputRelativeRoot
Assert-PopulatedDirectory $primaryAgentCliOutput 'Primary AgentCli Output'
$primaryAgentCli = Join-Path $primaryAgentCliOutput 'AgentCli.exe'
if (-not (Test-Path -LiteralPath $primaryAgentCli -PathType Leaf) -or (Get-Item -LiteralPath $primaryAgentCli).Length -eq 0) {
	throw "Required primary AgentCli executable is missing or empty: '$primaryAgentCli'."
}

$linkFarmPlans = @()
foreach ($relativePath in $modulePaths) {
	$source = Join-Path $primaryRoot $relativePath
	Assert-PopulatedDirectory $source "Primary submodule '$relativePath'"
	$primaryPin = Get-GitlinkPin $primaryRoot $relativePath 'Primary checkout'
	$currentPin = Get-GitlinkPin $root $relativePath 'Current worktree'
	$sourceHead = @(Invoke-Git @('-C', $source, 'rev-parse', 'HEAD'))[0].Trim()
	if ([string]::IsNullOrWhiteSpace($primaryPin) -or $primaryPin -ne $currentPin -or $primaryPin -ne $sourceHead) {
		throw "Submodule '$relativePath' pin mismatch (primary=$primaryPin, worktree=$currentPin, source=$sourceHead)."
	}
	$sourceStatus = @(Invoke-Git @('-C', $source, 'status', '--porcelain', '--untracked-files=all'))
	if ($sourceStatus.Count -ne 0) { throw "Primary submodule '$relativePath' has local changes: $($sourceStatus -join '; ')." }
	$entries = @(Get-LinkFarmEntries $source $relativePath)
	if (-not $root.Equals($primaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
		$destination = Join-Path $root $relativePath
		Assert-LinkFarmDestination $destination $entries
		$linkFarmPlans += [PSCustomObject]@{ Destination = $destination; Entries = $entries }
	}
}

if (-not $root.Equals($primaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
	$outputDestination = Join-Path $root $libraryRelativeRoot
	$agentCliOutputDestination = Join-Path $root $agentCliOutputRelativeRoot
	Assert-DirectoryLinkDestination $outputDestination $primaryOutput
	Assert-DirectoryLinkDestination $agentCliOutputDestination $primaryAgentCliOutput
	$createdArtifacts = [System.Collections.Generic.List[object]]::new()
	try {
		foreach ($plan in $linkFarmPlans) { Ensure-LinkFarm $plan.Destination $plan.Entries $createdArtifacts }
		Ensure-DirectoryLink $outputDestination $primaryOutput $createdArtifacts
		Ensure-DirectoryLink $agentCliOutputDestination $primaryAgentCliOutput $createdArtifacts
	}
	catch {
		$applyError = $_
		try { Undo-ProvisioningArtifacts $createdArtifacts }
		catch { throw "Provisioning failed: $applyError Rollback also failed: $_" }
		throw $applyError
	}
}
Write-Host "Shared worktree dependencies validated for '$root' using primary '$primaryRoot'."
}
finally {
	if ($null -ne $transientClaim) { Unregister-AgentCliSession -RepositoryRoot $root -Owner $owner }
}
