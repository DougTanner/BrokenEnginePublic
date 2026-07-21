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
	'BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER'
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

function Get-OrderText([string] $Queue, [string] $Plan) {
	$relative = if ([string]::IsNullOrEmpty($Plan)) { '' } else { $Plan.Substring("Documents/$Queue/".Length) }
	return @"
# $Queue plan order

| Plan | Tier | Effort | Impact | Risks | Score | Depends On | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
$(if ([string]::IsNullOrEmpty($Plan)) { '' } else { "| [$relative]($relative) | Small | 1 | 2 | 1 | 0 | - | fixture |" })

### Reference / Index Documents

| Document | Purpose |
| --- | --- |
| [Reference.md](Reference.md) | Fixture reference |

"@.Replace("`r`n", "`n")
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
	Set-Utf8File (Join-Path $primary 'Documents/Plans/Order.md') (Get-OrderText 'Plans' $plan)
	Set-Utf8File (Join-Path $primary 'Documents/Features/Order.md') (Get-OrderText 'Features' '')
	Set-Utf8File (Join-Path $primary 'Documents/Plans/TestPlan.md') "# Test plan`n`nImplement the fixture behavior.`n"
	Set-Utf8File (Join-Path $primary 'Documents/Plans/Reference.md') "# Plans reference`n"
	Set-Utf8File (Join-Path $primary 'Documents/Features/Reference.md') "# Features reference`n"
	Invoke-Git $primary @('add','--all') | Out-Null
	Invoke-Git $primary @('commit','-m','fixture baseline') | Out-Null
	$common = [IO.Path]::GetFullPath((Invoke-Git $primary @('rev-parse','--path-format=absolute','--git-common-dir')))
	# The plan queue is machine-local under %LOCALAPPDATA%\BrokenEngineLocks; isolate it (and the
	# session-exclusion ledger) in a scratch directory so this fixture never touches the real store.
	$env:LOCALAPPDATA = Join-Path $fixtureRoot 'localappdata'
	[IO.Directory]::CreateDirectory($env:LOCALAPPDATA) | Out-Null
	$primaryOutput = Join-Path $primary 'Tools/WorktreeCli/Platforms/VisualStudio2026/Output'
	$sessionOutput = Join-Path $script:session 'Tools/WorktreeCli/Platforms/VisualStudio2026/Output'
	[IO.Directory]::CreateDirectory($primaryOutput) | Out-Null
	$primaryExecutable = Join-Path $primaryOutput 'WorktreeCli.exe'
	Copy-Item -LiteralPath (Get-Item -LiteralPath $Executable -Force).FullName -Destination $primaryExecutable
	# Seed the machine-local store from the tracked Order.md files, then remove the in-tree queue tables
	# (mirrors the one-time migration) so the session models a post-migration checkout with no Order.md.
	$init = Invoke-Process $primaryExecutable @('plan','order','init','--repo',$common,'--worktree',$primary) $primary
	Assert-True ($init.ExitCode -eq 0) "plan order init failed: $($init.Stdout)$($init.Stderr)"
	Invoke-Git $primary @('rm','--','Documents/Plans/Order.md','Documents/Features/Order.md') | Out-Null
	Invoke-Git $primary @('commit','-m','remove in-tree queue tables (machine-local migration)') | Out-Null
	Invoke-Git $primary @('worktree','add','-b','codex/fixture-session',$script:session,'HEAD') | Out-Null
	$baseline = Invoke-Git $primary @('rev-parse','HEAD')
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
	$missingEnvironment = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Queue','plans','-Plan',$plan) 2
	Assert-True ($missingEnvironment.code -ceq 'claim.context-conflict') 'A missing wrapper environment value was not a deterministic blocker.'
	$env:BROKEN_ENGINE_TARGET_BRANCH = 'main'
	$env:BROKEN_ENGINE_WORKTREE_PATH = Join-Path $fixtureRoot 'missing-worktree'
	$invalidWorktree = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Queue','plans','-Plan',$plan) 2
	Assert-True ($invalidWorktree.code -ceq 'claim.context-conflict') 'An invalid wrapper worktree was not a deterministic blocker.'
	$env:BROKEN_ENGINE_WORKTREE_PATH = $script:session

	$ledgerPath = $sessionRegistration.Identity.LedgerPath
	$liveLedgerText = [IO.File]::ReadAllText($ledgerPath)
	$staleLedger = $liveLedgerText | ConvertFrom-Json -Depth 20
	$staleLedger.sessions[0].processStartUtc = [DateTime]::UtcNow.AddDays(-1).ToString('O')
	Set-Utf8File $ledgerPath ($staleLedger | ConvertTo-Json -Depth 20 -Compress)
	$staleContext = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Queue','plans','-Plan',$plan) 2
	Assert-True ($staleContext.code -ceq 'claim.context-conflict') 'A stale exclusion-ledger session was not a deterministic blocker.'
	Set-Utf8File $ledgerPath $liveLedgerText

	$wrongOutput = Join-Path $fixtureRoot 'wrong-output'
	[IO.Directory]::CreateDirectory($wrongOutput) | Out-Null
	Copy-Item -LiteralPath $primaryExecutable -Destination (Join-Path $wrongOutput 'WorktreeCli.exe')
	Remove-Item -LiteralPath $sessionOutput -Force
	New-Item -ItemType Junction -Path $sessionOutput -Target $wrongOutput | Out-Null
	$wrongTarget = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Queue','plans','-Plan',$plan) 2
	Assert-True ($wrongTarget.code -ceq 'claim.context-conflict') 'A wrong WorktreeCli Output target was not a deterministic blocker.'
	Remove-Item -LiteralPath $sessionOutput -Force
	New-Item -ItemType Junction -Path $sessionOutput -Target $primaryOutput | Out-Null

	# Primary advancing before the claim blocks the strict claim gate, then recovers in place:
	# fast-forward the session and re-baseline BROKEN_ENGINE_BASELINE, then claim normally.
	Set-Utf8File (Join-Path $primary 'Documents/PrimaryAdvance.txt') "advanced`n"
	Invoke-Git $primary @('add','--all') | Out-Null
	Invoke-Git $primary @('commit','-m','fixture pre-claim primary advance') | Out-Null
	$advancedTip = Invoke-Git $primary @('rev-parse','HEAD')
	$advanceBlocked = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Queue','plans','-Plan',$plan) 2
	Assert-True ($advanceBlocked.code -ceq 'claim.context-conflict') 'A pre-claim primary advance was not a deterministic claim blocker.'
	Invoke-Git $script:session @('rebase',$advancedTip) | Out-Null
	$env:BROKEN_ENGINE_BASELINE = $advancedTip

	# Recovery must leave the session clean. Dirty state is rejected before queue access.
	Set-Utf8File (Join-Path $script:session 'Dirty.txt') "uncommitted claim blocker`n"
	$dirtyClaim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Queue','plans','-Plan',$plan) 2
	Assert-True ($dirtyClaim.code -ceq 'claim.context-conflict') 'A dirty session worktree was not a deterministic claim blocker.'
	Remove-Item -LiteralPath (Join-Path $script:session 'Dirty.txt') -Force

	# Orphan plans are queue-authoring errors even though WorktreeCli reports them as notices.
	$orphanPath = Join-Path $primary 'Documents/Plans/Orphan.md'
	Set-Utf8File $orphanPath "# Orphan`n"
	$orphanClaim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Queue','plans','-Plan',$plan) 2
	Assert-True ($orphanClaim.code -ceq 'queue.orphan-plan') 'An orphan plan notice did not stop the next-plan workflow.'
	Remove-Item -LiteralPath $orphanPath -Force

	$claim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Plan',$plan) 0
	Assert-True ($claim.status -ceq 'pass' -and $claim.claim.plan -ceq $plan) 'Claim result did not bind the selected plan.'
	Assert-True ($claim.claim.primaryCommit -ceq $advancedTip) 'Recovered claim did not bind the advanced primary tip.'

	# Primary advancing mid-workflow is tolerated: the session keeps working at its baseline
	# without rebasing, and completion below succeeds.
	Set-Utf8File (Join-Path $primary 'Documents/PrimaryAdvance.txt') "advanced again`n"
	Invoke-Git $primary @('add','--all') | Out-Null
	Invoke-Git $primary @('commit','-m','fixture mid-workflow primary advance') | Out-Null

	# Phase 1 completion is git-rm-only: it stages the plan-file deletion but leaves the queue row and
	# owner-held claim in place (the row is removed post-landing by Invoke-FinalizeLanding.ps1).
	Set-Utf8File (Join-Path $script:session 'Source/Implemented.txt') "implemented`n"
	$originalPlanBytes = [IO.File]::ReadAllBytes((Join-Path $script:session $plan))
	Set-Utf8File (Join-Path $script:session $plan) "# Changed after claim`n"
	$mismatchedCompletion = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-Plan',$plan,'-PlanSha256',$claim.claim.planSha256) 2
	Assert-True ($mismatchedCompletion.code -ceq 'completion.plan-byte-mismatch') 'Changed plan bytes were not a deterministic completion blocker.'
	[IO.File]::WriteAllBytes((Join-Path $script:session $plan), $originalPlanBytes)
	$completion = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-Plan',$plan,'-PlanSha256',$claim.claim.planSha256) 0
	Assert-True (-not $completion.workflowTerminal -and $completion.nextAction -ceq 'finalize-changes') 'Completion incorrectly became a terminal workflow result.'
	Assert-True (-not (Test-Path -LiteralPath (Join-Path $script:session $plan))) 'Completion retained the selected plan file.'
	$stagedDeletion = Invoke-Git $script:session @('status','--porcelain','--',$plan)
	Assert-True ($stagedDeletion.StartsWith('D', [StringComparison]::Ordinal)) 'Completion did not stage the plan-file deletion.'
	$rowStatus = ConvertFrom-SingleJson (Invoke-Process $fixtureExecutable @('plan','row','status','--repo',$common,'--order','Documents/Plans/Order.md','--plan','TestPlan.md','--owner',$owner) $script:session) 0 'post-completion row status'
	Assert-True ($rowStatus.ownedByRequester) 'Completion did not retain the owner-held row claim.'
	$postCompleteValidation = ConvertFrom-SingleJson (Invoke-Process $fixtureExecutable @('plan','order','validate','--repo',$common,'--worktree',$primary) $primary) 0 'post-completion validate'
	Assert-True (@($postCompleteValidation.rows | Where-Object plan -CEQ $plan).Count -eq 1) 'Completion removed the queue row; row removal must wait for landing.'
	$missingPlan = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-Plan',$plan,'-PlanSha256',$claim.claim.planSha256) 2
	Assert-True ($missingPlan.code -ceq 'completion.plan-missing') 'A completed (missing) plan was not a deterministic completion blocker.'

	[pscustomobject]@{
		schemaVersion = 'broken-engine-next-plan-sidecar-fixtures/v1'
		status = 'pass'
		cases = @('missing environment rejection','invalid worktree rejection','live exclusion-ledger binding','wrong Output rejection','machine-local store seed via init','pre-claim primary-advance blocker','clean in-place recovery','dirty-tree claim rejection','orphan-plan stop','default Plans wrapper-derived claim','mid-workflow primary-advance tolerance','completion plan-digest mismatch','git-rm-only completion with retained row','missing-plan rejection')
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
