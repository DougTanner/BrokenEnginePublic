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
$commonModule = Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1'
Import-Module $commonModule -Force

function Assert-True([bool] $Condition, [string] $Name) {
	if (-not $Condition) { $script:Failures.Add($Name); Write-Host "FAIL $Name" } else { Write-Host "pass $Name" }
}

function Invoke-Lock([string[]] $Arguments, [string] $WorkingDirectory) {
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
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$result = [ordered]@{
		ProcessId = $processId
		HasExited = $process.HasExited
		ExitCode = $process.ExitCode
		Stdout = $stdoutTask.GetAwaiter().GetResult()
		Stderr = $stderrTask.GetAwaiter().GetResult()
		Json = $null
	}
	$process.Dispose()
	if (-not [string]::IsNullOrWhiteSpace($result.Stdout)) {
		$convertArguments = if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) { @{ DateKind = 'String' } } else { @{} }
		try { $result.Json = $result.Stdout.Trim() | ConvertFrom-Json -Depth 32 -ErrorAction Stop @convertArguments } catch { }
	}
	return [pscustomobject] $result
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

	$absent = Invoke-Lock @('lock', 'status', '--repo', $commonDirectory) $fixtureRoot
	Assert-Run $absent 'status absent' 2
	Assert-True ($null -ne $absent.Json) 'status absent emitted JSON'
	if ($null -ne $absent.Json) {
		Assert-True ($absent.Json.held -eq $false) 'status absent held=false'
		Assert-True ('claimantPid' -notin @($absent.Json.PSObject.Properties.Name)) 'status absent omits claimantPid'
	}

	Assert-True ($script:ProcessIds.Count -eq @($script:ProcessIds | Sort-Object -Unique).Count) 'each command used a distinct process'
	$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
	$commandsSource = Join-Path $repositoryRoot 'Tools\WorktreeCli\LandingLockCommands.cpp'
	if (Test-Path -LiteralPath $commandsSource -PathType Leaf) {
		$directRawPrint = Select-String -LiteralPath $commandsSource -Pattern 'PrintMetadata\s*\(\s*metadata\s*\)'
		Assert-True ($null -eq $directRawPrint) 'LandingLockCommands has no direct PrintMetadata(metadata)'
	}
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
