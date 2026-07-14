[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $RepositoryRoot,
	[int] $WaitSeconds = 660
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

if (-not [System.IO.Path]::IsPathRooted($RepositoryRoot)) { throw 'RepositoryRoot must be absolute.' }
$root = Get-CanonicalPath $RepositoryRoot
$topLevel = Get-CanonicalPath (@(Invoke-Git @('-C', $root, 'rev-parse', '--show-toplevel'))[0].Trim())
if (-not $topLevel.Equals($root, [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot is not the repository root: '$root'." }
$gitDirectory = Get-Item -LiteralPath (Join-Path $root '.git') -Force -ErrorAction Stop
if (-not $gitDirectory.PSIsContainer -or ($gitDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
	throw "RepositoryRoot is not the primary checkout: '$root'."
}
$commonDir = Get-CanonicalPath (@(Invoke-Git @('-C', $root, 'rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
if (-not $commonDir.Equals((Get-CanonicalPath $gitDirectory.FullName), [StringComparison]::OrdinalIgnoreCase)) {
	throw "RepositoryRoot is not the primary checkout: '$root'."
}
$owner = $env:BROKEN_ENGINE_AGENTCLI_SESSION_OWNER
if ([string]::IsNullOrWhiteSpace($owner)) { throw 'AgentCli bootstrap requires a live wrapper session claim.' }
Assert-AgentCliSessionOwner -RepositoryRoot $root -Owner $owner

$output = Join-Path $root 'Tools\AgentCli\Platforms\VisualStudio2026\Output'
$outputItem = Get-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
if ($null -ne $outputItem -and (-not $outputItem.PSIsContainer -or ($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint))) {
	throw "Primary AgentCli Output must be an ordinary directory: '$output'."
}
$agentCli = Join-Path $output 'AgentCli.exe'
$agentCliItem = Get-Item -LiteralPath $agentCli -Force -ErrorAction SilentlyContinue
if ($null -ne $agentCliItem) {
	& (Join-Path $PSScriptRoot 'Test-AgentCliCapabilities.ps1') -Executable $agentCli | Out-Null
	Write-Host "Primary AgentCli already available at '$agentCli'."
	return
}

$msBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msBuild -PathType Leaf)) {
	$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) { throw "Visual Studio locator is missing: '$vsWhere'." }
	$installPaths = @(& $vsWhere -latest -version '[18.0,19.0)' -products * -requires Microsoft.Component.MSBuild -property installationPath)
	if ($LASTEXITCODE -ne 0 -or $installPaths.Count -ne 1 -or [string]::IsNullOrWhiteSpace($installPaths[0])) {
		throw 'Unable to locate a Visual Studio installation containing MSBuild.'
	}
	$msBuild = Join-Path $installPaths[0].Trim() 'MSBuild\Current\Bin\MSBuild.exe'
	if (-not (Test-Path -LiteralPath $msBuild -PathType Leaf)) { throw "MSBuild is missing: '$msBuild'." }
}

$solution = Join-Path $root 'Tools\AgentCli\Platforms\VisualStudio2026\AgentCli.sln'
if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) { throw "AgentCli solution is missing: '$solution'." }
$maintenance = $null
$sessionWorktree = $env:BROKEN_ENGINE_AGENTCLI_SESSION_WORKTREE
if ([string]::IsNullOrWhiteSpace($sessionWorktree)) { throw 'AgentCli bootstrap requires wrapper session worktree identity.' }
try {
	if ($env:BROKEN_ENGINE_AGENTCLI_ADMISSION_MODE -eq 'maintenance') { $maintenance = [pscustomobject]@{ Owner = $owner } }
	else { $maintenance = Enter-AgentCliMaintenance -RepositoryRoot $root -Owner $owner -Label 'wrapper bootstrap' -Worktree $root -WaitSeconds $WaitSeconds -UpgradeSession }
	$exitCode = Invoke-AgentCliTrackedProcess -Executable $msBuild -ArgumentList @($solution, '/p:Configuration=Release', '/p:Platform=x64', '/p:EnableClangTidyCodeAnalysis=false', '/p:RunCodeAnalysis=false', '/verbosity:minimal') -WorkingDirectory $root
	if ($exitCode -ne 0) { throw "AgentCli bootstrap build failed with exit code $exitCode." }
	& (Join-Path $PSScriptRoot 'Test-AgentCliCapabilities.ps1') -Executable $agentCli | Out-Null
	Write-Host "Built primary AgentCli at '$agentCli'."
}
finally {
	if ($null -ne $maintenance) { Exit-AgentCliMaintenance -RepositoryRoot $root -Owner $owner -DowngradeToSession -Label 'wrapper session' -Worktree $sessionWorktree }
	if ($null -ne $maintenance) { $env:BROKEN_ENGINE_AGENTCLI_ADMISSION_MODE = 'session' }
}
