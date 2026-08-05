[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $RepositoryRoot,
	[int] $WaitSeconds = 660
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1') -Force

function Get-LinkTarget([System.IO.FileSystemInfo] $Item) {
	$target = $Item.Target
	if ($target -is [array]) { $target = $target[0] }
	if ([string]::IsNullOrWhiteSpace($target)) { throw "Unable to read link target for '$($Item.FullName)'." }
	if (-not [System.IO.Path]::IsPathRooted($target)) { $target = Join-Path $Item.Parent.FullName $target }
	return Get-AgentCanonicalPath $target
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
	$expected = Get-AgentCanonicalPath $Target
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
	$expected = Get-AgentCanonicalPath $Target
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

function Get-LinkFarmEntries([string] $Source, [string] $RelativePath, [switch] $SkipNestedScan) {
	$entries = @()
	foreach ($item in (Get-ChildItem -LiteralPath $Source -Force | Where-Object { $_.Name -ne '.git' })) {
		if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
			throw "Primary submodule '$RelativePath' has unsupported top-level reparse point '$($item.FullName)'."
		}
		if ($item.PSIsContainer) {
			if (-not $SkipNestedScan) {
				$nestedGit = Get-ChildItem -LiteralPath $item.FullName -Force -Recurse -Filter '.git' | Select-Object -First 1
				if ($nestedGit) {
					throw "Primary submodule '$RelativePath' directory '$($item.Name)' exposes nested Git metadata '$($nestedGit.FullName)'."
				}
				$nestedReparsePoint = Get-ChildItem -LiteralPath $item.FullName -Force -Recurse -Attributes ReparsePoint | Select-Object -First 1
				if ($nestedReparsePoint) {
					throw "Primary submodule '$RelativePath' directory '$($item.Name)' contains unsupported reparse point '$($nestedReparsePoint.FullName)'."
				}
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
		$expectedTarget = Get-AgentCanonicalPath $entry.Source
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
		New-Item -ItemType SymbolicLink -Path $target -Target (Get-AgentCanonicalPath $entry.Source) | Out-Null
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
	$treeEntries = @(Invoke-AgentGit @('-C', $CheckoutRoot, 'ls-tree', 'HEAD', '--', $RelativePath))
	if ($treeEntries.Count -ne 1) { throw "$Description has no unique tree entry for '$RelativePath'." }
	$parts = $treeEntries[0] -split '\s+', 4
	if ($parts.Count -ne 4 -or $parts[0] -ne '160000' -or $parts[1] -ne 'commit' -or [string]::IsNullOrWhiteSpace($parts[2])) {
		throw "$Description path '$RelativePath' is not a gitlink."
	}
	return $parts[2]
}

if (-not [System.IO.Path]::IsPathRooted($RepositoryRoot)) { throw 'RepositoryRoot must be absolute.' }
$root = Get-AgentCanonicalPath $RepositoryRoot
$topLevel = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $root, 'rev-parse', '--show-toplevel'))[0].Trim())
if (-not $topLevel.Equals($root, [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot is not the repository root: '$root'." }

$commonDir = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $root, 'rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
$gitDirectory = Get-Item -LiteralPath (Join-Path $root '.git') -Force -ErrorAction SilentlyContinue
if ($null -ne $gitDirectory -and $gitDirectory.PSIsContainer -and -not ($gitDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -and
	$commonDir.Equals((Get-AgentCanonicalPath $gitDirectory.FullName), [StringComparison]::OrdinalIgnoreCase)) {
	return
}

Import-Module (Join-Path $PSScriptRoot 'WorktreeCliSessionExclusion.psm1') -Force

$records = @(); $record = $null
foreach ($line in (Invoke-AgentGit @('-C', $root, 'worktree', 'list', '--porcelain'))) {
	if ($line -match '^worktree (.+)$') { if ($record) { $records += $record }; $record = @{ Path = Get-AgentCanonicalPath $Matches[1] } }
}
if ($record) { $records += $record }
$primary = @($records | Where-Object { (Get-AgentCanonicalPath (Join-Path $_.Path '.git')).Equals($commonDir, [StringComparison]::OrdinalIgnoreCase) })
if ($primary.Count -ne 1) { throw "Expected exactly one primary checkout; found $($primary.Count)." }
$primaryRoot = $primary[0].Path
$current = @($records | Where-Object { $_.Path.Equals($root, [StringComparison]::OrdinalIgnoreCase) })
if ($current.Count -ne 1) { throw "RepositoryRoot is not a registered worktree: '$root'." }

# A transient operation claim excludes AgentTools promotion from swapping the shared executables
# this provisioning links against, for the duration of the link-farm build.
$owner = [guid]::NewGuid().ToString()
Register-WorktreeCliSession -RepositoryRoot $root -Owner $owner -Label 'thirdparty provisioner' -Worktree $root -WaitSeconds $WaitSeconds | Out-Null

try {

$modulePaths = @()
foreach ($line in (Invoke-AgentGit @('-C', $root, 'config', '--file', (Join-Path $root '.gitmodules'), '--get-regexp', '^submodule\..*\.path$'))) {
	$parts = $line -split '\s+', 2
	if ($parts.Count -eq 2) { $modulePaths += $parts[1].Trim() }
}
$thirdPartyRoot = Get-AgentCanonicalPath (Join-Path $root 'ThirdParty')
$thirdPartyPrefix = $thirdPartyRoot + [System.IO.Path]::DirectorySeparatorChar
$seenModulePaths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($relativePath in $modulePaths) {
	if ([System.IO.Path]::IsPathRooted($relativePath) -or ($relativePath -split '[/\\]') -contains '..' -or ($relativePath -split '[/\\]') -contains '.') {
		throw "Submodule path must be a direct relative path without traversal: '$relativePath'."
	}
	$canonicalModulePath = Get-AgentCanonicalPath (Join-Path $root $relativePath)
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
$worktreeCliOutputRelativeRoot = 'Tools/WorktreeCli/Platforms/VisualStudio2026/Output'
$agentHarnessOutputRelativeRoot = 'Tools/AgentHarness/Platforms/VisualStudio2026/Output'
$primaryWorktreeCliOutput = Join-Path $primaryRoot $worktreeCliOutputRelativeRoot
$primaryAgentHarnessOutput = Join-Path $primaryRoot $agentHarnessOutputRelativeRoot
Assert-PopulatedDirectory $primaryWorktreeCliOutput 'Primary WorktreeCli Output'
Assert-PopulatedDirectory $primaryAgentHarnessOutput 'Primary AgentHarness Output'
$primaryWorktreeCli = Join-Path $primaryWorktreeCliOutput 'WorktreeCli.exe'
$primaryAgentHarness = Join-Path $primaryAgentHarnessOutput 'AgentHarness.exe'
if (-not (Test-Path -LiteralPath $primaryWorktreeCli -PathType Leaf) -or (Get-Item -LiteralPath $primaryWorktreeCli).Length -eq 0) {
	throw "Required primary WorktreeCli executable is missing or empty: '$primaryWorktreeCli'."
}
if (-not (Test-Path -LiteralPath $primaryAgentHarness -PathType Leaf) -or (Get-Item -LiteralPath $primaryAgentHarness).Length -eq 0) {
	throw "Required primary AgentHarness executable is missing or empty: '$primaryAgentHarness'."
}

$pinRecords = @()
foreach ($relativePath in $modulePaths) {
	$source = Join-Path $primaryRoot $relativePath
	Assert-PopulatedDirectory $source "Primary submodule '$relativePath'"
	$primaryPin = Get-GitlinkPin $primaryRoot $relativePath 'Primary checkout'
	$currentPin = Get-GitlinkPin $root $relativePath 'Current worktree'
	$sourceHead = @(Invoke-AgentGit @('-C', $source, 'rev-parse', 'HEAD'))[0].Trim()
	if ([string]::IsNullOrWhiteSpace($primaryPin) -or $primaryPin -ne $currentPin -or $primaryPin -ne $sourceHead) {
		throw "Submodule '$relativePath' pin mismatch (primary=$primaryPin, worktree=$currentPin, source=$sourceHead). If the primary checkout updated this submodule after this worktree was created, rebase the worktree onto the primary tip so the pins agree: git -C '$root' rebase <primary-branch>."
	}
	$sourceStatus = @(Invoke-AgentGit @('-C', $source, 'status', '--porcelain', '--untracked-files=all'))
	if ($sourceStatus.Count -ne 0) { throw "Primary submodule '$relativePath' has local changes: $($sourceStatus -join '; ')." }
	$pinRecords += [PSCustomObject]@{ RelativePath = $relativePath; Source = $source; Pin = $primaryPin }
}

# A stamp recording the exact current pins says the last successful full provisioning saw these same pins;
# combined with the unconditional pin and status checks above, a matching stamp may skip the recursive
# nested-metadata scans (accepting that a git-invisible filesystem mutation inside a pinned primary
# submodule goes undetected until the pins change). Any mismatch or read failure falls back to the full
# scan, and the stamp is rewritten only after a successful full provisioning.
$stampPath = Join-Path $root 'Temp\ProvisionedThirdPartyPins.txt'
$stampText = (@($pinRecords | ForEach-Object { "$($_.RelativePath)=$($_.Pin)" }) -join "`n") + "`n"
$skipNestedScan = $false
if (-not $root.Equals($primaryRoot, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $stampPath -PathType Leaf)) {
	try { $skipNestedScan = [IO.File]::ReadAllText($stampPath).Replace("`r", '') -ceq $stampText }
	catch { $skipNestedScan = $false }
}

$linkFarmPlans = @()
foreach ($record in $pinRecords) {
	$entries = @(Get-LinkFarmEntries $record.Source $record.RelativePath -SkipNestedScan:$skipNestedScan)
	if (-not $root.Equals($primaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
		$destination = Join-Path $root $record.RelativePath
		Assert-LinkFarmDestination $destination $entries
		$linkFarmPlans += [PSCustomObject]@{ Destination = $destination; Entries = $entries }
	}
}

if (-not $root.Equals($primaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
	$outputDestination = Join-Path $root $libraryRelativeRoot
	$worktreeCliOutputDestination = Join-Path $root $worktreeCliOutputRelativeRoot
	$agentHarnessOutputDestination = Join-Path $root $agentHarnessOutputRelativeRoot
	Assert-DirectoryLinkDestination $outputDestination $primaryOutput
	Assert-DirectoryLinkDestination $worktreeCliOutputDestination $primaryWorktreeCliOutput
	Assert-DirectoryLinkDestination $agentHarnessOutputDestination $primaryAgentHarnessOutput
	$createdArtifacts = [System.Collections.Generic.List[object]]::new()
	try {
		foreach ($plan in $linkFarmPlans) { Ensure-LinkFarm $plan.Destination $plan.Entries $createdArtifacts }
		Ensure-DirectoryLink $outputDestination $primaryOutput $createdArtifacts
		Ensure-DirectoryLink $worktreeCliOutputDestination $primaryWorktreeCliOutput $createdArtifacts
		Ensure-DirectoryLink $agentHarnessOutputDestination $primaryAgentHarnessOutput $createdArtifacts
	}
	catch {
		$applyError = $_
		try { Undo-ProvisioningArtifacts $createdArtifacts }
		catch { throw "Provisioning failed: $applyError Rollback also failed: $_" }
		throw $applyError
	}
	if (-not $skipNestedScan) {
		# The stamp is a pure optimization; never fail successful provisioning over it.
		try {
			New-Item -ItemType Directory -Path (Join-Path $root 'Temp') -Force | Out-Null
			[IO.File]::WriteAllText($stampPath, $stampText)
		}
		catch { Write-Warning "Unable to write the provisioning pin stamp '$stampPath', so the next run repeats the full scan: $_" }
	}
}
Write-Host "Shared worktree dependencies validated for '$root' using primary '$primaryRoot'."
}
finally {
	Unregister-WorktreeCliSession -RepositoryRoot $root -Owner $owner
}
