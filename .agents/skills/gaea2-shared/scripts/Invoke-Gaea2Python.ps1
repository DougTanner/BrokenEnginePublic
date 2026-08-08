# Detect-and-dispatch wrapper for the gaea2 Python scripts. It resolves the
# interpreter through the shared .agents/scripts/Detect-Python.ps1 probe, then runs the requested
# script from this directory and returns one JSON envelope.
# The wrapper never installs software: a missing or too-old interpreter is a
# blocked result carrying the probe line and the suggested install command as
# data, for the skill to report to the user.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $Script,
	[string[]] $Arguments = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:MaximumOutputCharacters = 8192
$script:RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'Detect-Python.ps1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
$script:ProbeScript = Join-Path $sharedScripts 'Detect-Python.ps1'
$script:Utf8 = [Text.UTF8Encoding]::new($false)
# The envelope stays lossless: the console code page would otherwise best-fit
# the non-ASCII characters of the Python messages down to ASCII.
[Console]::OutputEncoding = $script:Utf8

$result = [ordered]@{
	schemaVersion = 'broken-engine-gaea2-python/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Python dispatch did not run.'
	interpreter = $null
	pythonVersion = $null
	probeLine = $null
	installCommand = $null
	script = $null
	arguments = @($Arguments)
	workingDirectory = $script:RepositoryRoot
	exitCode = $null
	stdout = ''
	stderr = ''
	truncated = $false
}

function Complete-Gaea2Python([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
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

function Invoke-Gaea2Native([string] $Executable, [string[]] $ProcessArguments) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $Executable
	$start.WorkingDirectory = $script:RepositoryRoot
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = $script:Utf8
	$start.StandardErrorEncoding = $script:Utf8
	# Redirected Python stdio otherwise follows the console code page, which
	# replaces the non-ASCII characters its messages use.
	$start.Environment['PYTHONIOENCODING'] = 'utf-8'
	foreach ($argument in $ProcessArguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start '$Executable'." }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$response = [pscustomobject]@{
		ExitCode = $process.ExitCode
		Stdout = $stdoutTask.GetAwaiter().GetResult()
		Stderr = $stderrTask.GetAwaiter().GetResult()
	}
	$process.Dispose()
	return $response
}

try {
	if ([string]::IsNullOrWhiteSpace($Script)) {
		Complete-Gaea2Python 1 'error' 'script.not-found' 'No Python script name was supplied.'
	}
	# A bare name is a sibling script; a relative path is repository-relative.
	# Neither resolves against the current directory.
	$scriptPath = $Script
	if (-not [IO.Path]::IsPathRooted($scriptPath)) {
		if ($scriptPath.Contains('/') -or $scriptPath.Contains('\')) {
			$scriptPath = Join-Path $script:RepositoryRoot $scriptPath
		}
		else {
			$scriptPath = Join-Path $PSScriptRoot $scriptPath
		}
	}
	$scriptPath = [IO.Path]::GetFullPath($scriptPath)
	$result.script = $scriptPath
	if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
		Complete-Gaea2Python 1 'error' 'script.not-found' "Python script not found: '$scriptPath'."
	}
	if (-not (Test-Path -LiteralPath $script:ProbeScript -PathType Leaf)) {
		Complete-Gaea2Python 1 'error' 'probe.not-found' "Interpreter probe not found: '$($script:ProbeScript)'."
	}

	# The probe runs in its own pwsh process so its exit codes and native-command
	# behavior stay independent of this script's strict-mode settings.
	$probe = Invoke-Gaea2Native 'pwsh' @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script:ProbeScript)
	if (-not [string]::IsNullOrWhiteSpace($probe.Stderr)) { [Console]::Error.Write($probe.Stderr) }
	$probeLine = ($probe.Stdout -split "`r?`n" | Where-Object { $_.Trim() -cmatch '^(OK|MISSING|STALE)\b' } | Select-Object -First 1)
	if ($null -eq $probeLine) {
		$result.exitCode = $probe.ExitCode
		$result.stdout = Get-Gaea2CappedText $probe.Stdout
		$result.stderr = Get-Gaea2CappedText $probe.Stderr
		Complete-Gaea2Python 1 'error' 'python.probe-failed' "The interpreter probe returned no OK/MISSING/STALE line (exit $($probe.ExitCode))."
	}
	$probeLine = $probeLine.Trim()
	$result.probeLine = $probeLine
	if ($probeLine -cmatch 'install with:\s*(?<command>.+)$') { $result.installCommand = $Matches['command'].Trim() }
	if ($probeLine -cmatch '^MISSING\b') {
		Complete-Gaea2Python 2 'blocked' 'python.missing' "No usable Python interpreter is installed: $probeLine"
	}
	if ($probeLine -cmatch '^STALE\b') {
		Complete-Gaea2Python 2 'blocked' 'python.stale' "The installed Python interpreter is too old: $probeLine"
	}
	if ($probeLine -cnotmatch '^OK\s+(?<interpreter>.+?)\s+Python\s+(?<version>\d+\.\d+)$') {
		Complete-Gaea2Python 1 'error' 'python.probe-failed' "Could not parse the interpreter probe line: $probeLine"
	}
	$result.interpreter = $Matches['interpreter'].Trim()
	$result.pythonVersion = $Matches['version']

	$child = Invoke-Gaea2Native $result.interpreter (@($scriptPath) + $Arguments)
	if (-not [string]::IsNullOrWhiteSpace($child.Stderr)) { [Console]::Error.Write($child.Stderr) }
	$result.exitCode = $child.ExitCode
	$result.stdout = Get-Gaea2CappedText $child.Stdout
	$result.stderr = Get-Gaea2CappedText $child.Stderr
	if ($child.ExitCode -eq 0) {
		Complete-Gaea2Python 0 'pass' 'ok' "$([IO.Path]::GetFileName($scriptPath)) completed."
	}
	Complete-Gaea2Python 1 'error' 'python.script-failed' "$([IO.Path]::GetFileName($scriptPath)) exited with $($child.ExitCode)."
}
catch {
	Complete-Gaea2Python 1 'error' 'internal.error' $_.Exception.Message
}
