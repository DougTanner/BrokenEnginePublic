# Deterministic landing-lock public-status fixtures against an isolated scratch
# repository. Every command launches a distinct candidate WorktreeCli process;
# the fixture proves the one-shot claim process exits while the lease remains
# live, public responses omit claimantPid, and the private lock record retains
# its validated claimantPid. Never point --repo at a real checkout.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Failures = [Collections.Generic.List[string]]::new()
$script:ProcessIds = [Collections.Generic.List[int]]::new()
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$WorktreeCliExecutable = (Get-Item -LiteralPath $WorktreeCliExecutable -ErrorAction Stop).FullName
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
$commonModule = Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1'
Import-Module $commonModule -Force

function Assert-True([bool] $Condition, [string] $Name) {
	if (-not $Condition) { $script:Failures.Add($Name); Write-Host "FAIL $Name" } else { Write-Host "pass $Name" }
}

# Start-Lock returns before the process exits so a blocking claim can be observed while it waits;
# the stream reads must already be running by then or a chatty child could fill a pipe and hang.
function Start-Lock([string[]] $Arguments, [string] $WorkingDirectory) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $WorktreeCliExecutable
	$start.WorkingDirectory = $WorkingDirectory
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = $utf8
	$start.StandardErrorEncoding = $utf8
	foreach ($argument in $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start '$WorktreeCliExecutable'." }
	$processId = $process.Id
	$script:ProcessIds.Add($processId)
	return [pscustomobject] [ordered]@{
		Process = $process
		ProcessId = $processId
		StdoutTask = $process.StandardOutput.ReadToEndAsync()
		StderrTask = $process.StandardError.ReadToEndAsync()
	}
}

function Wait-Lock($Started) {
	$process = $Started.Process
	$process.WaitForExit()
	$result = [ordered]@{
		ProcessId = $Started.ProcessId
		HasExited = $process.HasExited
		ExitCode = $process.ExitCode
		Stdout = $Started.StdoutTask.GetAwaiter().GetResult()
		Stderr = $Started.StderrTask.GetAwaiter().GetResult()
		Json = $null
	}
	$process.Dispose()
	if (-not [string]::IsNullOrWhiteSpace($result.Stdout)) {
		$convertArguments = if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) { @{ DateKind = 'String' } } else { @{} }
		try { $result.Json = $result.Stdout.Trim() | ConvertFrom-Json -Depth 32 -ErrorAction Stop @convertArguments } catch { }
	}
	return [pscustomobject] $result
}

function Invoke-Lock([string[]] $Arguments, [string] $WorkingDirectory) {
	return Wait-Lock (Start-Lock $Arguments $WorkingDirectory)
}

function Assert-Run($Run, [string] $Case, [int] $ExpectedExit) {
	Assert-True $Run.HasExited "$Case process exited"
	Assert-True ($Run.ExitCode -eq $ExpectedExit) "$Case exit=$ExpectedExit (was $($Run.ExitCode))"
	Assert-True ([string]::IsNullOrWhiteSpace($Run.Stderr)) "$Case stderr empty"
}

function Assert-PublicLease($Run, [string] $Case, [string] $ExpectedOwner, [string] $ExpectedState = 'live') {
	Assert-True ($null -ne $Run.Json) "$Case emitted JSON"
	if ($null -eq $Run.Json) { return }
	Assert-True ('claimantPid' -notin @($Run.Json.PSObject.Properties.Name)) "$Case omits claimantPid"
	Assert-True ($Run.Json.held -eq $true) "$Case held=true"
	Assert-True ($Run.Json.leaseState -ceq $ExpectedState) "$Case leaseState=$ExpectedState (was $($Run.Json.leaseState))"
	Assert-True ($Run.Json.owner -ceq $ExpectedOwner) "$Case owner preserved"
	$heartbeat = [DateTime]::MinValue
	$expires = [DateTime]::MinValue
	$style = [Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal
	$heartbeatValid = $Run.Json.heartbeatAt -is [string] -and [DateTime]::TryParseExact($Run.Json.heartbeatAt, "yyyy-MM-dd'T'HH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture, $style, [ref]$heartbeat)
	$expiresValid = $Run.Json.expiresAt -is [string] -and [DateTime]::TryParseExact($Run.Json.expiresAt, "yyyy-MM-dd'T'HH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture, $style, [ref]$expires)
	Assert-True $heartbeatValid "$Case heartbeatAt present and parseable"
	Assert-True $expiresValid "$Case expiresAt present and parseable"
	Assert-True ($heartbeatValid -and $expiresValid -and $expires -gt $heartbeat) "$Case expiresAt follows heartbeatAt"
}

function Get-LandingLockPath([string] $LogicalKey) {
	$sha256 = [Security.Cryptography.SHA256]::Create()
	try { $digest = $sha256.ComputeHash([Text.Encoding]::UTF8.GetBytes($LogicalKey)) } finally { $sha256.Dispose() }
	$hash = [BitConverter]::ToString($digest).Replace('-', '').ToLowerInvariant()
	return Join-Path $env:LOCALAPPDATA "BrokenEngineLocks\landing\$hash.lock"
}

$fixtureId = [guid]::NewGuid().ToString('N')
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) "BrokenEngineLandingLockFixtures\$fixtureId"
$lockPath = $null
$guardPath = $null
$activeOwner = $null

try {
	New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
	$gitOutput = @(& git -C $fixtureRoot init -b main 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git init failed: $($gitOutput -join '; ')" }
	$commonDirectory = (@(& git -C $fixtureRoot rev-parse --path-format=absolute --git-common-dir 2>&1))[0].Trim()
	if ($LASTEXITCODE -ne 0) { throw 'git common-directory discovery failed.' }
	$logicalKey = (Get-FinalizeExistingWindowsIdentity $commonDirectory 'Fixture Git common directory').ToLowerInvariant()
	$lockPath = Get-LandingLockPath $logicalKey
	$guardPath = "$lockPath.guard"
	Assert-True (-not (Test-Path -LiteralPath $lockPath)) 'isolated landing-lock path starts absent'

	$ownerA = "fixture-$fixtureId-owner-a"
	$ownerB = "fixture-$fixtureId-owner-b"
	$sessionA = "fixture-$fixtureId-session-a"
	$sessionB = "fixture-$fixtureId-session-b"
	$baseA = @('--repo', $commonDirectory, '--owner', $ownerA, '--session', $sessionA, '--worktree', $fixtureRoot)
	$baseB = @('--repo', $commonDirectory, '--owner', $ownerB, '--session', $sessionB, '--worktree', $fixtureRoot)

	$claim = Invoke-Lock (@('lock', 'claim') + $baseA + @('--lease-seconds', '60')) $fixtureRoot
	$activeOwner = $ownerA
	Assert-Run $claim 'claim success' 0
	Assert-PublicLease $claim 'claim success' $ownerA

	$status = Invoke-Lock @('lock', 'status', '--repo', $commonDirectory) $fixtureRoot
	Assert-Run $status 'status live' 0
	Assert-PublicLease $status 'status live' $ownerA
	Assert-True $claim.HasExited 'claim process exited before live status observation'

	$refresh = Invoke-Lock @('lock', 'refresh', '--repo', $commonDirectory, '--owner', $ownerA) $fixtureRoot
	Assert-Run $refresh 'refresh success' 0
	Assert-PublicLease $refresh 'refresh success' $ownerA

	$competingClaim = Invoke-Lock (@('lock', 'claim') + $baseB + @('--lease-seconds', '60')) $fixtureRoot
	Assert-Run $competingClaim 'competing claim' 2
	Assert-PublicLease $competingClaim 'competing claim' $ownerA

	$wrongRefresh = Invoke-Lock @('lock', 'refresh', '--repo', $commonDirectory, '--owner', $ownerB) $fixtureRoot
	Assert-Run $wrongRefresh 'wrong-owner refresh' 2
	Assert-PublicLease $wrongRefresh 'wrong-owner refresh' $ownerA

	$wrongRelease = Invoke-Lock @('lock', 'release', '--repo', $commonDirectory, '--owner', $ownerB) $fixtureRoot
	Assert-Run $wrongRelease 'wrong-owner release' 2
	Assert-PublicLease $wrongRelease 'wrong-owner release' $ownerA

	$liveRecover = Invoke-Lock (@('lock', 'recover') + $baseB + @('--expect', $ownerA, '--lease-seconds', '60')) $fixtureRoot
	Assert-Run $liveRecover 'live recover conflict' 2
	Assert-PublicLease $liveRecover 'live recover conflict' $ownerA

	$steal = Invoke-Lock (@('lock', 'steal') + $baseB + @('--expect', $ownerA)) $fixtureRoot
	Assert-Run $steal 'steal conflict' 2
	Assert-PublicLease $steal 'steal conflict' $ownerA

	$internalMetadata = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json -Depth 32
	$internalNames = @($internalMetadata.PSObject.Properties.Name)
	Assert-True ('claimantPid' -in $internalNames) 'private lock record retains claimantPid'
	Assert-True (($internalMetadata.claimantPid -is [int] -or $internalMetadata.claimantPid -is [long]) -and $internalMetadata.claimantPid -ge 1 -and $internalMetadata.claimantPid -le [uint32]::MaxValue) 'private claimantPid is valid integer'
	Assert-True ($internalMetadata.claimantPid -eq $claim.ProcessId) 'private claimantPid identifies one-shot claim process'

	$expirySetupRelease = Invoke-Lock @('lock', 'release', '--repo', $commonDirectory, '--owner', $ownerA) $fixtureRoot
	$activeOwner = $null
	Assert-Run $expirySetupRelease 'expiry setup release' 0
	Assert-True (-not (Test-Path -LiteralPath $lockPath)) 'expiry setup lock path is absent before seeding'

	$heartbeat = [DateTime]::UtcNow.AddMinutes(-2)
	$internalMetadata.claimedAt = $heartbeat.ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
	$internalMetadata.heartbeatAt = $internalMetadata.claimedAt
	$internalMetadata.expiresAt = $heartbeat.AddSeconds(60).ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
	[IO.File]::WriteAllText($lockPath, (($internalMetadata | ConvertTo-Json -Depth 32) + "`n"), [Text.UTF8Encoding]::new($false))

	$expiredStatus = Invoke-Lock @('lock', 'status', '--repo', $commonDirectory) $fixtureRoot
	Assert-Run $expiredStatus 'status expired' 0
	Assert-PublicLease $expiredStatus 'status expired' $ownerA 'expired'

	$recover = Invoke-Lock (@('lock', 'recover') + $baseB + @('--expect', $ownerA, '--lease-seconds', '60')) $fixtureRoot
	$activeOwner = $ownerB
	Assert-Run $recover 'recover success' 0
	Assert-PublicLease $recover 'recover success' $ownerB

	$release = Invoke-Lock @('lock', 'release', '--repo', $commonDirectory, '--owner', $ownerB) $fixtureRoot
	$activeOwner = $null
	Assert-Run $release 'correct-owner release' 0
	Assert-True ([string]::IsNullOrWhiteSpace($release.Stdout)) 'correct-owner release emits no metadata'

	$refreshAbsent = Invoke-Lock @('lock', 'refresh', '--repo', $commonDirectory, '--owner', $ownerB) $fixtureRoot
	Assert-Run $refreshAbsent 'refresh absent' 2
	Assert-True ($refreshAbsent.Stdout -ceq ('{"held":false}' + [Environment]::NewLine)) 'refresh absent emits exact held=false JSON'
	Assert-True ($null -ne $refreshAbsent.Json) 'refresh absent emitted JSON'
	if ($null -ne $refreshAbsent.Json) {
		Assert-True ($refreshAbsent.Json.held -eq $false) 'refresh absent held=false'
		Assert-True ('claimantPid' -notin @($refreshAbsent.Json.PSObject.Properties.Name)) 'refresh absent omits claimantPid'
	}

	$recoverAbsent = Invoke-Lock (@('lock', 'recover') + $baseB + @('--expect', $ownerA, '--lease-seconds', '60')) $fixtureRoot
	Assert-Run $recoverAbsent 'recover absent' 2
	Assert-True ($recoverAbsent.Stdout -ceq ('{"held":false}' + [Environment]::NewLine)) 'recover absent emits exact held=false JSON'
	Assert-True ($null -ne $recoverAbsent.Json) 'recover absent emitted JSON'
	if ($null -ne $recoverAbsent.Json) {
		Assert-True ($recoverAbsent.Json.held -eq $false) 'recover absent held=false'
		Assert-True ('claimantPid' -notin @($recoverAbsent.Json.PSObject.Properties.Name)) 'recover absent omits claimantPid'
	}

	$absent = Invoke-Lock @('lock', 'status', '--repo', $commonDirectory) $fixtureRoot
	Assert-Run $absent 'status absent' 2
	Assert-True ($null -ne $absent.Json) 'status absent emitted JSON'
	if ($null -ne $absent.Json) {
		Assert-True ($absent.Json.held -eq $false) 'status absent held=false'
		Assert-True ('claimantPid' -notin @($absent.Json.PSObject.Properties.Name)) 'status absent omits claimantPid'
	}

	# Bounded blocking claims. WorktreeCli owns the wait and the guarded expired-lease recovery, so
	# every case below makes one claim invocation and never a separate recover invocation.
	$blockingSeed = Invoke-Lock (@('lock', 'claim') + $baseA + @('--lease-seconds', '3600')) $fixtureRoot
	$activeOwner = $ownerA
	Assert-Run $blockingSeed 'blocking grant seed claim' 0
	$blockingStarted = Start-Lock (@('lock', 'claim') + $baseB + @('--lease-seconds', '600', '--wait-seconds', '15', '--poll-milliseconds', '50')) $fixtureRoot
	# Only Wait-Lock reaps the waiter, so any throw before it must kill and reap it here or the
	# still-running claim would outlive the fixture and block scratch-directory cleanup.
	try {
		Start-Sleep -Seconds 2
		Assert-True (-not $blockingStarted.Process.HasExited) 'blocking claim still waiting on the live foreign lease'
		$blockingSeedRelease = Invoke-Lock @('lock', 'release', '--repo', $commonDirectory, '--owner', $ownerA) $fixtureRoot
		Assert-Run $blockingSeedRelease 'blocking grant seed release' 0
		$blockingGrant = Wait-Lock $blockingStarted
	}
	catch {
		try { $blockingStarted.Process.Kill() } catch { }
		try { $blockingStarted.Process.WaitForExit(); $blockingStarted.Process.Dispose() } catch { }
		throw
	}
	$activeOwner = $ownerB
	Assert-Run $blockingGrant 'blocking grant' 0
	Assert-PublicLease $blockingGrant 'blocking grant' $ownerB
	$blockingGrantRelease = Invoke-Lock @('lock', 'release', '--repo', $commonDirectory, '--owner', $ownerB) $fixtureRoot
	$activeOwner = $null
	Assert-Run $blockingGrantRelease 'blocking grant release' 0

	function New-ExpiredLease([string] $Case, [string[]] $BaseArguments, [string] $Owner) {
		$seedClaim = Invoke-Lock (@('lock', 'claim') + $BaseArguments + @('--lease-seconds', '60')) $fixtureRoot
		Assert-Run $seedClaim "$Case seed claim" 0
		$seedMetadata = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json -Depth 32
		$seedRelease = Invoke-Lock @('lock', 'release', '--repo', $commonDirectory, '--owner', $Owner) $fixtureRoot
		Assert-Run $seedRelease "$Case seed release" 0
		$seedHeartbeat = [DateTime]::UtcNow.AddMinutes(-2)
		$seedMetadata.claimedAt = $seedHeartbeat.ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
		$seedMetadata.heartbeatAt = $seedMetadata.claimedAt
		$seedMetadata.expiresAt = $seedHeartbeat.AddSeconds(60).ToString("yyyy-MM-dd'T'HH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
		[IO.File]::WriteAllText($lockPath, (($seedMetadata | ConvertTo-Json -Depth 32) + "`n"), [Text.UTF8Encoding]::new($false))
	}

	New-ExpiredLease 'internal recovery' $baseA $ownerA
	$internalRecovery = Invoke-Lock (@('lock', 'claim') + $baseB + @('--lease-seconds', '600', '--wait-seconds', '5', '--poll-milliseconds', '50')) $fixtureRoot
	$activeOwner = $ownerB
	Assert-Run $internalRecovery 'internal recovery claim' 0
	Assert-PublicLease $internalRecovery 'internal recovery claim' $ownerB
	$internalRecoveryRelease = Invoke-Lock @('lock', 'release', '--repo', $commonDirectory, '--owner', $ownerB) $fixtureRoot
	$activeOwner = $null
	Assert-Run $internalRecoveryRelease 'internal recovery release' 0

	# The internal recovery keeps the registered-worktree gate: a Git-operation marker in the only
	# registered worktree must keep an expired lease's owner in place until the marker is gone.
	$markerPath = (@(& git -C $fixtureRoot rev-parse --path-format=absolute --git-path MERGE_HEAD 2>&1))[0].Trim()
	if ($LASTEXITCODE -ne 0) { throw 'git marker-path discovery failed.' }
	New-ExpiredLease 'recovery gate' $baseA $ownerA
	[IO.File]::WriteAllText($markerPath, ('0' * 40) + "`n", [Text.UTF8Encoding]::new($false))
	try {
		$gatedClaim = Invoke-Lock (@('lock', 'claim') + $baseB + @('--lease-seconds', '600', '--wait-seconds', '1', '--poll-milliseconds', '50')) $fixtureRoot
		Assert-Run $gatedClaim 'recovery gate blocked claim' 2
		$gatedStatus = Invoke-Lock @('lock', 'status', '--repo', $commonDirectory) $fixtureRoot
		Assert-Run $gatedStatus 'recovery gate status' 0
		Assert-PublicLease $gatedStatus 'recovery gate status' $ownerA 'expired'
	}
	finally { Remove-Item -LiteralPath $markerPath -Force }
	$gateClearedClaim = Invoke-Lock (@('lock', 'claim') + $baseB + @('--lease-seconds', '600', '--wait-seconds', '5', '--poll-milliseconds', '50')) $fixtureRoot
	$activeOwner = $ownerB
	Assert-Run $gateClearedClaim 'recovery gate cleared claim' 0
	Assert-PublicLease $gateClearedClaim 'recovery gate cleared claim' $ownerB

	$selfOwnedTimer = [Diagnostics.Stopwatch]::StartNew()
	$selfOwnedClaim = Invoke-Lock (@('lock', 'claim') + $baseB + @('--lease-seconds', '600', '--wait-seconds', '15', '--poll-milliseconds', '50')) $fixtureRoot
	$selfOwnedTimer.Stop()
	Assert-Run $selfOwnedClaim 'self-owned claim' 2
	Assert-PublicLease $selfOwnedClaim 'self-owned claim' $ownerB
	Assert-True ($selfOwnedTimer.Elapsed.TotalSeconds -lt 3) "self-owned claim refuses without waiting (was $([Math]::Round($selfOwnedTimer.Elapsed.TotalSeconds, 2))s)"

	$timeoutTimer = [Diagnostics.Stopwatch]::StartNew()
	$timeoutClaim = Invoke-Lock (@('lock', 'claim') + $baseA + @('--lease-seconds', '600', '--wait-seconds', '1', '--poll-milliseconds', '50')) $fixtureRoot
	$timeoutTimer.Stop()
	Assert-Run $timeoutClaim 'bounded timeout claim' 2
	Assert-PublicLease $timeoutClaim 'bounded timeout claim' $ownerB
	Assert-True ($timeoutTimer.Elapsed.TotalSeconds -ge 0.9 -and $timeoutTimer.Elapsed.TotalSeconds -lt 10) "bounded timeout claim returns at its deadline (was $([Math]::Round($timeoutTimer.Elapsed.TotalSeconds, 2))s)"

	$zeroWaitClaim = Invoke-Lock (@('lock', 'claim') + $baseA + @('--lease-seconds', '600', '--wait-seconds', '0', '--poll-milliseconds', '50')) $fixtureRoot
	$oneShotClaim = Invoke-Lock (@('lock', 'claim') + $baseA + @('--lease-seconds', '600')) $fixtureRoot
	Assert-Run $zeroWaitClaim 'zero-wait claim' 2
	Assert-Run $oneShotClaim 'one-shot claim' 2
	Assert-PublicLease $oneShotClaim 'one-shot claim' $ownerB
	Assert-True ($zeroWaitClaim.ExitCode -eq $oneShotClaim.ExitCode -and $zeroWaitClaim.Stdout -ceq $oneShotClaim.Stdout) 'zero wait matches the one-shot claim exactly'
	$zeroWaitRelease = Invoke-Lock @('lock', 'release', '--repo', $commonDirectory, '--owner', $ownerB) $fixtureRoot
	$activeOwner = $null
	Assert-Run $zeroWaitRelease 'bounded wait cleanup release' 0

	# Unverifiable metadata is never waited on and never repaired; only user authority resolves it.
	[IO.File]::WriteAllText($lockPath, "not a landing lease`n", [Text.UTF8Encoding]::new($false))
	try {
		$unverifiableTimer = [Diagnostics.Stopwatch]::StartNew()
		$unverifiableClaim = Invoke-Lock (@('lock', 'claim') + $baseA + @('--lease-seconds', '600', '--wait-seconds', '15', '--poll-milliseconds', '50')) $fixtureRoot
		$unverifiableTimer.Stop()
		Assert-Run $unverifiableClaim 'unverifiable claim' 2
		Assert-True ($unverifiableClaim.Stdout.Contains('"leaseState":"unverifiable"')) 'unverifiable claim reports the unverifiable lease state'
		Assert-True ($null -ne $unverifiableClaim.Json -and $unverifiableClaim.Json.held -eq $true) 'unverifiable claim held=true'
		Assert-True ($unverifiableTimer.Elapsed.TotalSeconds -lt 3) "unverifiable claim short-circuits the wait (was $([Math]::Round($unverifiableTimer.Elapsed.TotalSeconds, 2))s)"
	}
	finally { Remove-Item -LiteralPath $lockPath -Force }

	Assert-True ($script:ProcessIds.Count -eq @($script:ProcessIds | Sort-Object -Unique).Count) 'each command used a distinct process'
	$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
	$commandsSource = Join-Path $repositoryRoot 'Tools\WorktreeCli\LandingLockCommands.cpp'
	if (-not (Test-Path -LiteralPath $commandsSource -PathType Leaf)) { throw "LandingLockCommands source is unavailable: $commandsSource" }
	$commandsText = Get-Content -LiteralPath $commandsSource -Raw
	$emitterStart = $commandsText.IndexOf('int EmitLandingConflict(')
	$emitterEnd = $commandsText.IndexOf('int HandleClaim(', $emitterStart)
	$recoverStart = $commandsText.IndexOf('int HandleRecover(')
	$recoverEnd = $commandsText.IndexOf('int HandleReleaseOrSteal(', $recoverStart)
	if ($emitterStart -lt 0 -or $emitterEnd -le $emitterStart -or $recoverStart -lt 0 -or $recoverEnd -le $recoverStart) { throw 'LandingLockCommands source regions are unavailable for routing assertions.' }
	$emitterSource = $commandsText.Substring($emitterStart, $emitterEnd - $emitterStart)
	$recoverSource = $commandsText.Substring($recoverStart, $recoverEnd - $recoverStart)
	$directRawPrint = Select-String -LiteralPath $commandsSource -Pattern 'PrintMetadata\s*\(\s*metadata\s*\)'
	Assert-True ($null -eq $directRawPrint) 'LandingLockCommands has no direct PrintMetadata(metadata)'
	$allConflictReturns = [regex]::Matches($commandsText, 'return\s+kiExitStateConflict\s*;')
	$emitterConflictReturns = [regex]::Matches($emitterSource, 'return\s+kiExitStateConflict\s*;')
	Assert-True ($allConflictReturns.Count -eq 1 -and $emitterConflictReturns.Count -eq 1) 'LandingLockCommands centralizes conflict exits in emitter'
	$revalidatedMetadataRouting = [regex]::IsMatch($recoverSource, '(?s)if\s*\(\s*revalidatedMetadata\s*!=\s*rMetadata\s*\)\s*\{\s*return\s+EmitLandingConflict\s*\(\s*rLocator\s*,\s*revalidatedMetadata\s*,\s*LandingRecordState::kReadable\s*\)\s*;\s*\}')
	Assert-True $revalidatedMetadataRouting 'recover revalidation mismatch emits revalidated metadata'
	$failedRereadPolicy = [regex]::IsMatch($recoverSource, '(?s)if\s*\(\s*!ReadMetadata\s*\(\s*rLocator\.path\s*,\s*revalidatedMetadata\s*\)\s*\)\s*\{\s*std::error_code\s+error\s*;\s*const\s+bool\s+bRevalidatedExists\s*=\s*std::filesystem::exists\s*\(\s*rLocator\.path\s*,\s*error\s*\)\s*;\s*return\s+EmitLandingConflict\s*\(\s*rLocator\s*,\s*rMetadata\s*,\s*!error\s*&&\s*!bRevalidatedExists\s*\?\s*LandingRecordState::kAbsent\s*:\s*LandingRecordState::kUnverifiable\s*\)\s*;\s*\}')
	Assert-True $failedRereadPolicy 'recover failed re-read routes through absent or unverifiable policy'
}
catch {
	$script:Failures.Add("fixture exception: $($_.Exception.Message)")
	Write-Host "FAIL fixture exception: $($_.Exception.Message)"
}
finally {
	if ($null -ne $activeOwner -and $null -ne $lockPath -and (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
		try { Invoke-Lock @('lock', 'release', '--repo', $commonDirectory, '--owner', $activeOwner) $fixtureRoot | Out-Null } catch { }
	}
	if ($null -ne $lockPath -and (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
		try {
			$cleanupMetadata = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json -Depth 32
			if ($cleanupMetadata.logicalKey -ceq $logicalKey) { Remove-Item -LiteralPath $lockPath -Force }
			else { $script:Failures.Add("refused cleanup of unexpected lock identity: $lockPath") }
		}
		catch { $script:Failures.Add("landing-lock cleanup failed: $($_.Exception.Message)") }
	}
	try {
		if ($null -ne $guardPath -and (Test-Path -LiteralPath $guardPath)) { Remove-Item -LiteralPath $guardPath -Force }
	}
	catch { $script:Failures.Add("landing-lock guard cleanup failed: $($_.Exception.Message)") }
	try {
		if (Test-Path -LiteralPath $fixtureRoot) { Remove-Item -LiteralPath $fixtureRoot -Recurse -Force }
	}
	catch { $script:Failures.Add("fixture-root cleanup failed: $($_.Exception.Message)") }
}

Write-Host ''
if ($script:Failures.Count -gt 0) {
	Write-Host "Landing-lock status fixtures FAILED ($($script:Failures.Count) assertion(s))."
	exit 1
}
Write-Host 'Landing-lock status fixtures passed.'
exit 0
