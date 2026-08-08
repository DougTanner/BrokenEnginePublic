# Resolves the deterministic build context /compile needs before any build command runs:
# repository identities, the changed-path set, the path-rule-derived data mode and its
# directories, and optionally DevEnvDir. Read-only: it launches no writing command and
# emits nothing but stdout JSON and stderr diagnostics.
#
# dataBuildMode is derived from path rules alone. The semantic Local triggers
# (generated-header logic, exporter versions/fingerprints, compression, chunk layout,
# pack/manifest contracts), the "may affect generated or serialized bytes" catch-all,
# Local-generation authorization, and the deletion-only exception all remain agent
# judgments this script cannot make.
[CmdletBinding()]
param(
	[string] $RepositoryRoot,
	[string] $Baseline,
	[string] $PrimaryCheckout,
	[switch] $IncludeDevEnvDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'AgentScriptCommon.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
Import-Module (Join-Path $sharedScripts 'AgentScriptCommon.psm1') -Force
Import-Module (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1') -Force

$script:MaximumReportedPaths = 512
$script:MaximumOutputBytes = 65536
$script:DataOutputSuffix = 'Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data'
# The four path-matchable Local triggers stated in SKILL.md.
$script:PathTriggers = @(
	[pscustomobject]@{ trigger = 'DataPacker/**'; prefix = 'DataPacker/'; exact = $null }
	[pscustomobject]@{ trigger = 'Engine/Data/**'; prefix = 'Engine/Data/'; exact = $null }
	[pscustomobject]@{ trigger = 'Projects/BrokenEngineSandbox/Data/**'; prefix = 'Projects/BrokenEngineSandbox/Data/'; exact = $null }
	[pscustomobject]@{ trigger = 'Common/DataFile.h'; prefix = $null; exact = 'Common/DataFile.h' }
)

$script:BlockedCode = $null
$script:Result = [ordered]@{
	schemaVersion = 'broken-engine-compile-context/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Compile context resolution did not run.'
	repositoryRoot = $null
	primaryCheckout = $null
	baseline = $null
	changedPaths = @()
	changedPathCount = 0
	changedPathsTruncated = $false
	triggerMatches = @()
	triggerMatchCount = 0
	triggerMatchesTruncated = $false
	deletionOnlyCandidates = @()
	deletionOnlyCandidateCount = 0
	deletionOnlyCandidatesTruncated = $false
	dataBuildMode = $null
	dataBuildModeDerivation = 'path-rules-only'
	gameDataDirectory = $null
	generatedDataIncludeRoot = $null
}
if ($IncludeDevEnvDir) { $script:Result.devEnvDir = $null }

function Complete-CompileContext([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$script:Result.status = $Status
	$script:Result.code = $Code
	$script:Result.message = $Message
	$json = $script:Result | ConvertTo-Json -Depth 8 -Compress
	if ([Text.Encoding]::UTF8.GetByteCount($json) -gt $script:MaximumOutputBytes) {
		# Never elide reported paths silently: drop the lists, flag them, and block.
		$script:Result.changedPaths = @()
		$script:Result.changedPathsTruncated = $true
		$script:Result.triggerMatches = @()
		$script:Result.triggerMatchesTruncated = $true
		$script:Result.deletionOnlyCandidates = @()
		$script:Result.deletionOnlyCandidatesTruncated = $true
		$script:Result.status = 'blocked'
		$script:Result.code = 'output.capacity-exceeded'
		$script:Result.message = "Compile context exceeds the $($script:MaximumOutputBytes)-byte output cap; the changed-path evidence cannot be reported completely."
		$json = $script:Result | ConvertTo-Json -Depth 8 -Compress
		$ExitCode = 2
	}
	[Console]::Out.Write($json)
	[Console]::Error.WriteLine("compile-context: $($script:Result.status) $($script:Result.code) - $($script:Result.message)")
	exit $ExitCode
}

function Stop-CompileContext([string] $Code, [string] $Message) {
	$script:BlockedCode = $Code
	throw $Message
}

function Invoke-ContextGit([string[]] $Arguments) {
	# $PSScriptRoot is a known-good working directory; every repository selection is an explicit -C argument.
	return Invoke-FinalizeNativeText 'git.exe' $Arguments $PSScriptRoot
}

function Get-ContextGitText([string[]] $Arguments, [string] $Code, [string] $Message) {
	$response = Invoke-ContextGit $Arguments
	if ($response.ExitCode -ne 0) { Stop-CompileContext $Code "$Message git exit $($response.ExitCode): $($response.Stderr.Trim())" }
	return $response.Stdout
}

function Get-ContextCanonicalDirectory([string] $Path, [string] $Code, [string] $Label) {
	if ([string]::IsNullOrWhiteSpace($Path)) { Stop-CompileContext $Code "$Label is empty." }
	$canonical = $null
	try { $canonical = Get-AgentCanonicalPath $Path.Trim() }
	catch { Stop-CompileContext $Code "$Label is not a usable path: '$Path'." }
	if (-not [IO.Path]::IsPathFullyQualified($canonical)) { Stop-CompileContext $Code "$Label must be an absolute path: '$canonical'." }
	$item = Get-Item -LiteralPath $canonical -Force -ErrorAction SilentlyContinue
	if ($null -eq $item -or -not $item.PSIsContainer) { Stop-CompileContext $Code "$Label must be an existing directory: '$canonical'." }
	return $canonical
}

function Select-ContextInput([string] $Supplied, [string] $EnvironmentName) {
	if (-not [string]::IsNullOrWhiteSpace($Supplied)) { return $Supplied.Trim() }
	$hint = [Environment]::GetEnvironmentVariable($EnvironmentName)
	if (-not [string]::IsNullOrWhiteSpace($hint)) { return $hint.Trim() }
	return $null
}

function Get-ContextNormalizedPaths([string] $Text) {
	$paths = [Collections.Generic.List[string]]::new()
	foreach ($line in ($Text -split "`n")) {
		$value = $line.TrimEnd("`r").Trim()
		if ($value.Length -eq 0) { continue }
		$paths.Add($value.Replace('\', '/'))
	}
	return $paths
}

function Get-ContextTrigger([string] $Path) {
	foreach ($trigger in $script:PathTriggers) {
		if ($null -ne $trigger.exact -and $Path.Equals($trigger.exact, [StringComparison]::OrdinalIgnoreCase)) { return $trigger.trigger }
		if ($null -ne $trigger.prefix -and $Path.StartsWith($trigger.prefix, [StringComparison]::OrdinalIgnoreCase)) { return $trigger.trigger }
	}
	return $null
}

function Get-ContextDevEnvDirectory {
	$candidate = Join-Path 'C:\Program Files\Microsoft Visual Studio\18\Community' 'Common7\IDE'
	if (-not (Test-ContextIdeDirectory $candidate)) {
		$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
		if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) { Stop-CompileContext 'dev-env-dir.unresolved' "Visual Studio 2026 is not at the fixed install path and vswhere is missing at '$vsWhere'." }
		$response = Invoke-FinalizeNativeText $vsWhere @('-latest', '-version', '[18.0,19.0)', '-products', '*', '-requires', 'Microsoft.Component.MSBuild', '-property', 'installationPath') $PSScriptRoot
		$install = $response.Stdout.Trim()
		if ($response.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($install)) { Stop-CompileContext 'dev-env-dir.unresolved' "vswhere did not report a Visual Studio 2026 installation: $($response.Stderr.Trim())" }
		$candidate = Join-Path ($install -split "`n")[0].Trim() 'Common7\IDE'
		if (-not (Test-ContextIdeDirectory $candidate)) { Stop-CompileContext 'dev-env-dir.unresolved' "Visual Studio IDE executable missing under '$candidate'." }
	}
	return (Get-AgentCanonicalPath $candidate) + '\'
}

function Test-ContextIdeDirectory([string] $Directory) {
	return (Test-Path -LiteralPath (Join-Path $Directory 'devenv.com') -PathType Leaf) -or (Test-Path -LiteralPath (Join-Path $Directory 'devenv.exe') -PathType Leaf)
}

try {
	# Identity precedence per input: explicit parameter, then wrapper environment hint, then derived default.
	$rootInput = Select-ContextInput $RepositoryRoot 'BROKEN_ENGINE_WORKTREE_PATH'
	if ($null -eq $rootInput) {
		$rootInput = (Get-ContextGitText @('-C', $PSScriptRoot, 'rev-parse', '--show-toplevel') 'repository-root.unresolved' 'Could not derive the repository root from this script location:').Trim()
	}
	$root = Get-ContextCanonicalDirectory $rootInput 'repository-root.unresolved' 'RepositoryRoot'
	$topLevel = Get-ContextCanonicalDirectory (Get-ContextGitText @('-C', $root, 'rev-parse', '--show-toplevel') 'repository-root.unresolved' "'$root' is not inside a Git working tree:").Trim() 'repository-root.unresolved' 'Repository top level'
	if (-not $topLevel.Equals($root, [StringComparison]::OrdinalIgnoreCase)) {
		Stop-CompileContext 'repository-root.mismatch' "RepositoryRoot '$root' is not its own Git top level '$topLevel'."
	}
	$script:Result.repositoryRoot = $root

	$primaryInput = Select-ContextInput $PrimaryCheckout 'BROKEN_ENGINE_PRIMARY_CHECKOUT'
	if ($null -eq $primaryInput) {
		$commonDirectory = Get-ContextCanonicalDirectory (Get-ContextGitText @('-C', $root, 'rev-parse', '--path-format=absolute', '--git-common-dir') 'primary-checkout.unresolved' 'Could not resolve the Git common directory:').Trim() 'primary-checkout.unresolved' 'Git common directory'
		$primaryInput = [IO.Path]::GetDirectoryName($commonDirectory)
	}
	$primary = Get-ContextCanonicalDirectory $primaryInput 'primary-checkout.unresolved' 'PrimaryCheckout'
	$primaryGit = Get-Item -LiteralPath (Join-Path $primary '.git') -Force -ErrorAction SilentlyContinue
	if ($null -eq $primaryGit -or -not $primaryGit.PSIsContainer -or ($primaryGit.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		Stop-CompileContext 'primary-checkout.invalid' "PrimaryCheckout must contain an ordinary '.git' directory: '$primary'."
	}
	$script:Result.primaryCheckout = $primary

	# A supplied baseline is authoritative and is never advanced to a later HEAD.
	$baselineInput = Select-ContextInput $Baseline 'BROKEN_ENGINE_BASELINE'
	if ($null -eq $baselineInput) { $baselineInput = 'HEAD' }
	$baselineResponse = Invoke-ContextGit @('-C', $root, 'rev-parse', '--verify', '--quiet', '--end-of-options', "$baselineInput^{commit}")
	$resolvedBaseline = $baselineResponse.Stdout.Trim()
	if ($baselineResponse.ExitCode -ne 0 -or $resolvedBaseline -cnotmatch '^[0-9a-f]{40}$') {
		Stop-CompileContext 'baseline.unresolved' "Baseline '$baselineInput' does not resolve to a commit in '$root'."
	}
	$script:Result.baseline = $resolvedBaseline

	# One baseline diff covers committed, staged, and unstaged tracked changes; ls-files adds untracked files.
	$diffText = Get-ContextGitText @('-c', 'core.quotepath=false', '-C', $root, 'diff', '--name-only', $resolvedBaseline, '--') 'changed-paths.unavailable' 'Could not list changed paths:'
	$untrackedText = Get-ContextGitText @('-c', 'core.quotepath=false', '-C', $root, 'ls-files', '--others', '--exclude-standard') 'changed-paths.unavailable' 'Could not list untracked files:'
	$changed = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
	foreach ($path in (Get-ContextNormalizedPaths $diffText)) { [void] $changed.Add($path) }
	foreach ($path in (Get-ContextNormalizedPaths $untrackedText)) { [void] $changed.Add($path) }

	$triggerMatches = [Collections.Generic.List[object]]::new()
	foreach ($path in $changed) {
		$trigger = Get-ContextTrigger $path
		if ($null -ne $trigger) { $triggerMatches.Add([pscustomobject][ordered]@{ path = $path; trigger = $trigger }) }
	}

	# Deletion-only evidence: baseline diff status 'D' only. Rename sides (R*), additions, and
	# modifications are excluded. Resolving the deletion-only exception remains an agent decision.
	$statusText = Get-ContextGitText @('-c', 'core.quotepath=false', '-C', $root, 'diff', '--name-status', $resolvedBaseline, '--') 'changed-paths.unavailable' 'Could not list changed-path statuses:'
	$deletions = [Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
	foreach ($line in ($statusText -split "`n")) {
		$fields = $line.TrimEnd("`r").Split("`t")
		if ($fields.Count -lt 2) { continue }
		$status = $fields[0].Trim()
		if ($status.Length -eq 0 -or $status[0] -ne 'D') { continue }
		$path = $fields[1].Trim().Replace('\', '/')
		if ($path.Length -gt 0 -and $null -ne (Get-ContextTrigger $path)) { [void] $deletions.Add($path) }
	}

	$script:Result.changedPathCount = $changed.Count
	$script:Result.changedPathsTruncated = $changed.Count -gt $script:MaximumReportedPaths
	$script:Result.changedPaths = @(@($changed) | Select-Object -First $script:MaximumReportedPaths)
	$script:Result.triggerMatchCount = $triggerMatches.Count
	$script:Result.triggerMatchesTruncated = $triggerMatches.Count -gt $script:MaximumReportedPaths
	$script:Result.triggerMatches = @($triggerMatches | Select-Object -First $script:MaximumReportedPaths)
	$script:Result.deletionOnlyCandidateCount = $deletions.Count
	$script:Result.deletionOnlyCandidatesTruncated = $deletions.Count -gt $script:MaximumReportedPaths
	$script:Result.deletionOnlyCandidates = @(@($deletions) | Select-Object -First $script:MaximumReportedPaths)

	$script:Result.dataBuildMode = if ($triggerMatches.Count -gt 0) { 'Local' } else { 'Shared' }
	$dataOwner = if ($script:Result.dataBuildMode -ceq 'Local') { $root } else { $primary }
	$gameDataDirectory = Get-AgentCanonicalPath (Join-Path $dataOwner $script:DataOutputSuffix)
	$script:Result.gameDataDirectory = $gameDataDirectory
	$script:Result.generatedDataIncludeRoot = Get-AgentCanonicalPath ([IO.Path]::GetDirectoryName($gameDataDirectory))

	if ($IncludeDevEnvDir) { $script:Result.devEnvDir = Get-ContextDevEnvDirectory }

	if ($script:Result.changedPathsTruncated -or $script:Result.triggerMatchesTruncated -or $script:Result.deletionOnlyCandidatesTruncated) {
		Complete-CompileContext 2 'blocked' 'changed-paths.capacity-exceeded' "The changed-path evidence exceeds the $($script:MaximumReportedPaths)-entry report cap and cannot be reported completely."
	}
	Complete-CompileContext 0 'pass' 'ok' "Compile context resolved; data mode $($script:Result.dataBuildMode) from path rules."
}
catch {
	if ($null -ne $script:BlockedCode) { Complete-CompileContext 2 'blocked' $script:BlockedCode $_.Exception.Message }
	Complete-CompileContext 1 'error' 'internal.error' $_.Exception.Message
}
