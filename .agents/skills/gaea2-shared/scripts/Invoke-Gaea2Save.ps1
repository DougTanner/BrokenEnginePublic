# Chain wrapper for /gaea2-save: resolves the Markdown input and the .terrain
# output under the documented defaults, dispatches save_terrain.py through
# Invoke-Gaea2Python.ps1, and returns the output path plus the saver's WARNING
# lines as structured entries. It never interprets a warning and never edits a
# terrain file; offering the original path back is the skill's prose, so this
# wrapper only applies the default output path.
[CmdletBinding()]
param(
	[string] $InputPath = '',
	[string] $Output = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# The dispatched wrapper reports failures as JSON with a nonzero exit code; a
# nonzero native exit code must not throw here.
$PSNativeCommandUseErrorActionPreference = $false

$script:MaximumOutputCharacters = 8192
$script:MaximumWarnings = 64
$script:RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$script:PythonWrapper = Join-Path $PSScriptRoot 'Invoke-Gaea2Python.ps1'
# Both the dispatched envelope and this one stay lossless: the console code page
# would otherwise best-fit their non-ASCII characters down to ASCII.
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)

$result = [ordered]@{
	schemaVersion = 'broken-engine-gaea2-save/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Terrain save did not run.'
	inputPath = $null
	outputPath = $null
	interpreter = $null
	pythonVersion = $null
	installCommand = $null
	warnings = @()
	stdout = ''
	stderr = ''
	truncated = $false
}

function Complete-Gaea2Save([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
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

function Get-Gaea2Warnings([string] $Text) {
	$entries = @()
	if ([string]::IsNullOrEmpty($Text)) { return $entries }
	foreach ($line in ($Text -split "`r?`n")) {
		$trimmed = $line.Trim()
		if ($trimmed -cmatch '^WARNING:\s*(?<detail>.*)$') {
			if ($entries.Count -ge $script:MaximumWarnings) {
				$result.truncated = $true
				break
			}
			$entries += $Matches['detail'].Trim()
		}
	}
	return $entries
}

try {
	if ([string]::IsNullOrWhiteSpace($InputPath)) {
		Complete-Gaea2Save 2 'blocked' 'input.needs-user' 'Ask the user which Markdown view to save: no input name or path was supplied.'
	}
	$resolved = $InputPath
	if ([string]::IsNullOrEmpty([IO.Path]::GetDirectoryName($resolved))) {
		if ([IO.Path]::GetExtension($resolved) -ine '.md') { $resolved = "$resolved.md" }
		$resolved = Join-Path $script:RepositoryRoot (Join-Path 'Temp' $resolved)
	}
	elseif (-not [IO.Path]::IsPathRooted($resolved)) {
		$resolved = Join-Path $script:RepositoryRoot $resolved
	}
	$resolved = [IO.Path]::GetFullPath($resolved)
	$result.inputPath = $resolved
	if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
		Complete-Gaea2Save 1 'error' 'input.not-found' "Markdown view not found: '$resolved'."
	}

	$outputPath = $Output
	if ([string]::IsNullOrWhiteSpace($outputPath)) {
		$outputPath = Join-Path ([IO.Path]::GetDirectoryName($resolved)) "$([IO.Path]::GetFileNameWithoutExtension($resolved)).terrain"
	}
	elseif (-not [IO.Path]::IsPathRooted($outputPath)) {
		$outputPath = Join-Path $script:RepositoryRoot $outputPath
	}
	$outputPath = [IO.Path]::GetFullPath($outputPath)
	$result.outputPath = $outputPath

	$save = Invoke-Gaea2PythonScript 'save_terrain.py' @($resolved, '--output', $outputPath)
	$result.interpreter = $save.interpreter
	$result.pythonVersion = $save.pythonVersion
	$result.installCommand = $save.installCommand
	$result.stdout = Get-Gaea2CappedText $save.stdout
	$result.stderr = Get-Gaea2CappedText $save.stderr
	# The dispatched envelope may already have cut the saver's streams, so the
	# warnings below can be incomplete for a reason this envelope cannot see.
	if ($save.truncated) { $result.truncated = $true }
	$result.warnings = @(Get-Gaea2Warnings (($save.stderr, $save.stdout) -join "`n"))
	if ($save.code -ceq 'python.missing' -or $save.code -ceq 'python.stale') {
		Complete-Gaea2Save 2 'blocked' $save.code $save.message
	}
	if ($save.status -cne 'pass') {
		Complete-Gaea2Save 1 'error' 'save.failed' $save.message
	}
	if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
		Complete-Gaea2Save 1 'error' 'save.output-missing' "The saver reported success but did not write '$outputPath'."
	}
	Complete-Gaea2Save 0 'pass' 'ok' "Saved '$resolved' to '$outputPath'."
}
catch {
	Complete-Gaea2Save 1 'error' 'internal.error' $_.Exception.Message
}
