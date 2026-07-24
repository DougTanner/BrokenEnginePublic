


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
$sessionRegistration = $null
$owner = $null
$originalEnvironment = @{}
# LOCALAPPDATA is restored separately (after Unregister-WorktreeCliSession), so the disposable
# session ledger in the scratch store is still reachable at cleanup time.
$originalLocalAppData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA')
$environmentNames = @(
	'BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE','BROKEN_ENGINE_WORKTREE_PATH','BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE',
	'BROKEN_ENGINE_PRIMARY_CHECKOUT','BROKEN_ENGINE_SESSION_BRANCH','BROKEN_ENGINE_TARGET_BRANCH','BROKEN_ENGINE_BASELINE',
	'BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER','BROKEN_ENGINE_WORKTREECLI_SIDECAR_FIXTURE'
)

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
	Set-Utf8File (Join-Path $primary 'Documents/Plans/Reference.md') "# Plans reference`n"
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
	Invoke-Git $primary @('worktree','add','-b','codex/fixture-session',$script:session,'HEAD') | Out-Null
	$baseline = Invoke-Git $primary @('rev-parse','HEAD')
	[IO.Directory]::CreateDirectory((Join-Path $script:session 'Temp')) | Out-Null
	[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($sessionOutput)) | Out-Null
	New-Item -ItemType Junction -Path $sessionOutput -Target $primaryOutput | Out-Null
	$fixtureExecutable = Join-Path $sessionOutput 'WorktreeCli.exe'

	$owner = [guid]::NewGuid().ToString()
	Import-Module (Join-Path $repositoryRoot '.agents/scripts/WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking
	$sessionRegistration = Register-WorktreeCliSession -RepositoryRoot $script:session -Owner $owner -Label 'next-plan disposable fixture' -Worktree $script:session -LegacySessionsClosed
	$env:BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE = 'session'
	$env:BROKEN_ENGINE_WORKTREE_PATH = $script:session
	$env:BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE = $script:session
	$env:BROKEN_ENGINE_PRIMARY_CHECKOUT = $primary
	$env:BROKEN_ENGINE_SESSION_BRANCH = 'codex/fixture-session'
	$env:BROKEN_ENGINE_TARGET_BRANCH = 'main'
	$env:BROKEN_ENGINE_BASELINE = $baseline
	$env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER = $owner
	Import-Module (Join-Path $repositoryRoot '.agents/scripts/AgentArtifactStore.psm1') -Force -DisableNameChecking
	$artifactRoot = Get-AgentArtifactRoot $script:session

	$env:BROKEN_ENGINE_TARGET_BRANCH = $null
	$missingEnvironment = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($missingEnvironment.code -ceq 'claim.context-conflict') 'A missing wrapper environment value was not a deterministic blocker.'
	$env:BROKEN_ENGINE_TARGET_BRANCH = 'main'
	$env:BROKEN_ENGINE_WORKTREE_PATH = Join-Path $fixtureRoot 'missing-worktree'
	$invalidWorktree = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($invalidWorktree.code -ceq 'claim.context-conflict') 'An invalid wrapper worktree was not a deterministic blocker.'
	$env:BROKEN_ENGINE_WORKTREE_PATH = $script:session

	$ledgerPath = $sessionRegistration.Identity.LedgerPath
	$liveLedgerText = [IO.File]::ReadAllText($ledgerPath)
	$staleLedger = $liveLedgerText | ConvertFrom-Json -Depth 20
	$staleLedger.sessions[0].processStartUtc = [DateTime]::UtcNow.AddDays(-1).ToString('O')
	Set-Utf8File $ledgerPath ($staleLedger | ConvertTo-Json -Depth 20 -Compress)
	$staleContext = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($staleContext.code -ceq 'claim.context-conflict') 'A stale exclusion-ledger session was not a deterministic blocker.'
	Set-Utf8File $ledgerPath $liveLedgerText

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

	# Primary advancing before the claim blocks the strict claim gate, then recovers in place:
	# fast-forward the session and re-baseline BROKEN_ENGINE_BASELINE, then claim normally.
	Set-Utf8File (Join-Path $primary 'Documents/PrimaryAdvance.txt') "advanced`n"
	Invoke-Git $primary @('add','--all') | Out-Null
	Invoke-Git $primary @('commit','-m','fixture pre-claim primary advance') | Out-Null
	$advancedTip = Invoke-Git $primary @('rev-parse','HEAD')
	$advanceBlocked = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($advanceBlocked.code -ceq 'claim.context-conflict') 'A pre-claim primary advance was not a deterministic claim blocker.'
	Invoke-Git $script:session @('rebase',$advancedTip) | Out-Null
	$env:BROKEN_ENGINE_BASELINE = $advancedTip

	# Recovery must leave the session clean. Dirty state is rejected before scheduler access.
	Set-Utf8File (Join-Path $script:session 'Dirty.txt') "uncommitted claim blocker`n"
	$dirtyClaim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 2
	Assert-True ($dirtyClaim.code -ceq 'claim.context-conflict') 'A dirty session worktree was not a deterministic claim blocker.'
	Remove-Item -LiteralPath (Join-Path $script:session 'Dirty.txt') -Force

	$claim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 0
	Assert-True ($claim.status -ceq 'pass' -and $claim.claim.plan -ceq $plan) 'Claim result did not bind the selected plan.'
	Assert-True ($claim.receipt.sha256 -cmatch '^[0-9a-f]{64}$') 'Recovered claim did not return durable receipt identity.'
	$claimReceipt = [IO.File]::ReadAllText([string]$claim.receipt.path) | ConvertFrom-Json -Depth 20 -ErrorAction Stop
	Assert-True ($claimReceipt.branch -ceq 'codex/fixture-session') 'Claim receipt did not bind the live session worktree branch.'
	Copy-Item -LiteralPath $exitFixtureExecutable -Destination $primaryExecutable -Force
	foreach ($case in @(@('claim-status-1',1,'error','completion.claim-status-failed'),@('claim-status-2',2,'blocked','completion.claim-status-failed'),@('prepare-1',1,'error','completion.prepare-failed'),@('prepare-2',2,'blocked','completion.prepare-failed'))) {
		$env:BROKEN_ENGINE_WORKTREECLI_SIDECAR_FIXTURE = $case[0]
		$outcome = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-ClaimReceipt',$claim.receipt.path,'-ClaimReceiptSha256',$claim.receipt.sha256) $case[1]
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
	$mismatchedCompletion = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-ClaimReceipt',$claim.receipt.path,'-ClaimReceiptSha256',$claim.receipt.sha256) 2
	Assert-True ($mismatchedCompletion.code -ceq 'completion.prepare-failed') 'Changed plan bytes were not a deterministic completion blocker.'
	[IO.File]::WriteAllBytes((Join-Path $script:session $plan), $originalPlanBytes)
	$completion = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-ClaimReceipt',$claim.receipt.path,'-ClaimReceiptSha256',$claim.receipt.sha256) 0
	Assert-True (-not $completion.workflowTerminal -and $completion.nextAction -ceq 'finalize-changes') 'Completion incorrectly became a terminal workflow result.'
	Assert-True (-not (Test-Path -LiteralPath (Join-Path $script:session $plan))) 'Completion retained the selected plan file.'
	$oppositeDisposition = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-ClaimReceipt',$claim.receipt.path,'-ClaimReceiptSha256',$claim.receipt.sha256,'-Reject') 2
	Assert-True ($oppositeDisposition.status -ceq 'blocked' -and $oppositeDisposition.code -ceq 'completion.disposition-mismatch') 'Opposite-disposition terminal recovery was not blocked.'
	$claimStatus = ConvertFrom-SingleJson (Invoke-Process $fixtureExecutable @('plan','claim-status','--worktree',$script:session,'--claim-receipt',$claim.receipt.path,'--claim-receipt-sha256',$claim.receipt.sha256) $script:session) 0 'post-completion claim status'
	Assert-True ($claimStatus.claimState -ceq 'awaiting-landing') 'Completion did not retain awaiting-landing scheduler state.'
	$missingPlan = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-ClaimReceipt',$claim.receipt.path,'-ClaimReceiptSha256',$claim.receipt.sha256) 0
	Assert-True ($missingPlan.status -ceq 'pass') 'Terminal preparation recovery did not remain idempotent.'

	[pscustomobject]@{
		schemaVersion = 'broken-engine-next-plan-sidecar-fixtures/v1'
		status = 'pass'
		cases = @('missing environment rejection','invalid worktree rejection','live exclusion-ledger binding','wrong Output rejection','claim exit 1 error mapping','claim exit 2 blocked mapping','metadata-only scheduler initialization','pre-claim primary-advance blocker','clean in-place recovery','dirty-tree claim rejection','manual-plan omission','default Plans wrapper-derived claim','claim-status exit mapping','prepare exit mapping','mid-workflow primary-advance tolerance','completion plan-digest mismatch','opposite-disposition recovery blocker','terminal preparation with retained receipt','idempotent terminal recovery')
	} | ConvertTo-Json -Depth 5
}
finally {
	foreach ($name in $environmentNames) {
		$value = $originalEnvironment[$name]
		if ($null -eq $value) { [Environment]::SetEnvironmentVariable($name, $null) }
		else { [Environment]::SetEnvironmentVariable($name, [string]$value) }
	}
	if ($null -ne $sessionRegistration) {
		try { Unregister-WorktreeCliSession -RepositoryRoot $script:session -Owner $owner }
		catch { Write-Warning "Could not unregister disposable WorktreeCli session: $($_.Exception.Message)" }
	}
	if ($null -ne $artifactRoot -and (Test-Path -LiteralPath $artifactRoot)) { Remove-Item -LiteralPath $artifactRoot -Recurse -Force }
	if (Test-Path -LiteralPath $fixtureRoot) { Remove-Item -LiteralPath $fixtureRoot -Recurse -Force }
	if ($null -eq $originalLocalAppData) { [Environment]::SetEnvironmentVariable('LOCALAPPDATA', $null) }
	else { $env:LOCALAPPDATA = $originalLocalAppData }
}
