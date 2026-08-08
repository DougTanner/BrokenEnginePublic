
# Deterministic fixtures for Invoke-AgentToolsPromotion.ps1 against a scratch primary
# repository: invalid candidate input, an unlanded commit, the exclusion-ledger mutex
# (malformed ledger and a live transient operation claim), cooperating-owner promotion,
# first-rollout and absent-ledger re-promotion success, failed post-promotion capability
# validation with verified rollback, a second-replacement failure with verified rollback,
# and an honestly reported failed rollback. Requires a real capability-passing executable
# pair to act as candidates. Never point this at the real primary checkout; every scenario
# runs in its own scratch repository whose coordination ledger is isolated by repository
# identity.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliExecutable,
	[Parameter(Mandatory = $true)]
	[string] $AgentHarnessExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'WorktreeCliSessionExclusion.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
Import-Module (Join-Path $sharedScripts 'WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1') -Force -DisableNameChecking

$script:Failures = [Collections.Generic.List[string]]::new()
$promotionScript = Join-Path $PSScriptRoot 'Invoke-AgentToolsPromotion.ps1'
$capabilitySource = Join-Path $sharedScripts 'Test-AgentToolsCapabilities.ps1'
$moduleSource = $sharedScripts
$WorktreeCliExecutable = (Get-Item -LiteralPath $WorktreeCliExecutable -ErrorAction Stop).FullName
$AgentHarnessExecutable = (Get-Item -LiteralPath $AgentHarnessExecutable -ErrorAction Stop).FullName

# Fail before allocating a scratch repository or ledger. These fixtures certify promotion
# mechanics only for an executable pair that can exercise the current capability contract.
& $capabilitySource -WorktreeCliExecutable $WorktreeCliExecutable -AgentHarnessExecutable $AgentHarnessExecutable | Out-Null

function Assert-True([bool] $Condition, [string] $Name) {
	if (-not $Condition) { $script:Failures.Add($Name); Write-Host "FAIL $Name" } else { Write-Host "pass $Name" }
}

function Get-Sha256([string] $Path) {
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-BytesOrNull([string] $Path) {
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
	return [IO.File]::ReadAllBytes($Path)
}

function Test-BytesEqual([byte[]] $Left, [byte[]] $Right) {
	if ($null -eq $Left -or $null -eq $Right) { return $null -eq $Left -and $null -eq $Right }
	return [System.Collections.StructuralComparisons]::StructuralEqualityComparer.Equals($Left, $Right)
}

$scratchParent = Join-Path ([IO.Path]::GetTempPath()) 'BrokenEnginePromotionFixtures'
$scratchLeaf = [guid]::NewGuid().ToString('N')
$scratchBase = Assert-SafeScratchRoot $scratchParent (Join-Path $scratchParent $scratchLeaf) $scratchLeaf
$primary = Join-Path $scratchBase 'primary'
$localAppData = Join-Path $scratchBase 'local-app-data'
$previousLocalAppData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA')
$sessionOwner = $null
$sessionRegistered = $false
$fixtureExitCode = 0

try {
New-Item -ItemType Directory -Force $primary | Out-Null
New-Item -ItemType Directory -Force $localAppData | Out-Null
[Environment]::SetEnvironmentVariable('LOCALAPPDATA', $localAppData)
Invoke-ScratchGit $primary @('init', '-b', 'main') | Out-Null
foreach ($tree in @('Tools\WorktreeCli', 'Tools\AgentHarness', 'Tools\ToolCommon')) {
	New-Item -ItemType Directory -Force (Join-Path $primary $tree) | Out-Null
	Set-Content (Join-Path $primary "$tree\source.txt") "fixture $tree"
}
New-Item -ItemType Directory -Force (Join-Path $primary '.agents\scripts') | Out-Null
Copy-Item -LiteralPath $capabilitySource -Destination (Join-Path $primary '.agents\scripts\Test-AgentToolsCapabilities.ps1') -Force
foreach ($module in @('AgentScriptCommon.psm1', 'WorktreeCliSessionExclusion.psm1', 'AgentWorktreeSession.psm1')) {
	Copy-Item -LiteralPath (Join-Path $moduleSource $module) -Destination (Join-Path $primary ".agents\scripts\$module") -Force
}
Set-Content (Join-Path $primary '.gitignore') "Temp`nOutput"
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'fixture base') | Out-Null
$landed = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()

# The coordination ledger self-initializes on the first transient claim; an absent ledger
# reads as an empty claim set for exclusive operations.
$candidateRoot = Join-Path $primary 'Temp\AgentToolsCandidate'
New-Item -ItemType Directory -Force $candidateRoot | Out-Null
$candidateWorktreeCli = Join-Path $candidateRoot 'WorktreeCli.exe'
$candidateAgentHarness = Join-Path $candidateRoot 'AgentHarness.exe'
Copy-Item -LiteralPath $WorktreeCliExecutable -Destination $candidateWorktreeCli -Force
Copy-Item -LiteralPath $AgentHarnessExecutable -Destination $candidateAgentHarness -Force

function Invoke-Promotion([string] $WorktreeCliCandidate, [string] $AgentHarnessCandidate, [string] $Commit, [string[]] $Extra = @()) {
	$stdout = @(& "$PSHOME\pwsh.exe" -NoProfile -File $promotionScript -PrimaryRoot $primary -WorktreeCliCandidate $WorktreeCliCandidate -AgentHarnessCandidate $AgentHarnessCandidate -LandedCommit $Commit @Extra 2>$null)
	$text = ($stdout -join "`n").Trim()
	$json = $null
	try { if (-not [string]::IsNullOrWhiteSpace($text)) { $json = $text | ConvertFrom-Json -Depth 32 -ErrorAction Stop } } catch { }
	return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Json = $json; Text = $text }
}

function Assert-Outcome($Run, [string] $Case, [int] $ExpectedExit, [string] $ExpectedCode) {
	Assert-True ($null -ne $Run.Json) "$Case emitted JSON"
	if ($null -eq $Run.Json) { Write-Host "  stdout: $($Run.Text)"; return }
	Assert-True ($Run.ExitCode -eq $ExpectedExit) "$Case exit=$ExpectedExit (was $($Run.ExitCode))"
	Assert-True ($Run.Json.code -ceq $ExpectedCode) "$Case code=$ExpectedCode (was $($Run.Json.code))"
	if ($Run.ExitCode -ne $ExpectedExit -or $Run.Json.code -cne $ExpectedCode) { Write-Host "  message: $($Run.Json.message)" }
}

$canonicalWorktreeCli = Join-Path $primary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
$canonicalAgentHarness = Join-Path $primary 'Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe'
$stampPath = Join-Path $primary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\AgentToolsSourceStamp.txt'

# 1. A missing or empty candidate is malformed input, never a promotion attempt.
$run = Invoke-Promotion (Join-Path $candidateRoot 'absent.exe') $candidateAgentHarness $landed
Assert-Outcome $run 'absent-candidate' 1 'promotion.failed'
Assert-True (-not (Test-Path -LiteralPath $canonicalWorktreeCli)) 'absent candidate promotes nothing'

# 2. Unlanded commit blocks.
Invoke-ScratchGit $primary @('checkout', '-q', '-b', 'side') | Out-Null
Set-Content (Join-Path $primary 'Tools\ToolCommon\side.txt') 'unlanded'
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'unlanded side work') | Out-Null
$unlanded = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()
Invoke-ScratchGit $primary @('checkout', '-q', 'main') | Out-Null
$run = Invoke-Promotion $candidateWorktreeCli $candidateAgentHarness $unlanded
Assert-Outcome $run 'not-landed' 2 'promotion.not-landed'

# 3. A malformed ledger is an authority blocker before the exclusive action can touch
# either canonical executable. The lock store self-initializes on a real claim; create it
# here to plant the malformed file.
$ledgerPath = (Get-WorktreeCliRepositoryIdentity $primary).LedgerPath
$ledgerBytes = Get-BytesOrNull $ledgerPath
$canonicalWorktreeCliBefore = Get-BytesOrNull $canonicalWorktreeCli
$canonicalAgentHarnessBefore = Get-BytesOrNull $canonicalAgentHarness
New-Item -ItemType Directory -Force (Split-Path -Parent $ledgerPath) | Out-Null
[IO.File]::WriteAllText($ledgerPath, '{', [Text.UTF8Encoding]::new($false))
try {
	$run = Invoke-Promotion $candidateWorktreeCli $candidateAgentHarness $landed
	Assert-Outcome $run 'malformed-ledger-exclusive-gate' 2 'promotion.coordination-unverifiable'
	if ($null -ne $run.Json) {
		Assert-True ($run.Json.disposition -ceq 'authority-required') 'malformed ledger exposes authority-required disposition'
		Assert-True $run.Json.blocker.requiresUserAuthority 'malformed ledger explicitly requires authority'
	}
	Assert-True (Test-BytesEqual $canonicalWorktreeCliBefore (Get-BytesOrNull $canonicalWorktreeCli)) 'malformed ledger leaves canonical WorktreeCli unchanged'
	Assert-True (Test-BytesEqual $canonicalAgentHarnessBefore (Get-BytesOrNull $canonicalAgentHarness)) 'malformed ledger leaves canonical AgentHarness unchanged'
}
finally {
	if ($null -eq $ledgerBytes) { Remove-Item -LiteralPath $ledgerPath -Force -ErrorAction SilentlyContinue }
	else { [IO.File]::WriteAllBytes($ledgerPath, $ledgerBytes) }
}

# 4. A live transient operation claim defers promotion; the cooperating owner does not
# self-block, and a different cooperating owner never exempts a peer claim.
$sessionOwner = [guid]::NewGuid().ToString()
Register-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner -Label 'fixture transient operation' -Worktree $primary | Out-Null
$sessionRegistered = $true
try {
	$run = Invoke-Promotion $candidateWorktreeCli $candidateAgentHarness $landed @('-WaitSeconds', '2')
	Assert-Outcome $run 'operation-claim-defers-promotion' 2 'promotion.shared-quiescence'
	if ($null -ne $run.Json) {
		Assert-True ($run.Json.disposition -ceq 'shared-quiescence') 'operation-claim-defers-promotion exposes top-level shared quiescence'
		Assert-True ($run.Json.blocker.disposition -ceq 'shared-quiescence') 'operation-claim-defers-promotion is retryable shared quiescence'
		Assert-True (-not $run.Json.blocker.requiresUserAuthority) 'operation-claim-defers-promotion needs no authority'
	}

	$peerCooperatingOwner = [guid]::NewGuid().ToString()
	$run = Invoke-Promotion $candidateWorktreeCli $candidateAgentHarness $landed @('-WaitSeconds', '2', '-CooperatingSessionOwner', $peerCooperatingOwner)
	Assert-Outcome $run 'peer-claim-defers-despite-cooperating-owner' 2 'promotion.shared-quiescence'
	if ($null -ne $run.Json) {
		Assert-True ($run.Json.disposition -ceq 'shared-quiescence') 'peer-claim-defers-despite-cooperating-owner exposes shared quiescence'
		Assert-True (-not $run.Json.blocker.requiresUserAuthority) 'peer-claim-defers-despite-cooperating-owner needs no authority'
	}

	# 5. First-rollout success with the transient operation claim cooperating.
	$run = Invoke-Promotion $candidateWorktreeCli $candidateAgentHarness $landed @('-WaitSeconds', '5', '-CooperatingSessionOwner', $sessionOwner)
	Assert-Outcome $run 'first-rollout' 0 'ok'
	if ($null -ne $run.Json -and $run.Json.status -ceq 'pass') {
		Assert-True ((Get-Sha256 $canonicalWorktreeCli) -ceq (Get-Sha256 $WorktreeCliExecutable)) 'first-rollout canonical WorktreeCli hash'
		Assert-True ((Get-Sha256 $canonicalAgentHarness) -ceq (Get-Sha256 $AgentHarnessExecutable)) 'first-rollout canonical AgentHarness hash'
		$promotionReceipt = Get-Content -LiteralPath $run.Json.receipt.path -Raw | ConvertFrom-Json -Depth 32
		Assert-True ($promotionReceipt.schemaVersion -ceq 'broken-engine-agenttools-promotion/v1') 'first-rollout receipt schema'
		Assert-True ($promotionReceipt.previous.WorktreeCli.present -eq $false) 'first-rollout previous absent'
		Assert-True ((Get-Content -LiteralPath $stampPath -Raw).Trim() -ceq $landed) 'first-rollout source stamp names the landed commit'
	}
}
finally {
	Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner
	$sessionRegistered = $false
}

# 6. Re-promotion over an existing pair records the previous identities and proceeds
# against an absent coordination ledger, self-initializing nothing.
$ledgerPath = (Get-WorktreeCliRepositoryIdentity $primary).LedgerPath
Remove-Item -LiteralPath $ledgerPath -Force -ErrorAction SilentlyContinue
Assert-True (-not (Test-Path -LiteralPath $ledgerPath)) 'absent coordination ledger precedes fresh-ledger promotion'
$run = Invoke-Promotion $candidateWorktreeCli $candidateAgentHarness $landed
Assert-Outcome $run 're-promotion' 0 'ok'
if ($null -ne $run.Json -and $run.Json.status -ceq 'pass') {
	$promotionReceipt = Get-Content -LiteralPath $run.Json.receipt.path -Raw | ConvertFrom-Json -Depth 32
	Assert-True ($promotionReceipt.previous.WorktreeCli.present -eq $true) 're-promotion previous present'
	Assert-True ($promotionReceipt.previous.WorktreeCli.sha256 -ceq (Get-Sha256 $WorktreeCliExecutable)) 're-promotion previous hash recorded'
}

# 7. Failed post-promotion capability validation rolls back the complete pair.
$beforeWorktreeCli = Get-Sha256 $canonicalWorktreeCli
$beforeAgentHarness = Get-Sha256 $canonicalAgentHarness
$garbageAgentHarness = Join-Path $candidateRoot 'garbage-AgentHarness.exe'
Copy-Item -LiteralPath $WorktreeCliExecutable -Destination $garbageAgentHarness -Force
$run = Invoke-Promotion $candidateWorktreeCli $garbageAgentHarness $landed
Assert-Outcome $run 'capability-rollback' 2 'promotion.rolled-back'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.rollback -ceq 'verified') 'capability-rollback verified'
	Assert-True ((Get-Sha256 $canonicalWorktreeCli) -ceq $beforeWorktreeCli) 'capability-rollback canonical WorktreeCli intact'
	Assert-True ((Get-Sha256 $canonicalAgentHarness) -ceq $beforeAgentHarness) 'capability-rollback canonical AgentHarness intact'
}

# 8. Second-replacement failure (canonical AgentHarness held open) rolls back the first
# replacement.
$holder = [IO.File]::Open($canonicalAgentHarness, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
try {
	$run = Invoke-Promotion $candidateWorktreeCli $candidateAgentHarness $landed
}
finally {
	$holder.Dispose()
}
Assert-Outcome $run 'replacement-rollback' 2 'promotion.rolled-back'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.rollback -ceq 'verified') 'replacement-rollback verified'
	Assert-True ((Get-Sha256 $canonicalWorktreeCli) -ceq $beforeWorktreeCli) 'replacement-rollback canonical WorktreeCli intact'
	Assert-True ((Get-Sha256 $canonicalAgentHarness) -ceq $beforeAgentHarness) 'replacement-rollback canonical AgentHarness intact'
}

# 9. Locked source stamp: the pair replaces but the stamp write and its rollback restore
# both fail, which must be reported as a failed rollback, never as verified.
$stampHolder = [IO.File]::Open($stampPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
	$run = Invoke-Promotion $candidateWorktreeCli $candidateAgentHarness $landed
}
finally {
	$stampHolder.Dispose()
}
Assert-Outcome $run 'stamp-rollback-failed' 1 'promotion.rollback-failed'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.rollback -ceq 'failed') 'stamp-rollback-failed reported honestly'
}

Write-Host ''
if ($script:Failures.Count -gt 0) {
	Write-Host "AgentTools promotion fixtures FAILED ($($script:Failures.Count) assertion(s))."
	$fixtureExitCode = 1
}
else {
	Write-Host 'AgentTools promotion fixtures passed.'
}
}
finally {
	if ($sessionRegistered -and $null -ne $sessionOwner) {
		try { Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner } catch { }
	}
	[Environment]::SetEnvironmentVariable('LOCALAPPDATA', $previousLocalAppData)
	$validatedScratch = Assert-SafeScratchRoot $scratchParent $scratchBase $scratchLeaf
	if (Test-Path -LiteralPath $validatedScratch) {
		Remove-Item -LiteralPath $validatedScratch -Recurse -Force -Confirm:$false
	}
}
exit $fixtureExitCode
