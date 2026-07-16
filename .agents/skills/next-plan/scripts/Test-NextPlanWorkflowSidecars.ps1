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
	Invoke-Git $primary @('worktree','add','-b','codex/fixture-session',$script:session,'HEAD') | Out-Null
	$baseline = Invoke-Git $primary @('rev-parse','HEAD')
	$common = [IO.Path]::GetFullPath((Invoke-Git $primary @('rev-parse','--path-format=absolute','--git-common-dir')))
	$primaryOutput = Join-Path $primary 'Tools/WorktreeCli/Platforms/VisualStudio2026/Output'
	$sessionOutput = Join-Path $script:session 'Tools/WorktreeCli/Platforms/VisualStudio2026/Output'
	[IO.Directory]::CreateDirectory($primaryOutput) | Out-Null
	$primaryExecutable = Join-Path $primaryOutput 'WorktreeCli.exe'
	Copy-Item -LiteralPath (Get-Item -LiteralPath $Executable -Force).FullName -Destination $primaryExecutable
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

	$claim = Invoke-Sidecar 'Invoke-NextPlanClaim.ps1' @('-Queue','plans','-Plan',$plan) 0
	Assert-True ($claim.status -ceq 'pass' -and $claim.claim.plan -ceq $plan) 'Claim result did not bind the selected plan.'
	$executionCardPath = Join-Path $script:session 'Temp/execution-card.md'
	$executionCardText = "- Goal: exercise the next-plan sidecars.`n- Acceptance: exact artifacts and retained claim.`n"
	Set-Utf8File $executionCardPath $executionCardText
	$primaryMode = Invoke-Sidecar 'New-NextPlanPresentation.ps1' @(
		'-ClaimReceiptPath',$claim.receipt.path,'-ClaimReceiptSha256',$claim.receipt.sha256,
		'-ExecutionCardPath',$executionCardPath,'-FinalizationMode','primary-commit'
	) 1
	Assert-True ($primaryMode.code -ceq 'presentation.failed') '/next-plan accepted unreachable primary-commit mode.'
	$outsideTemp = Invoke-Sidecar 'New-NextPlanPresentation.ps1' @(
		'-ClaimReceiptPath',$claim.receipt.path,'-ClaimReceiptSha256',$claim.receipt.sha256,
		'-ExecutionCardPath',(Join-Path $script:session $plan),'-FinalizationMode','session-landing'
	) 1
	Assert-True ($outsideTemp.code -ceq 'presentation.failed') '/next-plan accepted an execution card outside Temp.'
	$presentation = Invoke-Sidecar 'New-NextPlanPresentation.ps1' @(
		'-ClaimReceiptPath',$claim.receipt.path,'-ClaimReceiptSha256',$claim.receipt.sha256,
		'-ExecutionCardPath',$executionCardPath,'-FinalizationMode','session-landing'
	) 0
	Assert-True (@($presentation.presentation.ranges).Count -ge 1) 'Presentation did not publish complete ranges.'
	$rendered = ''
	foreach ($range in @($presentation.presentation.ranges)) {
		$read = Invoke-Process (Join-Path $PSHOME 'pwsh.exe') @(
			'-NoLogo','-NoProfile','-File',(Join-Path $repositoryRoot '.agents/scripts/Read-AgentReportSection.ps1'),
			'-ReportPath',$presentation.presentation.path,'-ExpectedSha256',$presentation.presentation.sha256,'-Range',$range
		) $script:session
		if ($read.ExitCode -ne 0) { throw "Presentation range read failed: $($read.Stderr)" }
		$rendered += $read.Stdout
	}
	Assert-True ($rendered.Contains('## Complete resolved plan', [StringComparison]::Ordinal)) 'Presentation ranges omitted the complete plan section.'

	Set-Utf8File $executionCardPath "$executionCardText- changed after presentation`n"
	$changedCard = Invoke-Sidecar 'Confirm-NextPlanApproval.ps1' @('-PresentationReceiptPath',$presentation.receipt.path,'-PresentationReceiptSha256',$presentation.receipt.sha256) 2
	Assert-True ($changedCard.code -ceq 'approval.execution-card-changed') 'Changed execution card was not rejected at approval.'
	Set-Utf8File $executionCardPath $executionCardText

	Add-Content -LiteralPath (Join-Path $script:session $plan) -Value 'post-presentation change'
	$rejected = Invoke-Sidecar 'Confirm-NextPlanApproval.ps1' @('-PresentationReceiptPath',$presentation.receipt.path,'-PresentationReceiptSha256',$presentation.receipt.sha256) 2
	Assert-True ($rejected.code -ceq 'approval.precode-manifest-changed' -or $rejected.code -ceq 'approval.plan-changed') 'Changed plan was not rejected at approval.'
	Invoke-Git $script:session @('restore','--',$plan) | Out-Null
	$approval = Invoke-Sidecar 'Confirm-NextPlanApproval.ps1' @('-PresentationReceiptPath',$presentation.receipt.path,'-PresentationReceiptSha256',$presentation.receipt.sha256) 0
	Assert-True ($approval.status -ceq 'pass') 'Exact presentation was not approved.'

	Set-Utf8File (Join-Path $script:session 'Source/Implemented.txt') "implemented`n"
	Set-Utf8File $executionCardPath "$executionCardText- changed after approval`n"
	$postApprovalCard = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-ApprovalReceiptPath',$approval.receipt.path,'-ApprovalReceiptSha256',$approval.receipt.sha256) 2
	Assert-True ($postApprovalCard.code -ceq 'completion.execution-card-changed') 'Changed post-approval execution card was not a deterministic completion blocker.'
	Set-Utf8File $executionCardPath $executionCardText
	Add-Content -LiteralPath (Join-Path $script:session 'Documents/Plans/Order.md') -Value '| [TestPlan.md](TestPlan.md) | Small | 1 | 2 | 1 | 0 | - | conflicting duplicate |'
	$completionConflict = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-ApprovalReceiptPath',$approval.receipt.path,'-ApprovalReceiptSha256',$approval.receipt.sha256) 2
	Assert-True ($completionConflict.code -ceq 'completion.conflict') 'A WorktreeCli completion state conflict did not return exit 2.'
	Invoke-Git $script:session @('restore','--','Documents/Plans/Order.md') | Out-Null
	Add-Content -LiteralPath (Join-Path $script:session $plan) -Value 'post-approval change'
	$postApprovalPlan = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-ApprovalReceiptPath',$approval.receipt.path,'-ApprovalReceiptSha256',$approval.receipt.sha256) 2
	Assert-True ($postApprovalPlan.code -ceq 'completion.plan-changed') 'Changed post-approval plan was not a deterministic completion blocker.'
	Invoke-Git $script:session @('restore','--',$plan) | Out-Null
	$completion = Invoke-Sidecar 'Complete-NextPlan.ps1' @('-ApprovalReceiptPath',$approval.receipt.path,'-ApprovalReceiptSha256',$approval.receipt.sha256) 0
	Assert-True (-not $completion.workflowTerminal -and $completion.nextAction -ceq 'finalize-changes') 'Completion incorrectly became a terminal workflow result.'
	Assert-True (-not (Test-Path -LiteralPath (Join-Path $script:session $plan))) 'Completion retained the selected plan file.'
	$rowStatus = ConvertFrom-SingleJson (Invoke-Process $fixtureExecutable @('plan','row','status','--repo',$common,'--order','Documents/Plans/Order.md','--plan','TestPlan.md','--owner',$owner) $script:session) 0 'post-completion row status'
	Assert-True ($rowStatus.ownedByRequester) 'Completion did not retain the owner-held row claim.'

	$chain = Invoke-Sidecar 'Test-NextPlanReceiptChain.ps1' @('-CompletionReceiptPath',$completion.receipt.path,'-CompletionReceiptSha256',$completion.receipt.sha256) 0
	Assert-True ($chain.finalizationMode -ceq 'session-landing' -and -not $chain.workflowTerminal -and $chain.nextAction -ceq 'finalize-changes') 'Receipt chain did not return authoritative finalization state.'
	$currentCardSha = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([IO.File]::ReadAllBytes($executionCardPath))).ToLowerInvariant()
	Assert-True ($chain.executionCard.sha256 -ceq $currentCardSha) 'Receipt chain did not validate the current execution card.'
	$presentationBytes = [IO.File]::ReadAllBytes($presentation.presentation.path)
	[IO.File]::WriteAllBytes($presentation.presentation.path, $presentationBytes + [byte[]](10))
	$tamperedChain = Invoke-Sidecar 'Test-NextPlanReceiptChain.ps1' @('-CompletionReceiptPath',$completion.receipt.path,'-CompletionReceiptSha256',$completion.receipt.sha256) 2
	Assert-True ($tamperedChain.code -ceq 'chain.artifact-invalid') 'Tampered presentation artifact was not rejected.'
	[IO.File]::WriteAllBytes($presentation.presentation.path, $presentationBytes)
	$completionBytes = [IO.File]::ReadAllBytes($completion.receipt.path)
	$changedCompletion = $utf8.GetString($completionBytes) | ConvertFrom-Json -Depth 100
	$changedCompletion.finalizationMode = 'primary-commit'
	$changedCompletionBytes = $utf8.GetBytes(($changedCompletion | ConvertTo-Json -Depth 100 -Compress) + "`n")
	[IO.File]::WriteAllBytes($completion.receipt.path, $changedCompletionBytes)
	$changedCompletionSha = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($changedCompletionBytes)).ToLowerInvariant()
	$modeChain = Invoke-Sidecar 'Test-NextPlanReceiptChain.ps1' @('-CompletionReceiptPath',$completion.receipt.path,'-CompletionReceiptSha256',$changedCompletionSha) 2
	Assert-True ($modeChain.code -ceq 'chain.provenance-mismatch') 'A split finalization-mode chain was not rejected.'
	[IO.File]::WriteAllBytes($completion.receipt.path, $completionBytes)

	[pscustomobject]@{
		schemaVersion = 'broken-engine-next-plan-sidecar-fixtures/v1'
		status = 'pass'
		cases = @('missing environment rejection','invalid worktree rejection','live exclusion-ledger binding','wrong Output rejection','wrapper-derived claim','primary-commit rejection','outside-Temp card rejection','immutable ranged presentation','changed-card rejection','changed-plan rejection','approval binding','post-approval changed-card rejection','completion conflict','post-approval plan rejection','closure scan and retained-claim completion','receipt-chain validation','tamper rejection','mode-chain rejection')
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
}
