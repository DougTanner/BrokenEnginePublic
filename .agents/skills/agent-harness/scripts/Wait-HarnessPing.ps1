# Deadline-limited readiness poll of one harness port. A single failed or timed-out attempt is
# expected during long startup, so the loop keeps retrying until the port answers ok:true or the
# deadline passes. One port per call, so launch order stays with the agent.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $AgentHarness,
	[Parameter(Mandatory)][string] $Owner,
	[Parameter(Mandatory)][ValidateRange(1, 65535)][int] $Port,
	[ValidateRange(1, 600)][int] $TimeoutSeconds = 120,
	[ValidateRange(1, 600000)][int] $TimeoutMilliseconds = 2000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$MaximumMessageLength = 256
$PollMilliseconds = 250

$result = [ordered]@{
	schemaVersion = 'broken-engine-harness-ping/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Harness ping wait did not run.'
	port = $Port
	timeoutSeconds = $TimeoutSeconds
	attempts = 0
	elapsedMilliseconds = 0
	tick = $null
	lastObservation = 'No ping attempted.'
}

function Limit-Text([string] $Value, [int] $Maximum) {
	$normalized = ([string]$Value).Trim()
	if ($normalized.Length -le $Maximum) { return $normalized }
	return $normalized.Substring(0, $Maximum - 3) + '...'
}

function Complete-HarnessPing([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = Limit-Text $Message $MaximumMessageLength
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 32 -Compress))
	exit $ExitCode
}

function Invoke-NativeCapture([string] $FilePath, [string[]] $Arguments) {
	$raw = @(& $FilePath @Arguments 2>&1)
	$exitCode = $LASTEXITCODE
	$standard = @($raw | Where-Object { $_ -isnot [System.Management.Automation.ErrorRecord] })
	$failed = @($raw | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] })
	return [pscustomobject]@{
		ExitCode = $exitCode
		Stdout = [string]::Join([Environment]::NewLine, @($standard | ForEach-Object { [string]$_ }))
		Stderr = [string]::Join([Environment]::NewLine, @($failed | ForEach-Object { [string]$_ }))
	}
}

function Get-ResponseField($Record, [string] $Name) {
	if ($null -eq $Record -or -not ($Record.PSObject.Properties.Name -ccontains $Name)) { return $null }
	return $Record.$Name
}

try {
	if ([string]::IsNullOrWhiteSpace($Owner)) { throw 'Owner must not be empty.' }
	if (-not (Test-Path -LiteralPath $AgentHarness -PathType Leaf)) {
		Complete-HarnessPing 1 'error' 'ping.harness-missing' "AgentHarness executable is missing: '$AgentHarness'."
	}
	$harness = (Resolve-Path -LiteralPath $AgentHarness).Path

	$stopwatch = [Diagnostics.Stopwatch]::StartNew()
	$deadlineMilliseconds = $TimeoutSeconds * 1000
	while ($stopwatch.ElapsedMilliseconds -lt $deadlineMilliseconds) {
		$attemptMilliseconds = [Math]::Max(1, [Math]::Min($TimeoutMilliseconds,
			$deadlineMilliseconds - [int]$stopwatch.ElapsedMilliseconds))
		$result.attempts = [int]$result.attempts + 1
		$ping = Invoke-NativeCapture $harness @(
			'--owner', $Owner, '--port', "$Port", '--timeout-ms', "$attemptMilliseconds", '{"cmd":"ping"}')
		if ($ping.ExitCode -eq 0) {
			$response = $null
			try { $response = $ping.Stdout | ConvertFrom-Json -Depth 32 } catch { $response = $null }
			$pingResult = Get-ResponseField $response 'result'
			if ($null -ne $pingResult) {
				$result.tick = Get-ResponseField $pingResult 'tick'
				$result.elapsedMilliseconds = [long]$stopwatch.ElapsedMilliseconds
				$result.lastObservation = "Port $Port answered ping on attempt $($result.attempts)."
				Complete-HarnessPing 0 'pass' 'ok' "Port $Port answered ping with tick $($result.tick)."
			}
			$result.lastObservation = Limit-Text "Attempt $($result.attempts) returned exit 0 without a readable ping result." $MaximumMessageLength
		}
		else {
			$result.lastObservation = Limit-Text "Attempt $($result.attempts) failed with exit $($ping.ExitCode): $(($ping.Stdout, $ping.Stderr) -join ' ')" $MaximumMessageLength
		}
		# Sleeping a full interval past the deadline would report a wait longer than the promised one.
		$remainingMilliseconds = $deadlineMilliseconds - [int]$stopwatch.ElapsedMilliseconds
		if ($remainingMilliseconds -le 0) { break }
		Start-Sleep -Milliseconds ([Math]::Min($PollMilliseconds, $remainingMilliseconds))
	}

	$result.elapsedMilliseconds = [long]$stopwatch.ElapsedMilliseconds
	Complete-HarnessPing 2 'blocked' 'ping.timeout' "Port $Port did not answer ping within $TimeoutSeconds seconds over $($result.attempts) attempts. $($result.lastObservation)"
}
catch {
	Complete-HarnessPing 1 'error' 'internal.error' $_.Exception.Message
}
