# Stable analysis manifest for one scope: the resolved file list, each file's
# ordered root-to-file AGENTS.md authority chain, and its bt-token-v1 size from
# Measure-Tokens.ps1. reduceFileCandidate is a routing observation for
# /reduce-file, never a defect claim.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $Path,
	[switch] $Recurse,
	[string[]] $Extension
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$script:MeasureTokensScript = Join-Path $PSScriptRoot 'Measure-Tokens.ps1'
$script:MaximumFiles = 80
$script:MaximumMessageLength = 256
$script:HeaderTokenThreshold = 5000
$script:ImplementationTokenThreshold = 10000

$result = [ordered]@{
	schemaVersion = 'broken-engine-analysis-manifest/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Analysis manifest did not run.'
	metric = 'bt-token-v1'
	scope = [ordered]@{
		path = $Path
		kind = $null
		recurse = [bool]$Recurse
		extensions = @()
	}
	fileLimit = $script:MaximumFiles
	fileCount = 0
	totalTokens = 0
	files = @()
}

function Complete-AnalysisManifest([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message)
{
	$result.status = $Status
	$result.code = $Code
	$result.message = if ($Message.Length -gt $script:MaximumMessageLength) { $Message.Substring(0, $script:MaximumMessageLength) } else { $Message }
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 32 -Compress))
	exit $ExitCode
}

function Get-RepositoryRelativePath([string] $FullPath)
{
	return [IO.Path]::GetRelativePath($script:RepoRoot, $FullPath).Replace([IO.Path]::DirectorySeparatorChar, '/')
}

# Every AGENTS.md from the repository root down to the file's own directory, in
# that order, skipping directories that have none.
function Get-AuthorityChain([string] $FileFullPath)
{
	$chain = [Collections.Generic.List[string]]::new()
	$directories = [Collections.Generic.List[string]]::new()
	$directory = [IO.Path]::GetDirectoryName($FileFullPath)
	while ($directory -and $directory.Length -ge $script:RepoRoot.Length)
	{
		$directories.Insert(0, $directory)
		if ([string]::Equals($directory, $script:RepoRoot, [StringComparison]::OrdinalIgnoreCase)) { break }
		$directory = [IO.Path]::GetDirectoryName($directory)
	}

	foreach ($candidateDirectory in $directories)
	{
		$candidate = Join-Path $candidateDirectory 'AGENTS.md'
		if (Test-Path -LiteralPath $candidate -PathType Leaf) { $chain.Add((Get-RepositoryRelativePath $candidate)) }
	}

	return @($chain)
}

try
{
	$resolved = if ([IO.Path]::IsPathRooted($Path)) { $Path } elseif (Test-Path -LiteralPath $Path) { $Path } else { Join-Path $script:RepoRoot $Path }
	if (-not (Test-Path -LiteralPath $resolved))
	{
		Complete-AnalysisManifest 1 'error' 'analysis-manifest.path-not-found' "Path does not exist: '$Path'."
	}

	$item = Get-Item -LiteralPath $resolved -Force
	$fullPath = [IO.Path]::GetFullPath($item.FullName)
	if (-not $fullPath.StartsWith($script:RepoRoot, [StringComparison]::OrdinalIgnoreCase))
	{
		Complete-AnalysisManifest 1 'error' 'analysis-manifest.path-outside-repository' "Path is outside the repository: '$Path'."
	}

	$extensions = @()
	if ($PSBoundParameters.ContainsKey('Extension'))
	{
		# Splitting on ',' keeps -Extension usable under `pwsh -File`, where
		# `.h,.cpp` binds as one string instead of an array.
		$extensions = @($Extension | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | ForEach-Object { $_ -split ',' } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
	}

	$result.scope.extensions = $extensions
	$scopeFiles = if ($item.PSIsContainer)
	{
		$result.scope.kind = 'directory'
		@(Get-ChildItem -LiteralPath $fullPath -File -Recurse:$Recurse)
	}
	else
	{
		$result.scope.kind = 'file'
		@($item)
	}

	if ($extensions.Count -gt 0)
	{
		$scopeFiles = @($scopeFiles | Where-Object { $extensions -contains $_.Extension })
	}

	$scopeFiles = @($scopeFiles | Sort-Object -Property FullName)
	$result.fileCount = $scopeFiles.Count
	if ($scopeFiles.Count -gt $script:MaximumFiles)
	{
		Complete-AnalysisManifest 2 'blocked' 'analysis-manifest.scope-too-large' "Scope resolves to $($scopeFiles.Count) files, above the $($script:MaximumFiles) file limit; narrow the scope to a smaller directory, drop -Recurse, or filter with -Extension."
	}

	if ($scopeFiles.Count -eq 0)
	{
		Complete-AnalysisManifest 0 'pass' 'ok' 'Scope resolves to no files.'
	}

	$measured = @(& $script:MeasureTokensScript -Path @($scopeFiles | ForEach-Object { $_.FullName }))
	$tokensByPath = @{}
	foreach ($measurement in $measured) { $tokensByPath[$measurement.Path] = [int64]$measurement.Tokens }

	$files = [Collections.Generic.List[object]]::new()
	foreach ($file in $scopeFiles)
	{
		$tokens = [int64]$tokensByPath[$file.FullName]
		$result.totalTokens += $tokens
		$candidate = switch ($file.Extension.ToLowerInvariant())
		{
			'.h' { $tokens -gt $script:HeaderTokenThreshold }
			'.cpp' { $tokens -gt $script:ImplementationTokenThreshold }
			default { $false }
		}

		$files.Add([ordered]@{
			path = Get-RepositoryRelativePath $file.FullName
			tokens = $tokens
			authorityChain = @(Get-AuthorityChain $file.FullName)
			reduceFileCandidate = [bool]$candidate
		})
	}

	$result.files = @($files)
	Complete-AnalysisManifest 0 'pass' 'ok' "Manifest covers $($result.fileCount) files totalling $($result.totalTokens) bt-token-v1."
}
catch
{
	Complete-AnalysisManifest 1 'error' 'internal.error' $_.Exception.Message
}
