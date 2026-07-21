# Deterministic fixtures for Invoke-AgentToolsPromotion.ps1 against a scratch
# primary repository: v2-only receipt identity, source and binary tampering,
# unlanded commit, candidate/source mismatch, session and maintenance blocking,
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
$moduleSource = Join-Path $PSScriptRoot '..\..\..\scripts'
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

function Assert-SafeScratchRoot([string] $Parent, [string] $Root, [string] $ExpectedLeaf) {
	$parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
	$rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
	if ((Split-Path -Parent $rootPath) -cne $parentPath -or (Split-Path -Leaf $rootPath) -cne $ExpectedLeaf -or $ExpectedLeaf -cnotmatch '^[0-9a-f]{32}$') {
		throw "Fixture scratch root failed containment validation: '$rootPath'."
	}
	return $rootPath
}

$scratchParent = Join-Path ([IO.Path]::GetTempPath()) 'BrokenEnginePromotionFixtures'
$scratchLeaf = [guid]::NewGuid().ToString('N')
$scratchBase = Assert-SafeScratchRoot $scratchParent (Join-Path $scratchParent $scratchLeaf) $scratchLeaf
$primary = Join-Path $scratchBase 'primary'
$localAppData = Join-Path $scratchBase 'local-app-data'
$previousLocalAppData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA')
$initialOwner = $null
$initialRegistered = $false
$sessionOwner = $null
$sessionRegistered = $false
$maintenanceOwner = $null
$maintenanceHeld = $false
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
$submoduleSource = Join-Path $scratchBase 'tinygltf-source'
New-Item -ItemType Directory -Force $submoduleSource | Out-Null
Invoke-ScratchGit $submoduleSource @('init', '-b', 'main') | Out-Null
Set-Content (Join-Path $submoduleSource 'json.hpp') 'fixture json'
Invoke-ScratchGit $submoduleSource @('add', 'json.hpp') | Out-Null
Invoke-ScratchGit $submoduleSource @('commit', '-m', 'fixture json base') | Out-Null
$baseJsonOwner = (@(Invoke-ScratchGit $submoduleSource @('rev-parse', 'HEAD')))[0].Trim()
Invoke-ScratchGit $primary @('-c', 'protocol.file.allow=always', 'submodule', 'add', '--quiet', $submoduleSource, 'ThirdParty/tinygltf') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $primary '.agents\scripts') | Out-Null
Copy-Item -LiteralPath $capabilitySource -Destination (Join-Path $primary '.agents\scripts\Test-AgentToolsCapabilities.ps1') -Force
foreach ($module in @('AgentScriptCommon.psm1', 'WorktreeCliSessionExclusion.psm1')) {
	Copy-Item -LiteralPath (Join-Path $moduleSource $module) -Destination (Join-Path $primary ".agents\scripts\$module") -Force
}
Set-Content (Join-Path $primary '.gitignore') "Temp`nOutput"
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'fixture base') | Out-Null
$landed = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()

# Initialize the scratch repository's isolated coordination ledger.
$initialOwner = [guid]::NewGuid().ToString()
Register-WorktreeCliSession -RepositoryRoot $primary -Owner $initialOwner -Label 'fixture-init' -Worktree $primary -LegacySessionsClosed | Out-Null
$initialRegistered = $true
Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $initialOwner
$initialRegistered = $false

function New-CandidateReceipt([string] $Root, [string] $WorktreeCliSource, [string] $AgentHarnessSource, [string] $Suffix = '') {
	$candidateRoot = Join-Path $Root "Temp\AgentToolsCandidate$Suffix"
	$paths = [ordered]@{}
	foreach ($entry in @(@('WorktreeCli', $WorktreeCliSource), @('AgentHarness', $AgentHarnessSource))) {
		$directory = Join-Path $candidateRoot $entry[0]
		New-Item -ItemType Directory -Force $directory | Out-Null
		$destination = Join-Path $directory "$($entry[0]).exe"
		Copy-Item -LiteralPath $entry[1] -Destination $destination -Force
		$paths[$entry[0]] = $destination
	}
	$sourcePaths = @(Invoke-ScratchGit $Root @('-c', 'core.quotePath=false', 'ls-files', '--cached', '--others', '--exclude-standard', '--', 'Tools/WorktreeCli', 'Tools/AgentHarness', 'Tools/ToolCommon')) | ForEach-Object { $_.Trim().Replace('\', '/') }
	$sourcePaths += 'ThirdParty/tinygltf/json.hpp'
	$sourcePaths = @($sourcePaths | Select-Object -Unique)
	[Array]::Sort($sourcePaths, [StringComparer]::Ordinal)
	$manifest = @()
	$digestLines = [Collections.Generic.List[string]]::new()
	foreach ($sourcePath in $sourcePaths) {
		$fullPath = Join-Path $Root ($sourcePath.Replace('/', '\'))
		$item = Get-Item -LiteralPath $fullPath -Force
		$sha256 = Get-Sha256 $fullPath
		$gitBlob = (@(Invoke-ScratchGit $Root @('hash-object', "--path=$sourcePath", '--', $fullPath)))[0].Trim()
		$manifest += [ordered]@{ path = $sourcePath; bytes = $item.Length; sha256 = $sha256; gitBlob = $gitBlob }
		$digestLines.Add("$sourcePath`0$($item.Length)`0$sha256`0$gitBlob")
	}
	$digestText = (($digestLines -join "`n") + "`n")
	$digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.UTF8Encoding]::new($false).GetBytes($digestText))).ToLowerInvariant()
	$fixtureLog = Join-Path $candidateRoot 'fixture.read.tlog'
	[IO.File]::WriteAllText($fixtureLog, 'fixture dependency evidence')
	$fixtureLogItem = Get-Item -LiteralPath $fixtureLog
	$toolItem = Get-Item -LiteralPath $WorktreeCliSource
	$capabilityItem = Get-Item -LiteralPath $capabilitySource
	$absentWorktreeCli = [ordered]@{ path = (Join-Path $Root 'canonical-WorktreeCli.exe'); present = $false; bytes = $null; sha256 = $null; lastWriteUtc = $null }
	$absentAgentHarness = [ordered]@{ path = (Join-Path $Root 'canonical-AgentHarness.exe'); present = $false; bytes = $null; sha256 = $null; lastWriteUtc = $null }
	$receipt = [ordered]@{
		schemaVersion = 'broken-engine-agenttools-candidate/v2'
		createdAt = [DateTime]::UtcNow.ToString('O')
		checkout = [ordered]@{ root = $Root; gitCommonDirectory = (Join-Path $Root '.git'); sourceCommit = (@(Invoke-ScratchGit $Root @('rev-parse', 'HEAD')))[0].Trim() }
		source = [ordered]@{
			algorithm = 'sorted-path-bytes-sha256-clean-filter-blob/v1'
			before = [ordered]@{ digest = $digest; manifest = $manifest }
			after = [ordered]@{ digest = $digest; manifest = $manifest }
			compilerDependencies = [ordered]@{ logs = @([ordered]@{ path = $fixtureLog; bytes = $fixtureLogItem.Length; sha256 = (Get-Sha256 $fixtureLog) }); repoLocalInputs = @($sourcePaths); blocker = $null }
		}
		toolchain = [ordered]@{ msBuild = [ordered]@{ path = $toolItem.FullName; bytes = $toolItem.Length; sha256 = (Get-Sha256 $toolItem.FullName); fileVersion = 'fixture'; productVersion = 'fixture' }; configuration = 'Release'; platform = 'x64' }
		executables = [ordered]@{
			WorktreeCli = [ordered]@{ path = $paths.WorktreeCli; sha256 = (Get-Sha256 $paths.WorktreeCli); bytes = (Get-Item $paths.WorktreeCli).Length }
			AgentHarness = [ordered]@{ path = $paths.AgentHarness; sha256 = (Get-Sha256 $paths.AgentHarness); bytes = (Get-Item $paths.AgentHarness).Length }
		}
		canonical = [ordered]@{
			before = [ordered]@{ worktreeCli = $absentWorktreeCli; agentHarness = $absentAgentHarness }
			after = [ordered]@{ worktreeCli = $absentWorktreeCli; agentHarness = $absentAgentHarness }
			unchanged = $true
		}
		capability = [ordered]@{ status = 'pass'; script = [ordered]@{ path = $capabilityItem.FullName; bytes = $capabilityItem.Length; sha256 = (Get-Sha256 $capabilityItem.FullName) } }
	}
	$receiptPath = Join-Path $candidateRoot 'candidate-receipt.json'
	[IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
	return [pscustomobject]@{ Path = $receiptPath; Sha256 = (Get-Sha256 $receiptPath) }
}

function Invoke-Promotion([string] $ReceiptPath, [string] $ReceiptSha256, [string] $Commit, [string[]] $Extra = @(), [string] $Script = $promotionScript) {
	$stdout = @(& "$PSHOME\pwsh.exe" -NoProfile -File $Script -PrimaryRoot $primary -CandidateReceiptPath $ReceiptPath -CandidateReceiptSha256 $ReceiptSha256 -LandedCommit $Commit @Extra 2>$null)
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
Assert-Outcome $run 'receipt-identity' 2 'promotion.certification.receipt-identity'

# 2. Legacy v1 receipts are rejected without compatibility fallback.
$legacyPath = Join-Path $primary 'Temp\legacy-v1.json'
[IO.File]::WriteAllText($legacyPath, '{"schemaVersion":"broken-engine-agenttools-candidate/v1"}')
$run = Invoke-Promotion $legacyPath (Get-Sha256 $legacyPath) $landed
Assert-Outcome $run 'legacy-v1' 2 'promotion.certification.receipt-schema'

# 3. Current source bytes changing after candidate production requires rebuild.
$sourcePath = Join-Path $primary 'Tools\ToolCommon\source.txt'
$sourceBefore = [IO.File]::ReadAllText($sourcePath)
[IO.File]::WriteAllText($sourcePath, 'changed after candidate')
$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed
Assert-Outcome $run 'source-tamper' 2 'promotion.certification.source-bytes'
[IO.File]::WriteAllText($sourcePath, $sourceBefore)

# 4. Candidate binary tampering requires rebuild before coordination.
$binaryReceipt = New-CandidateReceipt $primary $WorktreeCliExecutable $AgentHarnessExecutable 'BinaryTamper'
[IO.File]::WriteAllText((Get-Content -LiteralPath $binaryReceipt.Path -Raw | ConvertFrom-Json -Depth 100).executables.AgentHarness.path, 'tampered binary')
$run = Invoke-Promotion $binaryReceipt.Path $binaryReceipt.Sha256 $landed
Assert-Outcome $run 'binary-tamper' 2 'promotion.certification.candidate-identity'

# 5. A real submodule checkout advanced beyond the superproject gitlink cannot
# substitute its current json.hpp blob for the expected gitlink commit's blob.
$jsonCheckout = Join-Path $primary 'ThirdParty\tinygltf'
Set-Content (Join-Path $jsonCheckout 'json.hpp') 'fixture json advanced outside superproject'
Invoke-ScratchGit $jsonCheckout @('add', 'json.hpp') | Out-Null
Invoke-ScratchGit $jsonCheckout @('commit', '-m', 'advanced json not recorded by superproject') | Out-Null
$submoduleMismatchReceipt = New-CandidateReceipt $primary $WorktreeCliExecutable $AgentHarnessExecutable 'SubmoduleMismatch'
$run = Invoke-Promotion $submoduleMismatchReceipt.Path $submoduleMismatchReceipt.Sha256 $landed
Assert-Outcome $run 'submodule-gitlink-mismatch' 2 'promotion.certification.commit-mismatch'
Invoke-ScratchGit $jsonCheckout @('checkout', '--detach', $baseJsonOwner) | Out-Null

# 6. Unlanded commit blocks.
Invoke-ScratchGit $primary @('checkout', '-q', '-b', 'side') | Out-Null
Set-Content (Join-Path $primary 'Tools\ToolCommon\side.txt') 'unlanded'
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'unlanded side work') | Out-Null
$unlanded = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()
Invoke-ScratchGit $primary @('checkout', '-q', 'main') | Out-Null
$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $unlanded
Assert-Outcome $run 'not-landed' 2 'promotion.not-landed'

# 6. Registered foreign session blocks; the cooperating session does not self-block.
$sessionOwner = [guid]::NewGuid().ToString()
Register-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner -Label 'fixture-session' -Worktree $primary | Out-Null
$sessionRegistered = $true
try {
	$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed @('-WaitSeconds', '2')
	Assert-Outcome $run 'session-blocked' 2 'promotion.coordination-blocked'

	# 7. First-rollout success with the registered session cooperating.
	$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed @('-WaitSeconds', '5', '-CooperatingSessionOwner', $sessionOwner)
	Assert-Outcome $run 'first-rollout' 0 'ok'
	if ($null -ne $run.Json -and $run.Json.status -ceq 'pass') {
		Assert-True ((Get-Sha256 $canonicalWorktreeCli) -ceq (Get-Sha256 $WorktreeCliExecutable)) 'first-rollout canonical WorktreeCli hash'
		Assert-True ((Get-Sha256 $canonicalAgentHarness) -ceq (Get-Sha256 $AgentHarnessExecutable)) 'first-rollout canonical AgentHarness hash'
		$promotionReceipt = Get-Content -LiteralPath $run.Json.receipt.path -Raw | ConvertFrom-Json -Depth 32
		Assert-True ($promotionReceipt.schemaVersion -ceq 'broken-engine-agenttools-promotion/v1') 'first-rollout receipt schema'
		Assert-True ($promotionReceipt.previous.WorktreeCli.present -eq $false) 'first-rollout previous absent'
		$stamp = (Get-Content -LiteralPath (Join-Path $primary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\AgentToolsSourceStamp.txt') -Raw).Trim() -split "`n" | ForEach-Object { $_.Trim() }
		$expectedTrees = @($promotionReceipt.certifiedSource.commitIdentities.worktreeCli, $promotionReceipt.certifiedSource.commitIdentities.agentHarness, $promotionReceipt.certifiedSource.commitIdentities.toolCommon)
		Assert-True (($stamp -join '|') -ceq ($expectedTrees -join '|')) 'first-rollout source stamp'
	}
}
finally {
	Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner
	$sessionRegistered = $false
}

# 8. Held maintenance blocks.
$maintenanceOwner = [guid]::NewGuid().ToString()
Enter-WorktreeCliMaintenance -RepositoryRoot $primary -Owner $maintenanceOwner -Label 'fixture-maintenance' -Worktree $primary | Out-Null
$maintenanceHeld = $true
try {
	$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed @('-WaitSeconds', '2')
	Assert-Outcome $run 'maintenance-blocked' 2 'promotion.coordination-blocked'
}
finally {
	Exit-WorktreeCliMaintenance -RepositoryRoot $primary -Owner $maintenanceOwner
	$maintenanceHeld = $false
}

# 9. Re-promotion over an existing pair records the previous identities.
$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $landed
Assert-Outcome $run 're-promotion' 0 'ok'
if ($null -ne $run.Json -and $run.Json.status -ceq 'pass') {
	$promotionReceipt = Get-Content -LiteralPath $run.Json.receipt.path -Raw | ConvertFrom-Json -Depth 32
	Assert-True ($promotionReceipt.previous.WorktreeCli.present -eq $true) 're-promotion previous present'
	Assert-True ($promotionReceipt.previous.WorktreeCli.sha256 -ceq (Get-Sha256 $WorktreeCliExecutable)) 're-promotion previous hash recorded'
}

# 10. Failed post-promotion capability validation rolls back the complete pair.
$beforeWorktreeCli = Get-Sha256 $canonicalWorktreeCli
$beforeAgentHarness = Get-Sha256 $canonicalAgentHarness
$garbagePath = Join-Path $primary 'Temp\garbage-agentharness.exe'
Copy-Item -LiteralPath $WorktreeCliExecutable -Destination $garbagePath -Force
$garbageReceipt = New-CandidateReceipt $primary $WorktreeCliExecutable $garbagePath 'Garbage'
$run = Invoke-Promotion $garbageReceipt.Path $garbageReceipt.Sha256 $landed
Assert-Outcome $run 'capability-rollback' 2 'promotion.rolled-back'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.rollback -ceq 'verified') 'capability-rollback verified'
	Assert-True ((Get-Sha256 $canonicalWorktreeCli) -ceq $beforeWorktreeCli) 'capability-rollback canonical WorktreeCli intact'
	Assert-True ((Get-Sha256 $canonicalAgentHarness) -ceq $beforeAgentHarness) 'capability-rollback canonical AgentHarness intact'
}

# 11. Second-replacement failure (canonical AgentHarness held open) rolls back the first replacement.
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

# 12. Locked source stamp: the pair replaces but the stamp write and its rollback restore
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

# 14. The certification result remains authoritative if the receipt path is
# swapped after certification. Promotion must not reopen the tampered receipt.
$fixtureScriptDirectory = Join-Path $primary '.agents\skills\finalize-changes\scripts'
New-Item -ItemType Directory -Force $fixtureScriptDirectory | Out-Null
$fixturePromotionScript = Join-Path $fixtureScriptDirectory 'Invoke-AgentToolsPromotion.ps1'
Copy-Item -LiteralPath $promotionScript -Destination $fixturePromotionScript -Force
$fixtureCertificationScript = Join-Path $fixtureScriptDirectory 'Test-AgentToolsCandidateCertification.ps1'
$fixtureCertification = @'
[CmdletBinding()]
param(
	[string] $RepositoryRoot,
	[string] $WorktreeRoot,
	[string] $CandidateReceiptPath,
	[string] $CandidateReceiptSha256,
	[string] $ExpectedCommit
)
$ErrorActionPreference = 'Stop'
$receipt = [IO.File]::ReadAllText($CandidateReceiptPath) | ConvertFrom-Json -Depth 100
$trees = @(& git -C $RepositoryRoot rev-parse "$ExpectedCommit`:Tools/WorktreeCli" "$ExpectedCommit`:Tools/AgentHarness" "$ExpectedCommit`:Tools/ToolCommon")
if ($LASTEXITCODE -ne 0 -or $trees.Count -ne 3) { throw 'fixture tree lookup failed' }
$certification = [ordered]@{
	schemaVersion = 'broken-engine-agenttools-certification-result/v1'
	status = 'pass'
	code = 'ok'
	message = 'fixture certification passed before receipt swap'
	receipt = [ordered]@{ path = $CandidateReceiptPath; sha256 = $CandidateReceiptSha256; schemaVersion = $receipt.schemaVersion }
	expectedCommit = $ExpectedCommit
	source = [ordered]@{
		digest = $receipt.source.after.digest
		manifestCount = @($receipt.source.after.manifest).Count
		commitIdentities = [ordered]@{ worktreeCli = $trees[0].Trim(); agentHarness = $trees[1].Trim(); toolCommon = $trees[2].Trim() }
		primaryResolvedJson = [ordered]@{ path = 'ThirdParty/tinygltf/json.hpp'; ownerGitObject = 'fixture'; gitBlob = 'fixture' }
	}
	executables = $receipt.executables
}
[IO.File]::WriteAllText($CandidateReceiptPath, [IO.File]::ReadAllText($env:BROKEN_ENGINE_SWAP_RECEIPT_PATH), [Text.UTF8Encoding]::new($false))
[Console]::Out.Write(($certification | ConvertTo-Json -Depth 100 -Compress))
exit 0
'@
[IO.File]::WriteAllText($fixtureCertificationScript, $fixtureCertification, [Text.UTF8Encoding]::new($false))
$swapReceipt = New-CandidateReceipt $primary $WorktreeCliExecutable $AgentHarnessExecutable 'ReceiptSwap'
$previousSwapReceiptPath = [Environment]::GetEnvironmentVariable('BROKEN_ENGINE_SWAP_RECEIPT_PATH')
try {
	[Environment]::SetEnvironmentVariable('BROKEN_ENGINE_SWAP_RECEIPT_PATH', $garbageReceipt.Path)
	$run = Invoke-Promotion -ReceiptPath $swapReceipt.Path -ReceiptSha256 $swapReceipt.Sha256 -Commit $landed -Script $fixturePromotionScript
}
finally {
	[Environment]::SetEnvironmentVariable('BROKEN_ENGINE_SWAP_RECEIPT_PATH', $previousSwapReceiptPath)
}
Assert-Outcome $run 'post-certification-receipt-swap' 0 'ok'
if ($null -ne $run.Json -and $run.Json.status -ceq 'pass') {
	Assert-True ((Get-Sha256 $canonicalWorktreeCli) -ceq (Get-Sha256 $WorktreeCliExecutable)) 'receipt-swap certified WorktreeCli promoted'
	Assert-True ((Get-Sha256 $canonicalAgentHarness) -ceq (Get-Sha256 $AgentHarnessExecutable)) 'receipt-swap certified AgentHarness promoted'
}

# 15. Candidate/source mismatch after primary advanced its tool trees.
Set-Content (Join-Path $primary 'Tools\ToolCommon\source.txt') 'fixture changed'
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'tool change') | Out-Null
$newLanded = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()
$run = Invoke-Promotion $receipt.Path $receipt.Sha256 $newLanded
Assert-Outcome $run 'source-mismatch' 2 'promotion.certification.source-bytes'

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
	if ($initialRegistered -and $null -ne $initialOwner) {
		try { Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $initialOwner } catch { }
	}
	if ($maintenanceHeld -and $null -ne $maintenanceOwner) {
		try { Exit-WorktreeCliMaintenance -RepositoryRoot $primary -Owner $maintenanceOwner } catch { }
	}
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
