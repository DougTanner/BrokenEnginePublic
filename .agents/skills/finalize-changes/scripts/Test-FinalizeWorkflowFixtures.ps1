# Scratch-repository coverage for finalization preflight request binding, one
# session landing, and idempotent post-advance recovery. Uses the supplied
# capability-current WorktreeCli only inside disposable Output and never touches
# a real primary checkout, queue, or canonical executable.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\AgentWorktreeSession.psm1') -Force -DisableNameChecking

$script:Failures = [Collections.Generic.List[string]]::new()
$preflightScript = Join-Path $PSScriptRoot 'Test-FinalizePreflight.ps1'
$landingScript = Join-Path $PSScriptRoot 'Invoke-FinalizeLanding.ps1'
$lockClaimScript = Join-Path $PSScriptRoot 'Invoke-FinalizeLockClaim.ps1'
$moduleSource = Join-Path $PSScriptRoot '..\..\..\scripts'
$WorktreeCliExecutable = (Get-Item -LiteralPath $WorktreeCliExecutable -Force -ErrorAction Stop).FullName

function Assert-True([bool] $Condition, [string] $Name) {
	if ($Condition) { Write-Host "pass $Name" } else { $script:Failures.Add($Name); Write-Host "FAIL $Name" }
}

function Assert-SafeScratchRoot([string] $Parent, [string] $Root, [string] $ExpectedLeaf) {
	$parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
	$rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
	if ((Split-Path -Parent $rootPath) -cne $parentPath -or (Split-Path -Leaf $rootPath) -cne $ExpectedLeaf -or $ExpectedLeaf -cnotmatch '^[0-9a-f]{32}$') {
		throw "Fixture scratch root failed containment validation: '$rootPath'."
	}
	return $rootPath
}

function Invoke-ScratchGit([string] $Root, [string[]] $Arguments) {
	$output = @(& git -C $Root -c user.name=fixture -c user.email=fixture@example.com @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
}

function Invoke-JsonScript([string] $Script, [string[]] $Arguments) {
	$stdout = @(& "$PSHOME\pwsh.exe" -NoProfile -File $Script @Arguments 2>$null)
	$exitCode = $LASTEXITCODE
	$text = ($stdout -join "`n").Trim()
	$json = $null
	try { if (-not [string]::IsNullOrWhiteSpace($text)) { $json = $text | ConvertFrom-Json -Depth 100 -ErrorAction Stop } } catch { }
	return [pscustomobject]@{ ExitCode = $exitCode; Json = $json; Text = $text }
}

function Invoke-JsonScriptWithSplat([string] $Script, [Collections.IDictionary] $Parameters, [string] $ScratchRoot) {
	$invocationRoot = Join-Path $ScratchRoot 'splat-invocation'
	New-Item -ItemType Directory -Force $invocationRoot | Out-Null
	$payloadPath = Join-Path $invocationRoot ([guid]::NewGuid().ToString('N') + '.json')
	$wrapperPath = Join-Path $invocationRoot 'Invoke-WithSplat.ps1'
	$wrapper = @'
param([string] $TargetScript, [string] $PayloadPath)
$payload = Get-Content -LiteralPath $PayloadPath -Raw | ConvertFrom-Json -Depth 32 -ErrorAction Stop
$parameters = @{}
foreach ($property in $payload.PSObject.Properties) { $parameters[$property.Name] = $property.Value }
& $TargetScript @parameters
exit $LASTEXITCODE
'@
	[IO.File]::WriteAllText($wrapperPath, $wrapper, [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText($payloadPath, (ConvertTo-Json -InputObject $Parameters -Depth 8 -Compress), [Text.UTF8Encoding]::new($false))
	$stderrPath = Join-Path $invocationRoot ([guid]::NewGuid().ToString('N') + '.stderr')
	$stdout = @(& "$PSHOME\pwsh.exe" -NoProfile -File $wrapperPath -TargetScript $Script -PayloadPath $payloadPath 2>$stderrPath)
	$exitCode = $LASTEXITCODE
	$stderr = ''
	if (Test-Path -LiteralPath $stderrPath) {
		$stderr = [IO.File]::ReadAllText($stderrPath)
		Remove-Item -LiteralPath $stderrPath -Force
	}
	$text = ($stdout -join "`n").Trim()
	$json = $null
	try { if (-not [string]::IsNullOrWhiteSpace($text)) { $json = $text | ConvertFrom-Json -Depth 100 -ErrorAction Stop } } catch { }
	return [pscustomobject]@{ ExitCode = $exitCode; Json = $json; Text = $text; Stderr = $stderr }
}

function Invoke-WorktreeCli([string[]] $Arguments, [int] $ExpectedExitCode = 0) {
	$stdout = @(& $WorktreeCliExecutable @Arguments 2>&1)
	if ($LASTEXITCODE -ne $ExpectedExitCode) {
		throw "WorktreeCli $($Arguments -join ' ') exited $LASTEXITCODE, expected $ExpectedExitCode`: $($stdout -join '; ')"
	}
	return $stdout
}

function Assert-Outcome($Run, [string] $Case, [int] $ExpectedExit, [string] $ExpectedStatus, [string] $ExpectedCode) {
	Assert-True ($null -ne $Run.Json) "$Case emitted JSON"
	if ($null -eq $Run.Json) { Write-Host "  stdout: $($Run.Text)"; Write-Host "  stderr: $($Run.Stderr)"; return }
	Assert-True ($Run.ExitCode -eq $ExpectedExit) "$Case exit=$ExpectedExit (was $($Run.ExitCode))"
	Assert-True ($Run.Json.status -ceq $ExpectedStatus) "$Case status=$ExpectedStatus (was $($Run.Json.status))"
	Assert-True ($Run.Json.code -ceq $ExpectedCode) "$Case code=$ExpectedCode (was $($Run.Json.code))"
	if ($Run.ExitCode -ne $ExpectedExit -or $Run.Json.code -cne $ExpectedCode) { Write-Host "  message: $($Run.Json.message)"; Write-Host "  stderr: $($Run.Stderr)" }
}

function Set-ExpiredLandingLease([string] $LocalAppData, [string] $Owner) {
	$lease = @(Get-ChildItem -LiteralPath $LocalAppData -Recurse -Filter '*.lock' -File -Force | Where-Object {
		try {
			$metadata = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
			return $metadata.domain -ceq 'landing' -and $metadata.owner -ceq $Owner
		}
		catch { return $false }
	})
	if ($lease.Count -ne 1) { throw "Could not locate one scratch landing lease for '$Owner'." }
	$metadata = Get-Content -LiteralPath $lease[0].FullName -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
	[IO.File]::SetAttributes($lease[0].FullName, [IO.FileAttributes]::Normal)
	$now = [DateTime]::UtcNow
	$heartbeat = $now.AddSeconds(-120).ToString('yyyy-MM-ddTHH:mm:ss.fffZ', [Globalization.CultureInfo]::InvariantCulture)
	$expires = $now.AddSeconds(-60).ToString('yyyy-MM-ddTHH:mm:ss.fffZ', [Globalization.CultureInfo]::InvariantCulture)
	$metadata.claimedAt = $heartbeat
	$metadata.heartbeatAt = $heartbeat
	$metadata.expiresAt = $expires
	[IO.File]::WriteAllText($lease[0].FullName, ($metadata | ConvertTo-Json -Depth 16 -Compress), [Text.UTF8Encoding]::new($false))
}

function Set-UnverifiableLandingLease([string] $LocalAppData, [string] $Owner) {
	$lease = @(Get-ChildItem -LiteralPath $LocalAppData -Recurse -Filter '*.lock' -File -Force | Where-Object {
		try {
			$metadata = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
			return $metadata.domain -ceq 'landing' -and $metadata.owner -ceq $Owner
		}
		catch { return $false }
	})
	if ($lease.Count -ne 1) { throw "Could not locate one scratch landing lease for '$Owner'." }
	$metadata = Get-Content -LiteralPath $lease[0].FullName -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
	[IO.File]::SetAttributes($lease[0].FullName, [IO.FileAttributes]::Normal)
	$metadata.schemaVersion = 0
	[IO.File]::WriteAllText($lease[0].FullName, ($metadata | ConvertTo-Json -Depth 16 -Compress), [Text.UTF8Encoding]::new($false))
}

$scratchParent = Join-Path ([IO.Path]::GetTempPath()) 'BrokenEngineFinalizeWorkflowFixtures'
$scratchLeaf = [guid]::NewGuid().ToString('N')
$scratchBase = Assert-SafeScratchRoot $scratchParent (Join-Path $scratchParent $scratchLeaf) $scratchLeaf
$primary = Join-Path $scratchBase 'primary'
$session = Join-Path $scratchBase 'session'
$localAppData = Join-Path $scratchBase 'local-app-data'
$previousEnvironment = @{}
$fixtureEnvironment = $null
$fixtureExitCode = 0

try {
New-Item -ItemType Directory -Force $primary | Out-Null
New-Item -ItemType Directory -Force $localAppData | Out-Null
Invoke-ScratchGit $primary @('init', '-b', 'main') | Out-Null
Invoke-ScratchGit $primary @('config', 'core.autocrlf', 'false') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $primary '.agents\scripts') | Out-Null
foreach ($module in @('AgentScriptCommon.psm1', 'WorktreeCliSessionExclusion.psm1', 'AgentWorktreeSession.psm1')) {
	Copy-Item -LiteralPath (Join-Path $moduleSource $module) -Destination (Join-Path $primary ".agents\scripts\$module") -Force
}
[IO.File]::WriteAllText((Join-Path $primary '.gitignore'), "Temp/`nTools/WorktreeCli/Platforms/VisualStudio2026/Output/`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'base.txt'), 'base', [Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Force (Join-Path $primary 'Documents\Plans') | Out-Null
$metadata = '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-01T00:00:00.000Z","dependsOn":[]} -->'
$terminalPlan = 'Documents/Plans/Terminal/Recovery.md'
$primaryTerminalPlan = Join-Path $primary $terminalPlan
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($primaryTerminalPlan)) | Out-Null
[IO.File]::WriteAllText($primaryTerminalPlan, "$metadata`n# Recovery fixture`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'Documents\Plans\AGENTS.md'), '# Directory guidance', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'fixture base') | Out-Null
$baseline = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()

$primaryOutput = Join-Path $primary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
New-Item -ItemType Directory -Force $primaryOutput | Out-Null
Copy-Item -LiteralPath $WorktreeCliExecutable -Destination (Join-Path $primaryOutput 'WorktreeCli.exe') -Force
# The receipt constructor requires branch == "<client>/<worktreeId>"; use codex.
$uuid = [guid]::NewGuid().ToString()
$owner = [guid]::NewGuid().ToString()
$sessionBranch = "codex/$uuid"
Invoke-ScratchGit $primary @('worktree', 'add', '-b', $sessionBranch, $session, $baseline) | Out-Null
New-Item -ItemType Directory -Force (Join-Path $session 'Temp') | Out-Null
$sessionOutputParent = Join-Path $session 'Tools\WorktreeCli\Platforms\VisualStudio2026'
New-Item -ItemType Directory -Force $sessionOutputParent | Out-Null
New-Item -ItemType Junction -Path (Join-Path $sessionOutputParent 'Output') -Target $primaryOutput | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'change.txt'), 'session change', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add', 'change.txt') | Out-Null
Invoke-ScratchGit $session @('commit', '-m', 'fixture change') | Out-Null
# Align primary onto the session tip so the receipt baseline is fixed for every session-landing
# scenario below; the terminal claim later selects from an equal session/primary tree.
$sessionTip = (@(Invoke-ScratchGit $session @('rev-parse', 'HEAD')))[0].Trim()
Invoke-ScratchGit $primary @('merge', '--ff-only', $sessionTip) | Out-Null
$baseline = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()

$fixtureEnvironment = [ordered]@{
	LOCALAPPDATA = $localAppData
	BROKEN_ENGINE_WORKTREE_PATH = $session
	BROKEN_ENGINE_SESSION_BRANCH = $sessionBranch
	BROKEN_ENGINE_PRIMARY_CHECKOUT = $primary
	BROKEN_ENGINE_TARGET_BRANCH = 'main'
	BROKEN_ENGINE_BASELINE = $baseline
	BROKEN_ENGINE_SESSION_OWNER = $owner
}
foreach ($entry in $fixtureEnvironment.GetEnumerator()) {
	$previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key)
	[Environment]::SetEnvironmentVariable($entry.Key, $entry.Value)
}

$commonDirectory = ((@(Invoke-ScratchGit $primary @('rev-parse', '--path-format=absolute', '--git-common-dir')))[0].Trim())
# The in-worktree session receipt is the session-landing identity authority now; write it after
# the primary/session alignment so its fixed baseline equals every session-landing preflight's.
$primaryIdentity = Get-AgentWorktreePrimaryIdentity $primary
$receipt = New-AgentWorktreeSessionReceipt -Client codex -PrimaryCheckout $primaryIdentity.Root -GitCommonDirectory $primaryIdentity.CommonDirectory `
	-Worktree $session -WorktreeId $uuid -Branch $sessionBranch -TargetBranch 'main' -Baseline $baseline -SessionOwner $owner
Write-AgentWorktreeSessionReceipt -Worktree $session -Receipt $receipt | Out-Null
# Landing-lock fixtures below retain their independent lease and recovery coverage.

# Reconciliation uses the claim sidecar; landing calls the same common helper.
$reconcileOwner = [guid]::NewGuid().ToString()
$lockClaimArguments = @('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $reconcileOwner, '-LeaseSeconds', '60')
$run = Invoke-JsonScript $lockClaimScript $lockClaimArguments
Assert-Outcome $run 'reconcile-lock-claim' 0 'pass' 'ok'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.owner -ceq $reconcileOwner) 'reconcile lock preserves supplied owner'
	Assert-True ($null -eq $run.Json.blocker) 'reconcile lock success has no blocker disposition'
}
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $reconcileOwner) | Out-Null

$foreignLeaseOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $foreignLeaseOwner, '--session', 'foreign-fixture', '--worktree', $session, '--lease-seconds', '60') | Out-Null
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', ([guid]::NewGuid().ToString()), '-LeaseSeconds', '60', '-WaitSeconds', '1', '-PollMilliseconds', '50'))
Assert-Outcome $run 'reconcile-lock-live-contention' 2 'blocked' 'landing-lock.retryable-wait'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.disposition -ceq 'retryable-wait') 'live foreign lease exposes top-level retryable disposition'
	Assert-True ($run.Json.blocker.disposition -ceq 'retryable-wait') 'live foreign lease is retryable'
	Assert-True (-not $run.Json.blocker.requiresUserAuthority) 'live foreign lease needs no authority'
}
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $foreignLeaseOwner) | Out-Null

$sameOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $sameOwner, '--session', 'wrong-session', '--worktree', $session, '--lease-seconds', '60') | Out-Null
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $sameOwner, '-LeaseSeconds', '60', '-WaitSeconds', '1', '-PollMilliseconds', '50'))
Assert-Outcome $run 'reconcile-lock-same-owner-wrong-session' 2 'blocked' 'landing-lock.retryable-wait'
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $sameOwner) | Out-Null

$sameOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $sameOwner, '--session', 'finalize-fixture', '--worktree', $primary, '--lease-seconds', '60') | Out-Null
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $sameOwner, '-LeaseSeconds', '60', '-WaitSeconds', '1', '-PollMilliseconds', '50'))
Assert-Outcome $run 'reconcile-lock-same-owner-wrong-worktree' 2 'blocked' 'landing-lock.retryable-wait'
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $sameOwner) | Out-Null

$expiredLeaseOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $expiredLeaseOwner, '--session', 'expired-fixture', '--worktree', $session, '--lease-seconds', '60') | Out-Null
Set-ExpiredLandingLease $localAppData $expiredLeaseOwner
$recoveredOwner = [guid]::NewGuid().ToString()
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $recoveredOwner, '-LeaseSeconds', '60'))
Assert-Outcome $run 'reconcile-lock-expired-recovery' 0 'pass' 'ok'
if ($null -ne $run.Json) { Assert-True ($run.Json.lock.owner -ceq $recoveredOwner) 'expired lease recovers through exact owner compare-and-swap' }
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $recoveredOwner) | Out-Null

$unverifiableLeaseOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $unverifiableLeaseOwner, '--session', 'unverifiable-fixture', '--worktree', $session, '--lease-seconds', '60') | Out-Null
Set-UnverifiableLandingLease $localAppData $unverifiableLeaseOwner
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', ([guid]::NewGuid().ToString()), '-LeaseSeconds', '60'))
Assert-Outcome $run 'reconcile-lock-unverifiable' 2 'blocked' 'landing-lock.unverifiable'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.disposition -ceq 'authority-required') 'unverifiable lease exposes top-level authority disposition'
	Assert-True ($run.Json.blocker.disposition -ceq 'authority-required') 'unverifiable lease needs authority'
	Assert-True $run.Json.blocker.requiresUserAuthority 'unverifiable lease explicitly requires authority'
}
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $unverifiableLeaseOwner) | Out-Null

# Primary-commit mode needs neither a session receipt nor session-owner environment: its claim
# classification is 'not-required' and the receipt gate never runs. Clear the six session
# environment variables first so this proves genuine environment-independence, not a value still in
# scope; restore them afterward because later scenarios rely on the fixture environment.
$primaryCommitEnvironmentNames = @('BROKEN_ENGINE_SESSION_OWNER','BROKEN_ENGINE_WORKTREE_PATH','BROKEN_ENGINE_SESSION_BRANCH','BROKEN_ENGINE_PRIMARY_CHECKOUT','BROKEN_ENGINE_TARGET_BRANCH','BROKEN_ENGINE_BASELINE')
foreach ($name in $primaryCommitEnvironmentNames) { [Environment]::SetEnvironmentVariable($name, $null) }
try {
	$primaryCommitPreflight = @('-Mode','primary-commit','-Checkpoint','initial','-CurrentWorktree',$primary,'-PrimaryWorktree',$primary,'-CurrentBranch','main','-PrimaryBranch','main','-Baseline',$baseline,'-WaitSeconds','5')
	$run = Invoke-JsonScript $preflightScript $primaryCommitPreflight
	Assert-Outcome $run 'preflight-primary-commit' 0 'pass' 'ok'
	if ($null -ne $run.Json) { Assert-True ($run.Json.claim.classification -ceq 'not-required') 'primary-commit mode requires no session claim (no session-owner environment set)' }
}
finally {
	foreach ($name in $primaryCommitEnvironmentNames) { [Environment]::SetEnvironmentVariable($name, $fixtureEnvironment[$name]) }
}

# Finalization validates the reconciled session tree before any lock or primary
# mutation. Invalid metadata and dependency cycles both fail closed. The landing takes
# its own transient claim; the fixture registers no session claim.
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\Invalid.md'), '<!-- broken-engine-plan/v1 {"createdUtc":42,"dependsOn":[]} -->', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add','Documents/Plans/Invalid.md') | Out-Null
Invoke-ScratchGit $session @('commit','-m','fixture invalid Plan metadata') | Out-Null
$invalidTip = (@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()
$directParameters = [ordered]@{ CurrentWorktree=$session; PrimaryWorktree=$primary; CurrentBranch=$sessionBranch; PrimaryBranch='main'; Baseline=$baseline; ExpectedCurrentTip=$invalidTip; ExpectedPrimaryTip=$baseline; SessionOwner=$owner; SessionLabel='finalize-fixture'; ApprovedSessionCommit=$invalidTip }
$run = Invoke-JsonScriptWithSplat $landingScript $directParameters $scratchBase
Assert-Outcome $run 'invalid-metadata-blocks-landing' 1 'error' 'plan.validation-failed'
Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'invalid metadata leaves primary unchanged'
Remove-Item -LiteralPath (Join-Path $session 'Documents\Plans\Invalid.md') -Force
$cycleA = '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-03T00:00:00.000Z","dependsOn":["Documents/Plans/CycleB.md"]} -->'
$cycleB = '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-04T00:00:00.000Z","dependsOn":["Documents/Plans/CycleA.md"]} -->'
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\CycleA.md'), "$cycleA`n# Cycle A`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\CycleB.md'), "$cycleB`n# Cycle B`n", [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add','-A') | Out-Null
Invoke-ScratchGit $session @('commit','-m','fixture Plan metadata cycle') | Out-Null
$cycleTip = (@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()
$directParameters.ExpectedCurrentTip = $cycleTip
$directParameters.ApprovedSessionCommit = $cycleTip
$run = Invoke-JsonScriptWithSplat $landingScript $directParameters $scratchBase
Assert-Outcome $run 'metadata-cycle-blocks-landing' 1 'error' 'plan.validation-failed'
Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'metadata cycle leaves primary unchanged'
# The negative scenarios never mutated primary; discard their session commits so the terminal
# claim starts from an equal session/primary tree at the fixed receipt baseline.
Invoke-ScratchGit $session @('reset','--hard',$baseline) | Out-Null
Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim())) 'terminal claim starts from aligned primary and session tips'

# Claim and prepare in the session before approval. The terminal receipt remains
# live through landing; a retry uses the same receipt after the primary advance.
$receiptPath = Join-Path $session 'Temp\terminal-claim.json'
$claim = (Invoke-WorktreeCli @('plan','claim-next','--repo',$commonDirectory,'--primary-worktree',$primary,'--worktree',$session,'--branch',$sessionBranch,'--owner',$owner,'--session',$owner,'--write-claim-receipt',$receiptPath,'--plan',$terminalPlan) | ConvertFrom-Json -Depth 100)
Assert-True $claim.claimed 'fixture claimed terminal Plan through receipt flow'
Assert-True ($claim.receipt.sha256 -cmatch '^[0-9a-f]{64}$') 'fixture claim returns receipt hash'
$prepared = (Invoke-WorktreeCli @('plan','prepare-completion','--repo',$commonDirectory,'--worktree',$session,'--claim-receipt',$claim.receipt.path,'--claim-receipt-sha256',$claim.receipt.sha256) | ConvertFrom-Json -Depth 100)
Assert-True ($prepared.prepared -and $prepared.claimState -ceq 'awaiting-landing') 'fixture prepares terminal receipt state'
$terminalPlanParent = [IO.Path]::GetDirectoryName((Join-Path $session $terminalPlan))
Remove-Item -LiteralPath $terminalPlanParent -Force
Assert-True (-not (Test-Path -LiteralPath $terminalPlanParent)) 'last-Plan parent is absent before finalization reruns terminal preparation'
$lateChildMarker = "<!-- broken-engine-plan/v1 {`"createdUtc`":`"2024-01-02T00:00:00.000Z`",`"dependsOn`": [`"$terminalPlan`"]} -->"
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\LateChild.md'), "$lateChildMarker`n# Late child fixture`n", [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add','-A') | Out-Null
Invoke-ScratchGit $session @('commit','-m','fixture terminal completion') | Out-Null
$approved = (@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()

$commonPreflight = @('-Mode','session-landing','-Checkpoint','after-reconciliation','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$approved,'-ExpectedPrimaryTip',$baseline,'-SessionOwner',$owner,'-WaitSeconds','5','-ClaimReceiptPath',$claim.receipt.path,'-ClaimReceiptSha256',$claim.receipt.sha256)
$run = Invoke-JsonScript $preflightScript $commonPreflight
Assert-Outcome $run 'preflight-terminal-receipt' 0 'pass' 'ok'

# Session-landing preflight identity authority is now the in-worktree receipt: a wrong
# SessionOwner and a missing receipt each block deterministically before any mutation.
$wrongOwnerPreflight = @('-Mode','session-landing','-Checkpoint','after-reconciliation','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$approved,'-ExpectedPrimaryTip',$baseline,'-SessionOwner',([guid]::NewGuid().ToString()),'-WaitSeconds','5','-ClaimReceiptPath',$claim.receipt.path,'-ClaimReceiptSha256',$claim.receipt.sha256)
$run = Invoke-JsonScript $preflightScript $wrongOwnerPreflight
Assert-Outcome $run 'preflight-receipt-owner-mismatch' 2 'blocked' 'receipt.owner-mismatch'
$sessionReceiptPath = Get-AgentWorktreeReceiptPath $session
$sessionReceiptStash = "$sessionReceiptPath.stash"
Move-Item -LiteralPath $sessionReceiptPath -Destination $sessionReceiptStash
try {
	$run = Invoke-JsonScript $preflightScript $commonPreflight
	Assert-Outcome $run 'preflight-receipt-unreadable' 2 'blocked' 'receipt.unreadable'
}
finally { Move-Item -LiteralPath $sessionReceiptStash -Destination $sessionReceiptPath }

# A changed receipt is rejected before landing; a valid receipt-bound terminal
# transaction advances primary and releases the scheduler claim afterward.
$badReceipt = Invoke-JsonScript $preflightScript (@($commonPreflight[0..($commonPreflight.Count - 2)]) + @(('0' * 64)))
Assert-Outcome $badReceipt 'preflight-receipt-tamper' 2 'blocked' 'plan-claim-receipt.identity-changed'
$landingParameters = [ordered]@{ CurrentWorktree=$session; PrimaryWorktree=$primary; CurrentBranch=$sessionBranch; PrimaryBranch='main'; Baseline=$baseline; ExpectedCurrentTip=$approved; ExpectedPrimaryTip=$baseline; SessionOwner=$owner; SessionLabel='finalize-fixture'; ApprovedSessionCommit=$approved; ClaimReceiptPath=$claim.receipt.path; ClaimReceiptSha256=$claim.receipt.sha256; TerminalDisposition='completed' }
$run = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
Assert-Outcome $run 'late-child-refresh-required' 2 'blocked' 'approval.refresh-required'
Invoke-ScratchGit $session @('add','Documents/Plans/LateChild.md') | Out-Null
Invoke-ScratchGit $session @('commit','-m','fixture refreshed terminal candidate') | Out-Null
$approved = (@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()
# The landing reruns terminal preparation, so a terminal target carrying third-party bytes blocks as a
# named recovery conflict needing user judgment, never as a bare prepare failure.
Invoke-ScratchGit $session @('checkout',$baseline,'--',$terminalPlan) | Out-Null
[IO.File]::WriteAllText((Join-Path $session $terminalPlan), "$metadata`n# Recovery fixture with third-party bytes`n", [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add',$terminalPlan) | Out-Null
Invoke-ScratchGit $session @('commit','-m','fixture third-party terminal target bytes') | Out-Null
$conflictTip = (@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()
$landingParameters.ExpectedCurrentTip = $conflictTip
$landingParameters.ApprovedSessionCommit = $conflictTip
$run = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
Assert-Outcome $run 'terminal-target-third-party-bytes' 2 'blocked' 'plan.recovery-conflict'
if ($null -ne $run.Json) { Assert-True ($run.Json.message -clike "*$terminalPlan*") 'recovery conflict names the conflicting Plan path' }
Invoke-ScratchGit $session @('rm','--quiet','--',$terminalPlan) | Out-Null
Invoke-ScratchGit $session @('commit','-m','fixture restores terminal target deletion') | Out-Null
if (Test-Path -LiteralPath $terminalPlanParent) { Remove-Item -LiteralPath $terminalPlanParent -Force }
Assert-True (-not (Test-Path -LiteralPath $terminalPlanParent)) 'last-Plan parent remains absent before terminal landing'
$approved = (@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()
$landingParameters.ExpectedCurrentTip = $approved
$landingParameters.ApprovedSessionCommit = $approved
$landingParameters.TerminalDisposition = 'rejected'
$primaryBeforeDispositionMismatch = (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
$run = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
Assert-Outcome $run 'opposite-disposition-landing' 2 'blocked' 'plan.disposition-mismatch'
Assert-True ($primaryBeforeDispositionMismatch -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'opposite-disposition landing leaves primary unchanged'
$landingParameters.TerminalDisposition = 'completed'
$run = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
Assert-Outcome $run 'receipt-terminal-landing' 0 'landed' 'ok'
if ($null -ne $run.Json) { Assert-True $run.Json.primaryAdvanced 'terminal landing advanced primary'; Assert-True $run.Json.planClaim.released 'terminal landing released receipt-bound claim' }
Assert-True (-not (Test-Path -LiteralPath (Join-Path $primary $terminalPlan))) 'terminal landing removed last Plan from primary'

# Re-running landing after primary has advanced must recover terminal proof,
# not recreate scheduler state or publish any auxiliary plan work.
$run = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
Assert-Outcome $run 'receipt-post-advance-recovery' 0 'landed' 'ok'
if ($null -ne $run.Json) { Assert-True $run.Json.primaryAdvanced 'post-advance receipt recovery preserves primary advance'; Assert-True $run.Json.planClaim.released 'post-advance recovery proves released receipt state' }

Write-Host ''
if ($script:Failures.Count -gt 0) {
	Write-Host "Finalize workflow fixtures FAILED ($($script:Failures.Count) assertion(s))."
	$fixtureExitCode = 1
}
else {
	Write-Host 'Finalize workflow fixtures passed.'
}
}
finally {
	if ($null -ne $fixtureEnvironment) {
		foreach ($entry in $fixtureEnvironment.GetEnumerator()) { [Environment]::SetEnvironmentVariable($entry.Key, $previousEnvironment[$entry.Key]) }
	}
	$validatedScratch = Assert-SafeScratchRoot $scratchParent $scratchBase $scratchLeaf
	if (Test-Path -LiteralPath $validatedScratch) {
		Remove-Item -LiteralPath $validatedScratch -Recurse -Force -Confirm:$false
	}
}
exit $fixtureExitCode
