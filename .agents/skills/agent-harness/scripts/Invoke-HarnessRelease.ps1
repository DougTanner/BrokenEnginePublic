# Run the whole harness lifecycle release: quit both ports, wait for the supplied exact PIDs,
# stop only a supplied exact PID that survived, and release the lock only when every supplied PID
# is confirmed absent. A surviving PID leaves the claim held. Safe to call after a crashed run.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $AgentHarness,
	[Parameter(Mandatory)][string] $Owner,
	[ValidateRange(0, 2147483647)][int] $ServerPid = 0,
	[ValidateRange(0, 2147483647)][int] $ClientPid = 0,
	[ValidateRange(1, 65535)][int] $ServerPort = 27100,
	[ValidateRange(1, 65535)][int] $ClientPort = 27101,
	[string] $Key = 'default',
	[ValidateRange(1, 600)][int] $TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$MaximumMessageLength = 256
$QuitTimeoutMilliseconds = 3000
$PollMilliseconds = 250
$StopConfirmMilliseconds = 2000

$result = [ordered]@{
	schemaVersion = 'broken-engine-harness-release/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Harness release did not run.'
	owner = $Owner
	key = $Key
	agentHarness = $AgentHarness
	timeoutSeconds = $TimeoutSeconds
	quits = @()
	processes = @()
	survivingPids = @()
	lockRelease = [ordered]@{ attempted = $false; exitCode = $null; detail = $null }
}

function Limit-Text([string] $Value, [int] $Maximum) {
	$normalized = ([string]$Value).Trim()
	if ($normalized.Length -le $Maximum) { return $normalized }
	return $normalized.Substring(0, $Maximum - 3) + '...'
}

function Complete-HarnessRelease([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
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

function Test-ProcessAlive([int] $ProcessId) {
	return $null -ne (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)
}

try {
	if ([string]::IsNullOrWhiteSpace($Owner)) { throw 'Owner must not be empty.' }
	if ([string]::IsNullOrWhiteSpace($Key)) { throw 'Key must not be empty.' }
	if (-not (Test-Path -LiteralPath $AgentHarness -PathType Leaf)) {
		Complete-HarnessRelease 1 'error' 'release.harness-missing' "AgentHarness executable is missing: '$AgentHarness'."
	}
	$harness = (Resolve-Path -LiteralPath $AgentHarness).Path
	$result.agentHarness = $harness

	# A dead port would otherwise consume the full default connect budget, so each quit is short.
	$quits = @()
	foreach ($target in @(
		[pscustomobject]@{ Role = 'server'; Port = $ServerPort },
		[pscustomobject]@{ Role = 'client'; Port = $ClientPort })) {
		$quit = Invoke-NativeCapture $harness @(
			'--owner', $Owner, '--port', "$($target.Port)", '--timeout-ms', "$QuitTimeoutMilliseconds", '{"cmd":"quit"}')
		$quits += [ordered]@{
			role = $target.Role
			port = $target.Port
			exitCode = $quit.ExitCode
			accepted = ($quit.ExitCode -eq 0)
			detail = Limit-Text (($quit.Stdout, $quit.Stderr) -join ' ') $MaximumMessageLength
		}
	}
	$result.quits = $quits

	$tracked = @()
	foreach ($target in @(
		[pscustomobject]@{ Role = 'server'; ProcessId = $ServerPid },
		[pscustomobject]@{ Role = 'client'; ProcessId = $ClientPid })) {
		if ($target.ProcessId -le 0) { continue }
		$tracked += [pscustomobject]@{
			Role = $target.Role
			ProcessId = $target.ProcessId
			State = if (Test-ProcessAlive $target.ProcessId) { 'alive' } else { 'already-absent' }
			StopAttempted = $false
		}
	}

	$stopwatch = [Diagnostics.Stopwatch]::StartNew()
	$deadlineMilliseconds = $TimeoutSeconds * 1000
	while (@($tracked | Where-Object { $_.State -ceq 'alive' }).Count -gt 0 -and
		$stopwatch.ElapsedMilliseconds -lt $deadlineMilliseconds) {
		# Sleeping a full interval past the deadline would exceed the bounded wait the caller asked for.
		$remainingMilliseconds = $deadlineMilliseconds - [int]$stopwatch.ElapsedMilliseconds
		Start-Sleep -Milliseconds ([Math]::Min($PollMilliseconds, $remainingMilliseconds))
		foreach ($process in @($tracked | Where-Object { $_.State -ceq 'alive' })) {
			if (-not (Test-ProcessAlive $process.ProcessId)) { $process.State = 'exited-after-quit' }
		}
	}

	foreach ($process in @($tracked | Where-Object { $_.State -ceq 'alive' })) {
		# Clean quit could not connect or complete for this exact retained PID; never a name search.
		$process.StopAttempted = $true
		Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
		$stopWatchdog = [Diagnostics.Stopwatch]::StartNew()
		while ($stopWatchdog.ElapsedMilliseconds -lt $StopConfirmMilliseconds) {
			if (-not (Test-ProcessAlive $process.ProcessId)) {
				$process.State = 'stopped'
				break
			}
			Start-Sleep -Milliseconds $PollMilliseconds
		}
	}

	$result.processes = @($tracked | ForEach-Object {
		[ordered]@{ role = $_.Role; pid = $_.ProcessId; state = $_.State; stopAttempted = $_.StopAttempted }
	})
	$surviving = @($tracked | Where-Object { $_.State -ceq 'alive' } | ForEach-Object { $_.ProcessId })
	$result.survivingPids = $surviving
	if ($surviving.Count -gt 0) {
		# Releasing ownership while a harness process is still running is never the automatic outcome:
		# this returns before the lock release call below, leaving the claim held.
		Complete-HarnessRelease 2 'blocked' 'release.process-survived' "Harness process(es) $($surviving -join ', ') survived quit and stop; the claim stays held."
	}

	$release = Invoke-NativeCapture $harness @('lock', 'release', '--key', $Key, '--owner', $Owner)
	$result.lockRelease.attempted = $true
	$result.lockRelease.exitCode = $release.ExitCode
	$result.lockRelease.detail = Limit-Text (($release.Stdout, $release.Stderr) -join ' ') $MaximumMessageLength
	if ($release.ExitCode -eq 0) {
		Complete-HarnessRelease 0 'pass' 'ok' "Harness lock '$Key' released by owner $Owner."
	}
	if ($release.ExitCode -eq 2) {
		if ($release.Stdout -cmatch '"held"\s*:\s*false') {
			Complete-HarnessRelease 1 'error' 'release.lock-absent' "lock release found no harness lock '$Key' to release; coordination state was removed outside this script."
		}
		Complete-HarnessRelease 1 'error' 'release.lock-owner-mismatch' "lock release rejected owner $Owner for harness lock '$Key'; the lock is held by another owner."
	}
	Complete-HarnessRelease 1 'error' 'release.lock-failed' "lock release failed with exit $($release.ExitCode): $($release.Stderr)"
}
catch {
	Complete-HarnessRelease 1 'error' 'internal.error' $_.Exception.Message
}
