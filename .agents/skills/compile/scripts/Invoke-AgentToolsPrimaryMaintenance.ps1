[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $Primary,
	[Parameter(Mandatory = $true)]
	[string] $Baseline,
	[Parameter(Mandatory = $true)]
	[string[]] $ApprovedChangedPath,
	[int] $WaitSeconds = 660,
	[switch] $LegacySessionsClosed
)

$ErrorActionPreference = 'Stop'

function Get-CanonicalPath([string] $Path) {
	return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

function Invoke-Git([string[]] $Arguments) {
	$output = @(& git @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
}

function ConvertTo-NormalizedRepositoryPath([string] $Path, [string] $Root) {
	if ([string]::IsNullOrWhiteSpace($Path)) { throw 'Approved changed paths must not be empty.' }
	if ([IO.Path]::IsPathRooted($Path)) { throw "Approved changed path must be repository-relative: '$Path'." }
	$fullPath = Get-CanonicalPath (Join-Path $Root $Path)
	$rootPrefix = $Root + [IO.Path]::DirectorySeparatorChar
	if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Approved changed path escapes the primary checkout: '$Path'."
	}
	return ([IO.Path]::GetRelativePath($Root, $fullPath) -replace '\\', '/')
}

if (-not [IO.Path]::IsPathRooted($Primary)) { throw 'Primary must be absolute.' }
$primaryRoot = Get-CanonicalPath $Primary
$topLevel = Get-CanonicalPath (@(Invoke-Git @('-C', $primaryRoot, 'rev-parse', '--show-toplevel'))[0].Trim())
if (-not $topLevel.Equals($primaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
	throw "Primary is not the repository root: '$primaryRoot'."
}
$gitDirectory = Get-Item -LiteralPath (Join-Path $primaryRoot '.git') -Force -ErrorAction Stop
if (-not $gitDirectory.PSIsContainer -or ($gitDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
	throw "Primary must contain an ordinary .git directory: '$primaryRoot'."
}
$commonDirectory = Get-CanonicalPath (@(Invoke-Git @('-C', $primaryRoot, 'rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
if (-not $commonDirectory.Equals((Get-CanonicalPath $gitDirectory.FullName), [StringComparison]::OrdinalIgnoreCase)) {
	throw "Primary .git directory does not match the Git common directory: '$primaryRoot'."
}

Invoke-Git @('-C', $primaryRoot, 'symbolic-ref', '--quiet', '--short', 'HEAD') | Out-Null
if ($Baseline -notmatch '^[0-9a-fA-F]{40}$') { throw 'Baseline must be a full commit hash.' }
$resolvedBaseline = @(Invoke-Git @('-C', $primaryRoot, 'rev-parse', '--verify', "$Baseline^{commit}"))[0].Trim()
if (-not $resolvedBaseline.Equals($Baseline, [StringComparison]::OrdinalIgnoreCase)) {
	throw "Baseline does not resolve to the supplied commit: '$Baseline'."
}
& git -C $primaryRoot merge-base --is-ancestor $Baseline HEAD
if ($LASTEXITCODE -ne 0) { throw "HEAD does not descend from baseline '$Baseline'." }

foreach ($operationMarker in @('MERGE_HEAD', 'rebase-merge', 'rebase-apply', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'BISECT_LOG', 'sequencer')) {
	$markerPath = @(Invoke-Git @('-C', $primaryRoot, 'rev-parse', '--path-format=absolute', '--git-path', $operationMarker))[0].Trim()
	if (Test-Path -LiteralPath $markerPath) { throw "Git operation is in progress: '$operationMarker'." }
}

$approvedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($path in $ApprovedChangedPath) {
	[void]$approvedPaths.Add((ConvertTo-NormalizedRepositoryPath $path $primaryRoot))
}
$changedPaths = @(
	Invoke-Git @('-C', $primaryRoot, '-c', 'core.quotePath=false', 'diff', '--name-only', "$Baseline..HEAD", '--')
	Invoke-Git @('-C', $primaryRoot, '-c', 'core.quotePath=false', 'diff', '--cached', '--name-only', '--')
	Invoke-Git @('-C', $primaryRoot, '-c', 'core.quotePath=false', 'diff', '--name-only', '--')
	Invoke-Git @('-C', $primaryRoot, '-c', 'core.quotePath=false', 'ls-files', '--others', '--exclude-standard')
)
$unexpectedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($path in $changedPaths) {
	if ([string]::IsNullOrWhiteSpace($path)) { continue }
	$normalizedPath = ConvertTo-NormalizedRepositoryPath $path $primaryRoot
	if (-not $approvedPaths.Contains($normalizedPath)) { [void]$unexpectedPaths.Add($normalizedPath) }
}
if ($unexpectedPaths.Count -ne 0) {
	throw "Primary checkout contains changed paths outside approved set: $(@($unexpectedPaths) -join ', ')."
}

$worktreeCliOutput = Join-Path $primaryRoot 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
$agentHarnessOutput = Join-Path $primaryRoot 'Tools\AgentHarness\Platforms\VisualStudio2026\Output'
foreach ($output in @($worktreeCliOutput, $agentHarnessOutput)) {
	$outputItem = Get-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
	if ($null -ne $outputItem -and (-not $outputItem.PSIsContainer -or ($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint))) { throw "Primary AgentTools Output must be an ordinary directory: '$output'." }
}
$worktreeCli = Join-Path $worktreeCliOutput 'WorktreeCli.exe'
$agentHarness = Join-Path $agentHarnessOutput 'AgentHarness.exe'
$solutions = @(
	(Join-Path $primaryRoot 'Tools\WorktreeCli\Platforms\VisualStudio2026\WorktreeCli.sln'),
	(Join-Path $primaryRoot 'Tools\AgentHarness\Platforms\VisualStudio2026\AgentHarness.sln')
)
foreach ($solution in $solutions) { if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) { throw "AgentTools solution is missing: '$solution'." } }
$capabilityScript = Join-Path $primaryRoot '.agents\scripts\Test-AgentToolsCapabilities.ps1'
if (-not (Test-Path -LiteralPath $capabilityScript -PathType Leaf)) { throw "AgentTools capability checker is missing: '$capabilityScript'." }

$msBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msBuild -PathType Leaf)) {
	$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) { throw "Visual Studio locator is missing: '$vsWhere'." }
	$installPaths = @(& $vsWhere -latest -version '[18.0,19.0)' -products * -requires Microsoft.Component.MSBuild -property installationPath)
	if ($LASTEXITCODE -ne 0 -or $installPaths.Count -ne 1 -or [string]::IsNullOrWhiteSpace($installPaths[0])) {
		throw 'Unable to locate a Visual Studio 2026 installation containing MSBuild.'
	}
	$msBuild = Join-Path $installPaths[0].Trim() 'MSBuild\Current\Bin\MSBuild.exe'
	if (-not (Test-Path -LiteralPath $msBuild -PathType Leaf)) { throw "MSBuild is missing: '$msBuild'." }
}

Import-Module (Join-Path $primaryRoot '.agents\scripts\WorktreeCliSessionExclusion.psm1') -Force
$owner = [guid]::NewGuid().ToString()
$maintenance = $null
try {
	$maintenance = Enter-WorktreeCliMaintenance -RepositoryRoot $primaryRoot -Owner $owner -Label 'explicit primary AgentTools maintenance' -Worktree $primaryRoot -WaitSeconds $WaitSeconds -LegacySessionsClosed:$LegacySessionsClosed
	$worktreePresent = Test-Path -LiteralPath $worktreeCli -PathType Leaf
	$harnessPresent = Test-Path -LiteralPath $agentHarness -PathType Leaf
	if ($worktreePresent -ne $harnessPresent) { throw 'AgentTools pre-build output is partial; both executables must be present or absent.' }
	if ($worktreePresent) {
		$exitCode = Invoke-WorktreeCliTrackedProcess -Executable "$PSHOME\pwsh.exe" -WorkingDirectory $primaryRoot -ArgumentList @('-NoProfile', '-File', $capabilityScript, '-WorktreeCliExecutable', $worktreeCli, '-AgentHarnessExecutable', $agentHarness)
		if ($exitCode -ne 0) { throw "AgentTools pre-build capability check failed with exit code $exitCode." }
	}
	foreach ($solution in $solutions) {
		$exitCode = Invoke-WorktreeCliTrackedProcess -Executable $msBuild -WorkingDirectory $primaryRoot -ArgumentList @($solution, '/p:Configuration=Release', '/p:Platform=x64', '/p:EnableClangTidyCodeAnalysis=false', '/p:RunCodeAnalysis=false', '/verbosity:minimal')
		if ($exitCode -ne 0) { throw "AgentTools primary-maintenance build failed for '$solution' with exit code $exitCode." }
	}
	$exitCode = Invoke-WorktreeCliTrackedProcess -Executable "$PSHOME\pwsh.exe" -WorkingDirectory $primaryRoot -ArgumentList @('-NoProfile', '-File', $capabilityScript, '-WorktreeCliExecutable', $worktreeCli, '-AgentHarnessExecutable', $agentHarness)
	if ($exitCode -ne 0) { throw "AgentTools post-build capability check failed with exit code $exitCode." }
	Write-Host "AgentTools primary-maintenance build succeeded via '$msBuild'."
	[pscustomobject]@{ Status = 'Success'; ExitCode = 0; WorktreeCli = $worktreeCli; AgentHarness = $agentHarness; MSBuild = $msBuild }
}
finally {
	if ($null -ne $maintenance) { Exit-WorktreeCliMaintenance -RepositoryRoot $primaryRoot -Owner $owner }
}
