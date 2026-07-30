


[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $Executable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$fixtureRoot = Join-Path $repositoryRoot "Temp/NextPlanWorkflowFixtures/$([guid]::NewGuid().ToString('N'))"
$artifactRoot = $null
$owner = $null
$originalEnvironment = @{}
# LOCALAPPDATA is isolated to the scratch store for the sidecars' own scheduler state; restore it
# last so scratch cleanup can still reach it.
$originalLocalAppData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA')
# The sidecars no longer consult BROKEN_ENGINE_* provenance (the in-worktree receipt is the sole
# trust anchor); only the sidecar-fixture selector env var is set by this suite, so only it is saved
# and restored.
$environmentNames = @('BROKEN_ENGINE_WORKTREECLI_SIDECAR_FIXTURE')

function Invoke-Process([string] $FilePath, [string[]] $Arguments, [string] $WorkingDirectory) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $FilePath
	$start.WorkingDirectory = $WorkingDirectory
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	foreach ($argument in $Arguments) { [void]$start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start '$FilePath'." }
	$stdout = $process.StandardOutput.ReadToEndAsync()
	$stderr = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$result = [pscustomobject]@{ ExitCode = $process.ExitCode; Stdout = $stdout.GetAwaiter().GetResult(); Stderr = $stderr.GetAwaiter().GetResult() }
	$process.Dispose()
	return $result
}

function Invoke-Git([string] $WorkingDirectory, [string[]] $Arguments) {
	$result = Invoke-Process 'git.exe' $Arguments $WorkingDirectory
	if ($result.ExitCode -ne 0) { throw "git $($Arguments -join ' ') failed: $($result.Stdout)$($result.Stderr)" }
	return $result.Stdout.Trim()
}

function Set-Utf8File([string] $Path, [string] $Text) {
	[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
	[IO.File]::WriteAllText($Path, $Text, $utf8)
}

function New-WorktreeCliSidecarFixture([string] $Path) {
	$source = @'
using System;
using System.Linq;
public static class WorktreeCliSidecarFixture {
 public static int Main(string[] args) {
  string mode = Environment.GetEnvironmentVariable("BROKEN_ENGINE_WORKTREECLI_SIDECAR_FIXTURE") ?? "";
  bool claimStatus = args.Contains("claim-status");
  bool prepare = args.Contains("prepare-completion") || args.Contains("prepare-rejection");
  if (claimStatus && mode.StartsWith("prepare-")) { Console.Write("{\"ownedByReceipt\":true,\"status\":\"ok\",\"code\":\"claimed\"}"); return 0; }
  int exitCode = mode.EndsWith("-2") ? 2 : 1;
  Console.Write("{\"status\":\"" + (exitCode == 2 ? "blocked" : "error") + "\",\"code\":\"fixture\"}");
  return exitCode;
 }
}
'@
	$sourcePath = [IO.Path]::ChangeExtension($Path, '.cs')
	Set-Utf8File $sourcePath $source
	$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
	$response = Invoke-Process $csc @('/nologo','/target:exe',"/out:$Path",$sourcePath) (Split-Path -Parent $Path)
	if ($response.ExitCode -ne 0) { throw "Could not compile WorktreeCli sidecar fixture: $($response.Stdout)$($response.Stderr)" }
}

function ConvertFrom-SingleJson($Response, [int] $ExpectedExit, [string] $Label) {
	if ($Response.ExitCode -ne $ExpectedExit) { throw "$Label exited $($Response.ExitCode), expected $ExpectedExit. stdout=$($Response.Stdout) stderr=$($Response.Stderr)" }
	try { return $Response.Stdout | ConvertFrom-Json -Depth 100 -ErrorAction Stop }
	catch { throw "$Label did not return exactly one JSON value: $($Response.Stdout)" }
}

function Invoke-Sidecar([string] $Name, [string[]] $Arguments, [int] $ExpectedExit) {
	$response = Invoke-Process (Join-Path $PSHOME 'pwsh.exe') (@('-NoLogo','-NoProfile','-File',(Join-Path $PSScriptRoot $Name)) + $Arguments) $script:session
	return ConvertFrom-SingleJson $response $ExpectedExit $Name
}

function Release-Claim($Claim) {
	$outcome = Invoke-Sidecar 'Defer-NextPlan.ps1' @() 0
	Assert-True ($outcome.status -ceq 'pass' -and $outcome.code -ceq 'released' -and $outcome.claim.released -and (Test-Path -LiteralPath (Join-Path $script:session $Claim.claim.plan))) 'Plan deferral did not release the expected ordinary claim safely.'
}

function Assert-True([bool] $Condition, [string] $Message) {
	if (-not $Condition) { throw $Message }
}

foreach ($name in $environmentNames) { $originalEnvironment[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
	$primary = Join-Path $fixtureRoot 'primary'
	$script:session = Join-Path $fixtureRoot 'session'
	[IO.Directory]::CreateDirectory($primary) | Out-Null
	Invoke-Git $primary @('init','--initial-branch=main','.') | Out-Null
	Invoke-Git $primary @('config','user.email','next-plan-fixture@example.invalid') | Out-Null
	Invoke-Git $primary @('config','user.name','Next Plan Fixture') | Out-Null
	Invoke-Git $primary @('config','core.autocrlf','false') | Out-Null
	Set-Utf8File (Join-Path $primary '.gitignore') "Temp/`nTools/WorktreeCli/Platforms/VisualStudio2026/Output/`n"
	$plan = 'Documents/Plans/TestPlan.md'
	Set-Utf8File (Join-Path $primary 'Documents/Plans/TestPlan.md') "<!-- broken-engine-plan/v1 {`"createdUtc`":`"2024-01-01T00:00:00.000Z`",`"dependsOn`":[]} -->`n# Test plan`n`nImplement the fixture behavior.`n"
	$uniqueFilenamePlan = 'Documents/Plans/Unique/UniqueFilename.md'
	Set-Utf8File (Join-Path $primary $uniqueFilenamePlan) "<!-- broken-engine-plan/v1 {`"createdUtc`":`"2024-01-02T00:00:00.000Z`",`"dependsOn`":[]} -->`n# Unique filename plan`n"
	$duplicateFilenamePlans = @('Documents/Plans/Alpha/DuplicateFilename.md','Documents/Plans/Beta/DuplicateFilename.md')
	Set-Utf8File (Join-Path $primary $duplicateFilenamePlans[0]) "<!-- broken-engine-plan/v1 {`"createdUtc`":`"2024-01-03T00:00:00.000Z`",`"dependsOn`":[]} -->`n# First duplicate filename plan`n"
	Set-Utf8File (Join-Path $primary $duplicateFilenamePlans[1]) "<!-- broken-engine-plan/v1 {`"createdUtc`":`"2024-01-04T00:00:00.000Z`",`"dependsOn`":[]} -->`n# Second duplicate filename plan`n"
	Set-Utf8File (Join-Path $primary 'Documents/Plans/AGENTS.md') "# Directory guidance`n"
	Set-Utf8File (Join-Path $primary 'Documents/Features/Reference.md') "# Manual feature reference`n"
	Invoke-Git $primary @('add','--all') | Out-Null
	Invoke-Git $primary @('commit','-m','fixture baseline') | Out-Null
	$common = [IO.Path]::GetFullPath((Invoke-Git $primary @('rev-parse','--path-format=absolute','--git-common-dir')))
	# Isolate scheduler state and the session-exclusion ledger so this fixture never touches the real store.
	$env:LOCALAPPDATA = Join-Path $fixtureRoot 'localappdata'
	[IO.Directory]::CreateDirectory($env:LOCALAPPDATA) | Out-Null
	$primaryOutput = Join-Path $primary 'Tools/WorktreeCli/Platforms/VisualStudio2026/Output'
	$sessionOutput = Join-Path $script:session 'Tools/WorktreeCli/Platforms/VisualStudio2026/Output'
	[IO.Directory]::CreateDirectory($primaryOutput) | Out-Null
	$primaryExecutable = Join-Path $primaryOutput 'WorktreeCli.exe'
	Copy-Item -LiteralPath (Get-Item -LiteralPath $Executable -Force).FullName -Destination $primaryExecutable
	$realExecutable = Join-Path $fixtureRoot 'WorktreeCli-real.exe'
	Copy-Item -LiteralPath $primaryExecutable -Destination $realExecutable
	$exitFixtureExecutable = Join-Path $fixtureRoot 'WorktreeCli-sidecar-fixture.exe'
	New-WorktreeCliSidecarFixture $exitFixtureExecutable
	# The receipt constructor requires branch == "<client>/<worktreeId>" with a canonical lowercase
	# GUID worktreeId, so the session Git branch must be codex/<guid> for the receipt and the live
	# branch to agree.
	$worktreeId = [guid]::NewGuid().ToString()
	$sessionBranch = "codex/$worktreeId"
	$owner = [guid]::NewGuid().ToString()
	Invoke-Git $primary @('worktree','add','-b',$sessionBranch,$script:session,'HEAD') | Out-Null
	$baseline = Invoke-Git $primary @('rev-parse','HEAD')
	[IO.Directory]::CreateDirectory((Join-Path $script:session 'Temp')) | Out-Null
	[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($sessionOutput)) | Out-Null
	New-Item -ItemType Junction -Path $sessionOutput -Target $primaryOutput | Out-Null
	$fixtureExecutable = Join-Path $sessionOutput 'WorktreeCli.exe'
	$script:fixtureExecutable = $fixtureExecutable

	# The sidecars resolve session identity through Get-AgentWorktreeSessionProvenance, which now reads
	# it solely from the strictly validated in-worktree receipt (no environment fast path). The wrapper
	# registers no session ledger claim. Receipts are written into the session private Git directory
	# below.
	Import-Module (Join-Path $repositoryRoot '.agents/scripts/AgentWorktreeSession.psm1') -Force -DisableNameChecking
	Import-Module (Join-Path $repositoryRoot '.agents/scripts/AgentArtifactStore.psm1') -Force -DisableNameChecking
	$artifactRoot = Get-AgentArtifactRoot $script:session

	# The receipt is the sole trust anchor: with none present, provenance resolution throws and the
	# claim is a deterministic context blocker.
	$missingReceipt = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($missingReceipt.code -ceq 'claim.context-conflict') 'A missing session receipt was not a deterministic blocker.'

	# A schema-valid receipt whose branch disagrees with the live session Git branch fails the
	# Get-NextPlanContext branch cross-check as a deterministic context blocker.
	$otherWorktreeId = [guid]::NewGuid().ToString()
	$mismatchedReceipt = New-AgentWorktreeSessionReceipt -Client 'codex' -PrimaryCheckout $primary -GitCommonDirectory $common -Worktree $script:session -WorktreeId $otherWorktreeId -Branch "codex/$otherWorktreeId" -TargetBranch 'main' -Baseline $baseline -SessionOwner $owner
	Write-AgentWorktreeSessionReceipt -Worktree $script:session -Receipt $mismatchedReceipt | Out-Null
	$branchMismatch = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($branchMismatch.code -ceq 'claim.context-conflict') 'A receipt branch disagreeing with the live Git branch was not a deterministic blocker.'
	Remove-Item -LiteralPath @((Get-AgentWorktreeReceiptPath $script:session),(Get-AgentWorktreeReceiptIntegrityPath $script:session)) -Force

	# The authoritative receipt binds the live session branch; every positive flow below resolves
	# through it.
	$receipt = New-AgentWorktreeSessionReceipt -Client 'codex' -PrimaryCheckout $primary -GitCommonDirectory $common -Worktree $script:session -WorktreeId $worktreeId -Branch $sessionBranch -TargetBranch 'main' -Baseline $baseline -SessionOwner $owner
	Write-AgentWorktreeSessionReceipt -Worktree $script:session -Receipt $receipt | Out-Null

	$wrongOutput = Join-Path $fixtureRoot 'wrong-output'
	[IO.Directory]::CreateDirectory($wrongOutput) | Out-Null
	Copy-Item -LiteralPath $primaryExecutable -Destination (Join-Path $wrongOutput 'WorktreeCli.exe')
	Remove-Item -LiteralPath $sessionOutput -Force
	New-Item -ItemType Junction -Path $sessionOutput -Target $wrongOutput | Out-Null
	$wrongTarget = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($wrongTarget.code -ceq 'claim.context-conflict') 'A wrong WorktreeCli Output target was not a deterministic blocker.'
	Remove-Item -LiteralPath $sessionOutput -Force
	New-Item -ItemType Junction -Path $sessionOutput -Target $primaryOutput | Out-Null
	Copy-Item -LiteralPath $exitFixtureExecutable -Destination $primaryExecutable -Force
	$env:BROKEN_ENGINE_WORKTREECLI_SIDECAR_FIXTURE = 'claim-1'
	$claimExitOne = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 1
	Assert-True ($claimExitOne.status -ceq 'error' -and $claimExitOne.code -ceq 'plan.validation-failed') 'Invoke-NextPlanClaim did not preserve WorktreeCli exit 1 as status error/exit 1.'
	$env:BROKEN_ENGINE_WORKTREECLI_SIDECAR_FIXTURE = 'claim-2'
	$claimExitTwo = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($claimExitTwo.status -ceq 'blocked' -and $claimExitTwo.code -ceq 'plan.validation-failed') 'Invoke-NextPlanClaim did not preserve WorktreeCli exit 2 as status blocked/exit 2.'
	$env:BROKEN_ENGINE_WORKTREECLI_SIDECAR_FIXTURE = $null
	Copy-Item -LiteralPath $realExecutable -Destination $primaryExecutable -Force

	# Filename-only inputs resolve from validation's executable-plan projection. Every successful
	# independent claim is receipt-unclaimed before the next selection assertion.
	$bareClaim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @() 0
	Assert-True ($bareClaim.status -ceq 'pass' -and $bareClaim.claim.plan -ceq $plan) 'Bare invocation did not preserve the oldest eligible Plan selection.'
	Release-Claim $bareClaim
	$uniqueFilenameClaim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan','UniqueFilename.md') 0
	Assert-True ($uniqueFilenameClaim.status -ceq 'pass' -and $uniqueFilenameClaim.claim.plan -ceq $uniqueFilenamePlan) 'A unique filename-only input did not claim its canonical Plan path.'
	Release-Claim $uniqueFilenameClaim
	$missingFilename = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan','MissingFilename.md') 2
	Assert-True ($missingFilename.status -ceq 'blocked' -and $missingFilename.code -ceq 'plan-name-not-found') 'A nonexistent filename-only input did not return the required deterministic blocker.'
	$wrongCaseFilename = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan','uniquefilename.md') 2
	Assert-True ($wrongCaseFilename.status -ceq 'blocked' -and $wrongCaseFilename.code -ceq 'plan-name-not-found') 'A wrong-case filename-only input did not return the required deterministic blocker.'
	$ambiguousFilename = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan','DuplicateFilename.md') 2
	Assert-True ($ambiguousFilename.status -ceq 'blocked' -and $ambiguousFilename.code -ceq 'plan-name-ambiguous') 'A duplicate filename-only input did not return the required deterministic blocker.'
	Assert-True ((@($ambiguousFilename.candidates) -join '|') -ceq ($duplicateFilenamePlans -join '|')) 'Ambiguous filename candidates were not returned in exact ordinal canonical-path order.'
	$canonicalDuplicateClaim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$duplicateFilenamePlans[0]) 0
	Assert-True ($canonicalDuplicateClaim.status -ceq 'pass' -and $canonicalDuplicateClaim.claim.plan -ceq $duplicateFilenamePlans[0]) 'A canonical duplicate filename Plan path did not claim that exact Plan.'
	Release-Claim $canonicalDuplicateClaim

	# Primary advancing before the claim is tolerated: the rebuilt WorktreeCli selects candidates
	# from the session tree and requires only that session HEAD is a primary-tip ancestor. The
	# session is not rebased and the receipt baseline stays at the old baseline; the successful
	# claim below proceeds against that unchanged baseline (Get-NextPlanContext succeeds throughout).
	Set-Utf8File (Join-Path $primary 'Documents/PrimaryAdvance.txt') "advanced`n"
	Invoke-Git $primary @('add','--all') | Out-Null
	Invoke-Git $primary @('commit','-m','fixture pre-claim primary advance') | Out-Null

	# Recovery must leave the session clean. Dirty state is rejected before scheduler access.
	Set-Utf8File (Join-Path $script:session 'Dirty.txt') "uncommitted claim blocker`n"
	$dirtyClaim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($dirtyClaim.code -ceq 'claim.context-conflict') 'A dirty session worktree was not a deterministic claim blocker.'
	Remove-Item -LiteralPath (Join-Path $script:session 'Dirty.txt') -Force

	$claim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 0
	Assert-True ($claim.status -ceq 'pass' -and $claim.claim.plan -ceq $plan) 'Claim result did not bind the selected plan.'
	Assert-True ($claim.PSObject.Properties.Name -cnotcontains 'receipt') 'Claim result exposed receipt identity.'
	Assert-True ((Invoke-Git $script:session @('rev-parse','HEAD')) -ceq $baseline) 'Claim rebased the session; it must proceed at the old baseline after the primary advance.'
	$claimReceiptPath = Join-Path $script:session 'Temp\next-plan-claim.json'
	$claimReceipt = [IO.File]::ReadAllText($claimReceiptPath) | ConvertFrom-Json -Depth 20 -ErrorAction Stop
	Assert-True ($claimReceipt.branch -ceq $sessionBranch) 'Claim receipt did not bind the live session worktree branch.'
	$initialReceiptBytes = [IO.File]::ReadAllBytes($claimReceiptPath)
	$initialReceiptSha256 = (Get-FileHash -LiteralPath $claimReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
	$reusedClaim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 0
	Assert-True ($reusedClaim.status -ceq 'pass' -and $reusedClaim.code -ceq 'reused' -and $reusedClaim.claim.plan -ceq $plan -and $reusedClaim.claim.state -ceq 'claimed' -and $reusedClaim.PSObject.Properties.Name -cnotcontains 'receipt') 'Idempotent owned receipt reuse did not retain the ordinary claim without exposing receipt identity.'
	Assert-True ((Get-FileHash -LiteralPath $claimReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $initialReceiptSha256 -and [Convert]::ToHexString([IO.File]::ReadAllBytes($claimReceiptPath)) -ceq [Convert]::ToHexString($initialReceiptBytes)) 'Idempotent owned receipt reuse changed the deterministic receipt.'
	$originalClaimedPlanBytes = [IO.File]::ReadAllBytes((Join-Path $script:session $plan))
	Set-Utf8File (Join-Path $script:session $plan) "# Changed after claim`n"
	Invoke-Git $script:session @('add','--',$plan) | Out-Null
	Invoke-Git $script:session @('commit','-m','fixture claimed plan mutation') | Out-Null
	$mismatchedClaim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($mismatchedClaim.code -ceq 'claim.plan-digest-mismatch') 'Idempotent claim did not block changed claimed Plan bytes.'
	Invoke-Git $script:session @('reset','--hard',$baseline) | Out-Null
	Assert-True (((Get-FileHash -LiteralPath (Join-Path $script:session $plan) -Algorithm SHA256).Hash.ToLowerInvariant()) -ceq $claimReceipt.planSha256) 'Fixture did not restore the claimed Plan bytes after the idempotent claim check.'
	# A scheduler-healed stale claim makes claim-status receipt-invalid.  The sidecar must
	# ask the scheduler to unclaim the still-valid receipt, receive already-absent, retire
	# its local copy, and claim anew rather than treating the receipt as a permanent blocker.
	$staleReceiptSha256 = (Get-FileHash -LiteralPath $claimReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
	$staleRelease = ConvertFrom-SingleJson (Invoke-Process $fixtureExecutable @('plan','unclaim','--worktree',$script:session,'--claim-receipt',$claimReceiptPath,'--claim-receipt-sha256',$staleReceiptSha256) $script:session) 0 'stale claim release'
	Assert-True ($staleRelease.code -ceq 'released') 'Fixture could not create an absent scheduler claim behind the deterministic receipt.'
	$reclaimed = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 0
	Assert-True ($reclaimed.status -ceq 'pass' -and $reclaimed.code -ceq 'ok' -and $reclaimed.claim.plan -ceq $plan) 'receipt-invalid -> unclaim already-absent did not retire and freshly claim.'
	$claimReceipt = [IO.File]::ReadAllText($claimReceiptPath) | ConvertFrom-Json -Depth 20 -ErrorAction Stop
	Assert-True (((Get-FileHash -LiteralPath $claimReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()) -cne $staleReceiptSha256) 'Stale deterministic receipt was not replaced by the fresh claim.'
	$claim = $reclaimed
	Copy-Item -LiteralPath $exitFixtureExecutable -Destination $primaryExecutable -Force
	foreach ($case in @(@('claim-status-1',1,'error','completion.claim-status-failed'),@('claim-status-2',2,'blocked','completion.claim-status-failed'),@('prepare-1',1,'error','completion.prepare-failed'),@('prepare-2',2,'blocked','completion.prepare-failed'))) {
		$env:BROKEN_ENGINE_WORKTREECLI_SIDECAR_FIXTURE = $case[0]
		$outcome = Invoke-Sidecar 'Complete-NextPlan.ps1' @() $case[1]
		Assert-True ($outcome.status -ceq $case[2] -and $outcome.code -ceq $case[3]) "$($case[0]) did not preserve WorktreeCli status/exit contract."
	}
	$env:BROKEN_ENGINE_WORKTREECLI_SIDECAR_FIXTURE = $null
	Copy-Item -LiteralPath $realExecutable -Destination $primaryExecutable -Force

	# Primary advancing mid-workflow is tolerated: the session keeps working at its baseline
	# without rebasing, and completion below succeeds.
	Set-Utf8File (Join-Path $primary 'Documents/PrimaryAdvance.txt') "advanced again`n"
	Invoke-Git $primary @('add','--all') | Out-Null
	Invoke-Git $primary @('commit','-m','fixture mid-workflow primary advance') | Out-Null

	# Terminal preparation deletes the Plan bytes and retains its receipt-bound claim until landing.
	Set-Utf8File (Join-Path $script:session 'Source/Implemented.txt') "implemented`n"
	$originalPlanBytes = [IO.File]::ReadAllBytes((Join-Path $script:session $plan))
	Set-Utf8File (Join-Path $script:session $plan) "# Changed after claim`n"
	$mismatchedCompletion = Invoke-Sidecar 'Complete-NextPlan.ps1' @() 2
	Assert-True ($mismatchedCompletion.code -ceq 'completion.prepare-failed') 'Changed plan bytes were not a deterministic completion blocker.'
	[IO.File]::WriteAllBytes((Join-Path $script:session $plan), $originalPlanBytes)
	Assert-True (((Get-FileHash -LiteralPath (Join-Path $script:session $plan) -Algorithm SHA256).Hash.ToLowerInvariant()) -ceq $claimReceipt.planSha256) 'Fixture did not restore the claimed Plan bytes exactly.'
	$completion = Invoke-Sidecar 'Complete-NextPlan.ps1' @() 0
	Assert-True (-not $completion.workflowTerminal -and $completion.nextAction -ceq 'finalize-changes') 'Completion incorrectly became a terminal workflow result.'
	Assert-True (-not (Test-Path -LiteralPath (Join-Path $script:session $plan))) 'Completion retained the selected plan file.'
	$terminalProofPath = Join-Path $script:session 'Temp\next-plan-terminal-result.json'
	Assert-True (Test-Path -LiteralPath $terminalProofPath -PathType Leaf) 'Completion did not persist the receipt-bound terminal proof.'
	if (Test-Path -LiteralPath $terminalProofPath -PathType Leaf) {
		$terminalProof = [IO.File]::ReadAllText($terminalProofPath, [Text.UTF8Encoding]::new($false, $true)) | ConvertFrom-Json -Depth 32 -ErrorAction Stop
		Assert-True ($terminalProof.schemaVersion -ceq 'broken-engine-next-plan-terminal-proof/v1' -and $terminalProof.disposition -ceq 'completed' -and $terminalProof.claimState -ceq 'awaiting-landing' -and $terminalProof.changedPaths -contains $plan) 'Completion terminal proof did not preserve original disposition and changed paths.'
	}
	$oppositeDisposition = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-Reject') 2
	Assert-True ($oppositeDisposition.status -ceq 'blocked' -and $oppositeDisposition.code -in @('completion.disposition-mismatch','completion.prepare-failed')) 'Opposite-disposition terminal recovery was not blocked.'
	$claimReceiptSha256 = (Get-FileHash -LiteralPath $claimReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
	$claimStatus = ConvertFrom-SingleJson (Invoke-Process $fixtureExecutable @('plan','claim-status','--worktree',$script:session,'--claim-receipt',$claimReceiptPath,'--claim-receipt-sha256',$claimReceiptSha256) $script:session) 0 'post-completion claim status'
	Assert-True ($claimStatus.claimState -ceq 'awaiting-landing') 'Completion did not retain awaiting-landing scheduler state.'
	$missingPlan = Invoke-Sidecar 'Complete-NextPlan.ps1' @() 0
	Assert-True ($missingPlan.status -ceq 'pass') 'Terminal preparation recovery did not remain idempotent.'
	if (Test-Path -LiteralPath $terminalProofPath -PathType Leaf) {
		$retryProof = [IO.File]::ReadAllText($terminalProofPath, [Text.UTF8Encoding]::new($false, $true)) | ConvertFrom-Json -Depth 32 -ErrorAction Stop
		Assert-True ($retryProof.disposition -ceq 'completed' -and $retryProof.changedPaths -contains $plan) 'Terminal retry overwrote the original terminal proof with an empty recovery result.'
	}

	[pscustomobject]@{
		schemaVersion = 'broken-engine-next-plan-sidecar-fixtures/v1'
		status = 'pass'
		cases = @('missing receipt rejection','receipt branch mismatch rejection','wrong Output rejection','claim exit 1 error mapping','claim exit 2 blocked mapping','bare oldest eligible selection','unique filename-only claim','missing filename-only blocker','wrong-case filename-only blocker','ambiguous filename-only blocker','canonical duplicate filename claim','pre-claim primary-advance tolerance','dirty-tree claim rejection','wrapper-derived plan claim','idempotent owned receipt reuse','idempotent claim plan-digest mismatch','stale receipt retirement and fresh claim','claim-status exit mapping','prepare exit mapping','mid-workflow primary-advance tolerance','completion plan-digest mismatch','opposite-disposition recovery blocker','terminal preparation with retained receipt','idempotent terminal recovery')
	} | ConvertTo-Json -Depth 5
}
finally {
	foreach ($name in $environmentNames) {
		$value = $originalEnvironment[$name]
		if ($null -eq $value) { [Environment]::SetEnvironmentVariable($name, $null) }
		else { [Environment]::SetEnvironmentVariable($name, [string]$value) }
	}
	if ($null -ne $artifactRoot -and (Test-Path -LiteralPath $artifactRoot)) { Remove-Item -LiteralPath $artifactRoot -Recurse -Force }
	if (Test-Path -LiteralPath $fixtureRoot) { Remove-Item -LiteralPath $fixtureRoot -Recurse -Force }
	if ($null -eq $originalLocalAppData) { [Environment]::SetEnvironmentVariable('LOCALAPPDATA', $null) }
	else { $env:LOCALAPPDATA = $originalLocalAppData }
}
