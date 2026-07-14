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

$output = Join-Path $primaryRoot 'Tools\AgentCli\Platforms\VisualStudio2026\Output'
$outputItem = Get-Item -LiteralPath $output -Force -ErrorAction Stop
if (-not $outputItem.PSIsContainer -or ($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
	throw "Primary AgentCli Output must be an ordinary directory: '$output'."
}
$agentCli = Join-Path $output 'AgentCli.exe'
$solution = Join-Path $primaryRoot 'Tools\AgentCli\Platforms\VisualStudio2026\AgentCli.sln'
if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) { throw "AgentCli solution is missing: '$solution'." }
$capabilityScript = Join-Path $primaryRoot '.agents\scripts\Test-AgentCliCapabilities.ps1'
if (-not (Test-Path -LiteralPath $capabilityScript -PathType Leaf)) { throw "AgentCli capability checker is missing: '$capabilityScript'." }

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

Import-Module (Join-Path $primaryRoot '.agents\scripts\AgentCliSessionExclusion.psm1') -Force
$owner = [guid]::NewGuid().ToString()
$maintenance = $null
try {
	$maintenance = Enter-AgentCliMaintenance -RepositoryRoot $primaryRoot -Owner $owner -Label 'explicit primary AgentCli maintenance' -Worktree $primaryRoot -WaitSeconds $WaitSeconds -LegacySessionsClosed:$LegacySessionsClosed
	$exitCode = Invoke-AgentCliTrackedProcess -Executable "$PSHOME\pwsh.exe" -WorkingDirectory $primaryRoot -ArgumentList @('-NoProfile', '-File', $capabilityScript, '-Executable', $agentCli)
	if ($exitCode -ne 0) { throw "AgentCli pre-build capability check failed with exit code $exitCode." }
	$exitCode = Invoke-AgentCliTrackedProcess -Executable $msBuild -WorkingDirectory $primaryRoot -ArgumentList @(
		$solution, '/p:Configuration=Release', '/p:Platform=x64',
		'/p:EnableClangTidyCodeAnalysis=false', '/p:RunCodeAnalysis=false', '/verbosity:minimal')
	if ($exitCode -ne 0) { throw "AgentCli primary-maintenance build failed with exit code $exitCode." }
	$exitCode = Invoke-AgentCliTrackedProcess -Executable "$PSHOME\pwsh.exe" -WorkingDirectory $primaryRoot -ArgumentList @('-NoProfile', '-File', $capabilityScript, '-Executable', $agentCli)
	if ($exitCode -ne 0) { throw "AgentCli post-build capability check failed with exit code $exitCode." }
	Write-Host "AgentCli primary-maintenance build succeeded via '$msBuild'."
	[pscustomobject]@{ Status = 'Success'; ExitCode = 0; Executable = $agentCli; MSBuild = $msBuild }
}
finally {
	if ($null -ne $maintenance) { Exit-AgentCliMaintenance -RepositoryRoot $primaryRoot -Owner $owner }
}
