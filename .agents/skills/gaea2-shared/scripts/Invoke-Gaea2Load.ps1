# Chain wrapper for /gaea2-load: resolves the .terrain input path, dispatches
# the Gaea version probe and the loader through Invoke-Gaea2Python.ps1, and
# returns the produced Markdown/passthrough paths plus the version-drift notice.
# The version probe is informational: its exit code becomes payload data and
# never this wrapper's exit code.
[CmdletBinding()]
param(
	[string] $InputPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# The dispatched wrapper reports failures as JSON with a nonzero exit code; a
# nonzero native exit code must not throw here.
$PSNativeCommandUseErrorActionPreference = $false

$script:MaximumOutputCharacters = 8192
$script:RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$script:PythonWrapper = Join-Path $PSScriptRoot 'Invoke-Gaea2Python.ps1'
$script:ExamplesDirectory = 'C:/Program Files/QuadSpinner/Gaea 2/Examples'
# Both the dispatched envelope and this one stay lossless: the console code page
# would otherwise best-fit their non-ASCII characters down to ASCII.
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)

$result = [ordered]@{
	schemaVersion = 'broken-engine-gaea2-load/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Terrain load did not run.'
	inputPath = $null
	interpreter = $null
	pythonVersion = $null
	installCommand = $null
	markdownPath = $null
	passthroughPath = $null
	versionDrift = $null
	stdout = ''
	stderr = ''
	truncated = $false
}

function Complete-Gaea2Load([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 32 -Compress))
	exit $ExitCode
}

function Get-Gaea2CappedText([string] $Text) {
	if ([string]::IsNullOrEmpty($Text)) { return $Text }
	if ($Text.Length -le $script:MaximumOutputCharacters) { return $Text }
	$result.truncated = $true
	return $Text.Substring(0, $script:MaximumOutputCharacters)
}

function Get-Gaea2QuotedLiteral([string] $Value) {
	return "'" + $Value.Replace("'", "''") + "'"
}

function Invoke-Gaea2PythonScript([string] $PythonScript, [string[]] $ScriptArguments) {
	# The wrapper runs through -Command, not -File: an argument such as
	# '--output' would otherwise be parsed as a parameter name of the wrapper.
	$command = "& $(Get-Gaea2QuotedLiteral $script:PythonWrapper) -Script $(Get-Gaea2QuotedLiteral $PythonScript)"
	if ($ScriptArguments.Count -gt 0) {
		$command += " -Arguments $((($ScriptArguments | ForEach-Object { Get-Gaea2QuotedLiteral $_ }) -join ','))"
	}
	$json = & pwsh -NoProfile -ExecutionPolicy Bypass -Command $command
	if ([string]::IsNullOrWhiteSpace($json)) { throw "Invoke-Gaea2Python.ps1 returned no envelope for '$PythonScript'." }
	return ($json -join '') | ConvertFrom-Json -Depth 32
}

try {
	if ([string]::IsNullOrWhiteSpace($InputPath) -or [IO.Path]::GetExtension($InputPath) -ine '.terrain') {
		Complete-Gaea2Load 2 'blocked' 'input.needs-user' 'Ask the user for the .terrain file to load: the supplied path is empty or does not end in .terrain.'
	}
	$resolved = $InputPath
	if ([string]::IsNullOrEmpty([IO.Path]::GetDirectoryName($resolved))) {
		$resolved = Join-Path $script:ExamplesDirectory $resolved
	}
	elseif (-not [IO.Path]::IsPathRooted($resolved)) {
		$resolved = Join-Path $script:RepositoryRoot $resolved
	}
	$resolved = [IO.Path]::GetFullPath($resolved)
	$result.inputPath = $resolved
	if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
		Complete-Gaea2Load 1 'error' 'input.not-found' "Terrain file not found: '$resolved'."
	}

	# Version drift is a notice, not a gate: only a failed dispatch (missing
	# interpreter, wrapper error) stops the load.
	$probe = Invoke-Gaea2PythonScript 'check_gaea_version.py' @()
	$result.interpreter = $probe.interpreter
	$result.pythonVersion = $probe.pythonVersion
	$result.installCommand = $probe.installCommand
	if ($probe.code -ceq 'python.missing' -or $probe.code -ceq 'python.stale') {
		Complete-Gaea2Load 2 'blocked' $probe.code $probe.message
	}
	if ($probe.status -cne 'pass' -and $probe.code -cne 'python.script-failed') {
		Complete-Gaea2Load 1 'error' $probe.code $probe.message
	}
	$driftState = switch ([int] $probe.exitCode) {
		0 { 'unchanged' }
		1 { 'changed' }
		2 { 'baseline-cached' }
		3 { 'undetected' }
		default { 'unknown' }
	}
	# A dispatched envelope that already cut its streams makes the notice below
	# incomplete for a reason this envelope cannot see.
	if ($probe.truncated) { $result.truncated = $true }
	$driftText = (($probe.stderr, $probe.stdout) -join "`n").Trim()
	$result.versionDrift = [ordered]@{
		exitCode = $probe.exitCode
		state = $driftState
		notice = Get-Gaea2CappedText $driftText
	}

	$load = Invoke-Gaea2PythonScript 'load_terrain.py' @($resolved)
	$result.stdout = Get-Gaea2CappedText $load.stdout
	$result.stderr = Get-Gaea2CappedText $load.stderr
	if ($load.truncated) { $result.truncated = $true }
	if ($load.code -ceq 'python.missing' -or $load.code -ceq 'python.stale') {
		$result.installCommand = $load.installCommand
		Complete-Gaea2Load 2 'blocked' $load.code $load.message
	}
	if ($load.status -cne 'pass') {
		Complete-Gaea2Load 1 'error' 'load.failed' $load.message
	}

	$baseName = [IO.Path]::GetFileNameWithoutExtension($resolved)
	$outputDirectory = Join-Path $script:RepositoryRoot 'Temp'
	$result.markdownPath = Join-Path $outputDirectory "$baseName.md"
	$result.passthroughPath = Join-Path $outputDirectory "$baseName.passthrough.json"
	if (-not (Test-Path -LiteralPath $result.markdownPath -PathType Leaf) -or -not (Test-Path -LiteralPath $result.passthroughPath -PathType Leaf)) {
		Complete-Gaea2Load 1 'error' 'load.output-missing' "The loader reported success but did not write '$($result.markdownPath)' and its sidecar."
	}
	Complete-Gaea2Load 0 'pass' 'ok' "Loaded '$resolved' into '$($result.markdownPath)'."
}
catch {
	Complete-Gaea2Load 1 'error' 'internal.error' $_.Exception.Message
}
