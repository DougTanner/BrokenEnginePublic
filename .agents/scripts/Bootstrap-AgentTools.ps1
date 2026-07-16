[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $RepositoryRoot,
	[int] $WaitSeconds = 660
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WorktreeCliSessionExclusion.psm1') -Force

function Invoke-Git {
	param([string[]] $Arguments)
	$output = & git @Arguments 2>&1
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $output" }
	return $output
}

function Get-CanonicalPath([string] $Path) {
	return [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

if (-not [System.IO.Path]::IsPathRooted($RepositoryRoot)) { throw 'RepositoryRoot must be absolute.' }
$root = Get-CanonicalPath $RepositoryRoot
$topLevel = Get-CanonicalPath (@(Invoke-Git @('-C', $root, 'rev-parse', '--show-toplevel'))[0].Trim())
if (-not $topLevel.Equals($root, [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot is not the repository root: '$root'." }
$gitDirectory = Get-Item -LiteralPath (Join-Path $root '.git') -Force -ErrorAction Stop
if (-not $gitDirectory.PSIsContainer -or ($gitDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "RepositoryRoot is not the primary checkout: '$root'." }
$commonDir = Get-CanonicalPath (@(Invoke-Git @('-C', $root, 'rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
if (-not $commonDir.Equals((Get-CanonicalPath $gitDirectory.FullName), [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot is not the primary checkout: '$root'." }

$owner = $env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER
if ([string]::IsNullOrWhiteSpace($owner)) { throw 'AgentTools bootstrap requires a live wrapper session claim.' }
Assert-WorktreeCliSessionOwner -RepositoryRoot $root -Owner $owner

$worktreeCliOutput = Join-Path $root 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
$agentHarnessOutput = Join-Path $root 'Tools\AgentHarness\Platforms\VisualStudio2026\Output'
foreach ($output in @($worktreeCliOutput, $agentHarnessOutput)) {
	$outputItem = Get-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
	if ($null -ne $outputItem -and (-not $outputItem.PSIsContainer -or ($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint))) { throw "Primary AgentTools Output must be an ordinary directory: '$output'." }
}
$worktreeCli = Join-Path $worktreeCliOutput 'WorktreeCli.exe'
$agentHarness = Join-Path $agentHarnessOutput 'AgentHarness.exe'
if ((Test-Path -LiteralPath $worktreeCli -PathType Leaf) -and (Test-Path -LiteralPath $agentHarness -PathType Leaf)) {
	& (Join-Path $PSScriptRoot 'Test-AgentToolsCapabilities.ps1') -WorktreeCliExecutable $worktreeCli -AgentHarnessExecutable $agentHarness | Out-Null
	Write-Host "Primary AgentTools already available at '$worktreeCli' and '$agentHarness'."
	return
}

$msBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msBuild -PathType Leaf)) {
	$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) { throw "Visual Studio locator is missing: '$vsWhere'." }
	$installPaths = @(& $vsWhere -latest -version '[18.0,19.0)' -products * -requires Microsoft.Component.MSBuild -property installationPath)
	if ($LASTEXITCODE -ne 0 -or $installPaths.Count -ne 1 -or [string]::IsNullOrWhiteSpace($installPaths[0])) { throw 'Unable to locate a Visual Studio installation containing MSBuild.' }
	$msBuild = Join-Path $installPaths[0].Trim() 'MSBuild\Current\Bin\MSBuild.exe'
	if (-not (Test-Path -LiteralPath $msBuild -PathType Leaf)) { throw "MSBuild is missing: '$msBuild'." }
}

$solutions = @(
	(Join-Path $root 'Tools\WorktreeCli\Platforms\VisualStudio2026\WorktreeCli.sln'),
	(Join-Path $root 'Tools\AgentHarness\Platforms\VisualStudio2026\AgentHarness.sln')
)
foreach ($solution in $solutions) { if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) { throw "AgentTools solution is missing: '$solution'." } }
$maintenance = $null
$sessionWorktree = $env:BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE
if ([string]::IsNullOrWhiteSpace($sessionWorktree)) { throw 'AgentTools bootstrap requires wrapper session worktree identity.' }
try {
	if ($env:BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE -eq 'maintenance') { $maintenance = [pscustomobject]@{ Owner = $owner } }
	else { $maintenance = Enter-WorktreeCliMaintenance -RepositoryRoot $root -Owner $owner -Label 'wrapper bootstrap' -Worktree $root -WaitSeconds $WaitSeconds -UpgradeSession }
	foreach ($solution in $solutions) {
		$exitCode = Invoke-WorktreeCliTrackedProcess -Executable $msBuild -ArgumentList @($solution, '/p:Configuration=Release', '/p:Platform=x64', '/p:EnableClangTidyCodeAnalysis=false', '/p:RunCodeAnalysis=false', '/verbosity:minimal') -WorkingDirectory $root
		if ($exitCode -ne 0) { throw "AgentTools bootstrap build failed for '$solution' with exit code $exitCode." }
	}
	& (Join-Path $PSScriptRoot 'Test-AgentToolsCapabilities.ps1') -WorktreeCliExecutable $worktreeCli -AgentHarnessExecutable $agentHarness | Out-Null
	Write-Host "Built primary AgentTools at '$worktreeCli' and '$agentHarness'."
}
finally {
	if ($null -ne $maintenance) { Exit-WorktreeCliMaintenance -RepositoryRoot $root -Owner $owner -DowngradeToSession -Label 'wrapper session' -Worktree $sessionWorktree }
	if ($null -ne $maintenance) { $env:BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE = 'session' }
}
