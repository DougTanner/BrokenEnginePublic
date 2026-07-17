# Deterministic fixtures for Invoke-AgentToolsPromotion.ps1 against a scratch
# primary repository: receipt identity, dirty candidate, unlanded commit,
# candidate/source mismatch, registered-session and held-maintenance blocking,
# cooperating-session promotion, first-rollout and re-promotion success,
# failed post-promotion capability validation with verified rollback, and a
# second-replacement failure with verified rollback. Requires a real
# capability-passing executable pair to act as candidates (for example the
# candidate pair from New-AgentToolsCandidate.ps1). Never point this at the real
# primary checkout; every scenario runs in its own scratch repository whose
# coordination ledger is isolated by repository identity.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliExecutable,
	[Parameter(Mandatory = $true)]
	[string] $AgentHarnessExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking

$script:Failures = [Collections.Generic.List[string]]::new()
$promotionScript = Join-Path $PSScriptRoot 'Invoke-AgentToolsPromotion.ps1'
$capabilitySource = Join-Path $PSScriptRoot '..\..\..\scripts\Test-AgentToolsCapabilities.ps1'
$WorktreeCliExecutable = (Get-Item -LiteralPath $WorktreeCliExecutable -ErrorAction Stop).FullName
$AgentHarnessExecutable = (Get-Item -LiteralPath $AgentHarnessExecutable -ErrorAction Stop).FullName

function Assert-True([bool] $Condition, [string] $Name) {
	if (-not $Condition) { $script:Failures.Add($Name); Write-Host "FAIL $Name" } else { Write-Host "pass $Name" }
}

function Get-Sha256([string] $Path) {
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Invoke-ScratchGit([string] $Root, [string[]] $Arguments) {
	$output = @(& git -C $Root -c user.name=fixture -c user.email=fixture@example.com @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
}

$primary = Join-Path ([IO.Path]::GetTempPath()) "BrokenEnginePromotionFixtures\$([guid]::NewGuid().ToString('N').Substring(0, 8))"
New-Item -ItemType Directory -Force $primary | Out-Null
Invoke-ScratchGit $primary @('init', '-b', 'main') | Out-Null
foreach ($tree in @('Tools\WorktreeCli', 'Tools\AgentHarness', 'Tools\ToolCommon')) {
	New-Item -ItemType Directory -Force (Join-Path $primary $tree) | Out-Null
	Set-Content (Join-Path $primary "$tree\source.txt") "fixture $tree"
}
New-Item -ItemType Directory -Force (Join-Path $primary '.agents\scripts') | Out-Null
Copy-Item -LiteralPath $capabilitySource -Destination (Join-Path $primary '.agents\scripts\Test-AgentToolsCapabilities.ps1') -Force
Set-Content (Join-Path $primary '.gitignore') "Temp`nOutput"
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'fixture base') | Out-Null
$landed = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()

# Initialize the scratch repository's isolated coordination ledger.
$initialOwner = [guid]::NewGuid().ToString()
Register-WorktreeCliSession -RepositoryRoot $primary -Owner $initialOwner -Label 'fixture-init' -Worktree $primary -LegacySessionsClosed | Out-Null
Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $initialOwner

function New-CandidateReceipt([string] $Root, [string] $WorktreeCliSource, [string] $AgentHarnessSource, [bool] $Dirty = $false, [string] $Suffix = '') {
	$candidateRoot = Join-Path $Root "Temp\AgentToolsCandidate$Suffix"
	$paths = [ordered]@{}
	foreach ($entry in @(@('WorktreeCli', $WorktreeCliSource), @('AgentHarness', $AgentHarnessSource))) {
		$directory = Join-Path $candidateRoot $entry[0]
		New-Item -ItemType Directory -Force $directory | Out-Null
		$destination = Join-Path $directory "$($entry[0]).exe"
		Copy-Item -LiteralPath $entry[1] -Destination $destination -Force
		$paths[$entry[0]] = $destination
	}
	$trees = @(Invoke-ScratchGit $Root @('rev-parse', 'HEAD:Tools/WorktreeCli', 'HEAD:Tools/AgentHarness', 'HEAD:Tools/ToolCommon')) | ForEach-Object { $_.Trim() }
	$receipt = [ordered]@{
		schemaVersion = 'broken-engine-agenttools-candidate/v1'
		createdAt = [DateTime]::UtcNow.ToString('O')
		worktree = $Root
		sourceCommit = (@(Invoke-ScratchGit $Root @('rev-parse', 'HEAD')))[0].Trim()
		toolTreeHashes = [ordered]@{ worktreeCli = $trees[0]; agentHarness = $trees[1]; toolCommon = $trees[2] }
		dirtyToolPaths = $Dirty
		msBuild = 'fixture'
		executables = [ordered]@{
			WorktreeCli = [ordered]@{ path = $paths.WorktreeCli; sha256 = (Get-Sha256 $paths.WorktreeCli); bytes = (Get-Item $paths.WorktreeCli).Length }
			AgentHarness = [ordered]@{ path = $paths.AgentHarness; sha256 = (Get-Sha256 $paths.AgentHarness); bytes = (Get-Item $paths.AgentHarness).Length }
		}
		canonical = [ordered]@{ unchanged = $true }
		capabilityCheck = 'pass'
	}
	$receiptPath = Join-Path $candidateRoot 'candidate-receipt.json'
	[IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
	return [pscustomobject]@{ Path = $receiptPath; Sha256 = (Get-Sha256 $receiptPath) }
}

function Invoke-Promotion([string] $ReceiptPath, [string] $ReceiptSha256, [string] $Commit, [string[]] $Extra = @()) {
	$stdout = @(& "$PSHOME\pwsh.exe" -NoProfile -File $promotionScript -PrimaryRoot $primary -CandidateReceiptPath $ReceiptPath -CandidateReceiptSha256 $ReceiptSha256 -LandedCommit $Commit @Extra 2>$null)
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
$receipt = New-CandidateReceipt $primary $WorktreeCliExecutable $AgentHarnessExecutable

# 1. Receipt identity: tampered bytes must block.
$tamperedPath = "$($receipt.Path).tampered.json"
[IO.File]::WriteAllText($tamperedPath, ([IO.File]::ReadAllText($receipt.Path) + ' '))
$run = Invoke-Promotion $tamperedPath $receipt.Sha256 $landed
Assert-Outcome $run 'receipt-identity' 2 'promotion.receipt-identity'

# 2. Dirty candidate blocks.
$dirtyReceipt = New-CandidateReceipt $primary $WorktreeCliExecutable $AgentHarnessExecutable $true 'Dirty'
$run = Invoke-Promotion $dirtyReceipt.Path $dirtyReceipt.Sha256 $landed
Assert-Outcome $run 'dirty-candidate' 2 'promotion.dirty-candidate'

# 3. Unlanded commit blocks.
Invoke-ScratchGit $primary @('checkout', '-q', '-b', 'side') | Out-Null
Set-Content (Join-Path $primary 'Tools\ToolCommon\side.txt') 'unlanded'
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'unlanded side work') | Out-Null
$unlanded = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()
Invoke-ScratchGit $primary @('checkout', '-q', 'main') | Out-Null
$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $unlanded
Assert-Outcome $run 'not-landed' 2 'promotion.not-landed'

# 4. Registered foreign session blocks; the cooperating session does not self-block.
$sessionOwner = [guid]::NewGuid().ToString()
Register-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner -Label 'fixture-session' -Worktree $primary | Out-Null
try {
	$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed @('-WaitSeconds', '2')
	Assert-Outcome $run 'session-blocked' 2 'promotion.coordination-blocked'

	# 5. First-rollout success with the registered session cooperating.
	$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed @('-WaitSeconds', '5', '-CooperatingSessionOwner', $sessionOwner)
	Assert-Outcome $run 'first-rollout' 0 'ok'
	if ($null -ne $run.Json -and $run.Json.status -ceq 'pass') {
		Assert-True ((Get-Sha256 $canonicalWorktreeCli) -ceq (Get-Sha256 $WorktreeCliExecutable)) 'first-rollout canonical WorktreeCli hash'
		Assert-True ((Get-Sha256 $canonicalAgentHarness) -ceq (Get-Sha256 $AgentHarnessExecutable)) 'first-rollout canonical AgentHarness hash'
		$promotionReceipt = Get-Content -LiteralPath $run.Json.receipt.path -Raw | ConvertFrom-Json -Depth 32
		Assert-True ($promotionReceipt.schemaVersion -ceq 'broken-engine-agenttools-promotion/v1') 'first-rollout receipt schema'
		Assert-True ($promotionReceipt.previous.WorktreeCli.present -eq $false) 'first-rollout previous absent'
		$stamp = (Get-Content -LiteralPath (Join-Path $primary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\AgentToolsSourceStamp.txt') -Raw).Trim() -split "`n" | ForEach-Object { $_.Trim() }
		$expectedTrees = @(Invoke-ScratchGit $primary @('rev-parse', "${landed}:Tools/WorktreeCli", "${landed}:Tools/AgentHarness", "${landed}:Tools/ToolCommon")) | ForEach-Object { $_.Trim() }
		Assert-True (($stamp -join '|') -ceq ($expectedTrees -join '|')) 'first-rollout source stamp'
	}
}
finally {
	Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner
}

# 6. Held maintenance blocks.
$maintenanceOwner = [guid]::NewGuid().ToString()
Enter-WorktreeCliMaintenance -RepositoryRoot $primary -Owner $maintenanceOwner -Label 'fixture-maintenance' -Worktree $primary | Out-Null
try {
	$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed @('-WaitSeconds', '2')
	Assert-Outcome $run 'maintenance-blocked' 2 'promotion.coordination-blocked'
}
finally {
	Exit-WorktreeCliMaintenance -RepositoryRoot $primary -Owner $maintenanceOwner
}

# 7. Re-promotion over an existing pair records the previous identities.
$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed
Assert-Outcome $run 're-promotion' 0 'ok'
if ($null -ne $run.Json -and $run.Json.status -ceq 'pass') {
	$promotionReceipt = Get-Content -LiteralPath $run.Json.receipt.path -Raw | ConvertFrom-Json -Depth 32
	Assert-True ($promotionReceipt.previous.WorktreeCli.present -eq $true) 're-promotion previous present'
	Assert-True ($promotionReceipt.previous.WorktreeCli.sha256 -ceq (Get-Sha256 $WorktreeCliExecutable)) 're-promotion previous hash recorded'
}

# 8. Failed post-promotion capability validation rolls back the complete pair.
$beforeWorktreeCli = Get-Sha256 $canonicalWorktreeCli
$beforeAgentHarness = Get-Sha256 $canonicalAgentHarness
$garbagePath = Join-Path $primary 'Temp\garbage-agentharness.exe'
Set-Content -LiteralPath $garbagePath 'this is not a portable executable'
$garbageReceipt = New-CandidateReceipt $primary $WorktreeCliExecutable $garbagePath $false 'Garbage'
$run = Invoke-Promotion $garbageReceipt.Path $garbageReceipt.Sha256 $landed
Assert-Outcome $run 'capability-rollback' 2 'promotion.rolled-back'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.rollback -ceq 'verified') 'capability-rollback verified'
	Assert-True ((Get-Sha256 $canonicalWorktreeCli) -ceq $beforeWorktreeCli) 'capability-rollback canonical WorktreeCli intact'
	Assert-True ((Get-Sha256 $canonicalAgentHarness) -ceq $beforeAgentHarness) 'capability-rollback canonical AgentHarness intact'
}

# 9. Second-replacement failure (canonical AgentHarness held open) rolls back the first replacement.
$holder = [IO.File]::Open($canonicalAgentHarness, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
try {
	$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed
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

# 10. Locked source stamp: the pair replaces but the stamp write and its rollback restore
# both fail, which must be reported as a failed rollback, never as verified.
$stampPath = Join-Path $primary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\AgentToolsSourceStamp.txt'
$stampHolder = [IO.File]::Open($stampPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
	$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed
}
finally {
	$stampHolder.Dispose()
}
Assert-Outcome $run 'stamp-rollback-failed' 1 'promotion.rollback-failed'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.rollback -ceq 'failed') 'stamp-rollback-failed reported honestly'
}

# 11. Candidate/source mismatch after primary advanced its tool trees.
Set-Content (Join-Path $primary 'Tools\ToolCommon\source.txt') 'fixture changed'
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'tool change') | Out-Null
$newLanded = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()
$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $newLanded
Assert-Outcome $run 'source-mismatch' 2 'promotion.source-mismatch'

Write-Host ''
if ($script:Failures.Count -gt 0) {
	Write-Host "AgentTools promotion fixtures FAILED ($($script:Failures.Count) assertion(s))."
	exit 1
}
Write-Host 'AgentTools promotion fixtures passed (11 scenarios).'
exit 0
