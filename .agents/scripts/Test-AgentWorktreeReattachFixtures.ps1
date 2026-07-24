# Scratch-only reattach checks. Every Start-AgentWorktreeSession invocation uses
# a PowerShell stub client; this fixture never launches Codex or Claude.
[CmdletBinding()]
param(
	[string] $RepositoryRoot = (Join-Path $PSScriptRoot '..\..')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-True([bool] $Condition, [string] $Message) {
	if (-not $Condition) { throw "ASSERT: $Message" }
}

function Invoke-Git([string[]] $Arguments) {
	$output = @(& git @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
}

function Get-BytesOrNull([string] $Path) {
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
	return [IO.File]::ReadAllBytes($Path)
}

function Test-BytesEqual($Expected, $Actual) {
	if ($null -eq $Expected -or $null -eq $Actual) { return $null -eq $Expected -and $null -eq $Actual }
	return [System.Collections.StructuralComparisons]::StructuralEqualityComparer.Equals($Expected, $Actual)
}

$sourceRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('broken-engine-reattach-' + [guid]::NewGuid().ToString())
$oldLocalAppData = $env:LOCALAPPDATA
$oldPath = $env:PATH
$oldFixtureRealGit = $env:FIXTURE_REAL_GIT
$oldFixtureGitMode = $env:FIXTURE_GIT_MODE
$oldFixtureGitTarget = $env:FIXTURE_GIT_TARGET
$oldFixtureGitHead = $env:FIXTURE_GIT_HEAD
$oldFixtureGitBranch = $env:FIXTURE_GIT_BRANCH
$oldFixturePwsh = $env:FIXTURE_PWSH
$environmentNames = @('BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER', 'BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE', 'BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE', 'BROKEN_ENGINE_WORKTREE_PATH', 'BROKEN_ENGINE_SESSION_BRANCH', 'BROKEN_ENGINE_PRIMARY_CHECKOUT', 'BROKEN_ENGINE_TARGET_BRANCH', 'BROKEN_ENGINE_BASELINE', 'BROKEN_ENGINE_AGENT_CLIENT', 'BROKEN_ENGINE_CLIENT_ARGUMENTS')
$oldEnvironment = @{}
foreach ($environmentName in $environmentNames) { $oldEnvironment[$environmentName] = [Environment]::GetEnvironmentVariable($environmentName, 'Process') }

try {
	New-Item -ItemType Directory -Path $scratch | Out-Null
	$localAppData = Join-Path $scratch 'local-app-data'
	$env:LOCALAPPDATA = $localAppData
	$primary = Join-Path $scratch 'primary'
	$worktree = Join-Path $scratch 'retained'
	$sourceScripts = Join-Path $sourceRoot '.agents\scripts'
	$destinationScripts = Join-Path $primary '.agents\scripts'
	New-Item -ItemType Directory -Path $primary, $destinationScripts | Out-Null
	foreach ($name in @('AgentScriptCommon.psm1', 'WorktreeCliSessionExclusion.psm1', 'AgentWorktreeSession.psm1', 'Start-AgentWorktreeSession.ps1')) {
		Copy-Item -LiteralPath (Join-Path $sourceScripts $name) -Destination (Join-Path $destinationScripts $name)
	}
	[IO.File]::WriteAllText((Join-Path $destinationScripts 'Bootstrap-AgentTools.ps1'), @'
param([string] $RepositoryRoot, [int] $WaitSeconds)
if ([string]::IsNullOrWhiteSpace($env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER)) { throw 'fixture bootstrap did not receive owner' }
[IO.File]::WriteAllText($env:FIXTURE_BOOTSTRAP_CAPTURE, $env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER)
'@, [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText((Join-Path $destinationScripts 'Provision-WorktreeThirdParty.ps1'), @'
param([string] $RepositoryRoot, [int] $WaitSeconds)
if ([string]::IsNullOrWhiteSpace($env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER)) { throw 'fixture provision did not receive owner' }
[IO.File]::WriteAllText($env:FIXTURE_PROVISION_CAPTURE, $env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER)
'@, [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText((Join-Path $destinationScripts 'Build-WorktreeDataPacker.ps1'), @'
param([string] $Worktree, [string] $WorktreeCliExecutable, [string] $PrimaryCheckout)
if ([string]::IsNullOrWhiteSpace($env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER)) { throw 'fixture datapacker did not receive owner' }
if (-not (Test-Path -LiteralPath $WorktreeCliExecutable -PathType Leaf)) { throw 'fixture datapacker did not receive the primary WorktreeCli executable' }
if ([string]::IsNullOrWhiteSpace($PrimaryCheckout)) { throw 'fixture datapacker did not receive the primary checkout' }
[IO.File]::WriteAllText($env:FIXTURE_DATAPACKER_CAPTURE, $env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER)
'@, [Text.UTF8Encoding]::new($false))
	$clientStub = Join-Path $scratch 'StubClient.ps1'
	[IO.File]::WriteAllText($clientStub, @'
$value = [ordered]@{
	owner = $env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER
	worktree = $env:BROKEN_ENGINE_WORKTREE_PATH
	branch = $env:BROKEN_ENGINE_SESSION_BRANCH
	targetBranch = $env:BROKEN_ENGINE_TARGET_BRANCH
	baseline = $env:BROKEN_ENGINE_BASELINE
	client = $env:BROKEN_ENGINE_AGENT_CLIENT
	admissionMode = $env:BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE
	clientArgumentTransport = $env:BROKEN_ENGINE_CLIENT_ARGUMENTS
}
[IO.File]::WriteAllText($env:FIXTURE_CLIENT_CAPTURE, ($value | ConvertTo-Json -Compress), [Text.UTF8Encoding]::new($false))
'@, [Text.UTF8Encoding]::new($false))
	$clientCommand = Join-Path $scratch 'StubClient.cmd'
	[IO.File]::WriteAllText($clientCommand, ('@"' + "$PSHOME\pwsh.exe" + '" -NoProfile -File "' + $clientStub + '"' + "`r`n"), [Text.UTF8Encoding]::new($false))
	Invoke-Git @('init', '-q', $primary) | Out-Null
	Invoke-Git @('-C', $primary, 'config', 'user.email', 'fixture@example.test') | Out-Null
	Invoke-Git @('-C', $primary, 'config', 'user.name', 'fixture') | Out-Null
	[IO.File]::WriteAllText((Join-Path $primary 'tracked.txt'), 'fixture', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $primary, 'add', '.') | Out-Null
	Invoke-Git @('-C', $primary, 'commit', '-qm', 'fixture baseline') | Out-Null
	$branch = @(Invoke-Git @('-C', $primary, 'branch', '--show-current'))[0].Trim()
	$baseline = @(Invoke-Git @('-C', $primary, 'rev-parse', 'HEAD'))[0].Trim()
	$uuid = [guid]::NewGuid().ToString()
	$sessionOwner = [guid]::NewGuid().ToString()
	$sessionBranch = "codex/$uuid"
	Invoke-Git @('-C', $primary, 'worktree', 'add', '-q', '-b', $sessionBranch, $worktree, $baseline) | Out-Null
	$skillsSource = Join-Path $scratch 'skills-source'
	New-Item -ItemType Directory -Path $skillsSource, (Join-Path $worktree '.claude') | Out-Null
	[IO.File]::WriteAllText((Join-Path $skillsSource 'skill.txt'), 'fixture')
	New-Item -ItemType Junction -Path (Join-Path $worktree '.claude\skills') -Target $skillsSource | Out-Null
	$toolOutput = Join-Path $primary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	New-Item -ItemType Directory -Path $toolOutput -Force | Out-Null
	[IO.File]::WriteAllText((Join-Path $toolOutput 'WorktreeCli.exe'), 'fixture')
	Import-Module (Join-Path $destinationScripts 'AgentWorktreeSession.psm1') -Force -DisableNameChecking
	Import-Module (Join-Path $destinationScripts 'WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking
	$primaryIdentity = Get-AgentWorktreePrimaryIdentity $primary
	$receiptValue = New-AgentWorktreeSessionReceipt -Client codex -PrimaryCheckout $primaryIdentity.Root -GitCommonDirectory $primaryIdentity.CommonDirectory -Worktree $worktree -WorktreeId $uuid -Branch $sessionBranch -TargetBranch $branch -Baseline $baseline -SessionOwner $sessionOwner
	$receiptPath = Write-AgentWorktreeSessionReceipt -Worktree $worktree -Receipt $receiptValue
	$receiptBytes = [IO.File]::ReadAllBytes($receiptPath)
	$integrityPath = Get-AgentWorktreeReceiptIntegrityPath $worktree
	$integrityBytes = [IO.File]::ReadAllBytes($integrityPath)
	$privateGitDirectory = Split-Path -Parent $receiptPath
	Assert-True ((Get-Item -LiteralPath $receiptPath -Force).DirectoryName -ceq $privateGitDirectory) 'Receipt was not written in the linked worktree private Git directory.'
	Assert-True (@(Get-ChildItem -LiteralPath $privateGitDirectory -Filter '.*.tmp' -Force).Count -eq 0) 'Atomic receipt write left a temporary file.'
	$duplicateWriteBlocked = $false
	try { Write-AgentWorktreeSessionReceipt -Worktree $worktree -Receipt $receiptValue | Out-Null } catch { $duplicateWriteBlocked = $true }
	Assert-True $duplicateWriteBlocked 'Receipt writer overwrote an existing receipt.'
	Assert-True (Test-BytesEqual $receiptBytes ([IO.File]::ReadAllBytes($receiptPath))) 'Receipt bytes changed after rejected overwrite.'
	Assert-True (Test-BytesEqual $integrityBytes ([IO.File]::ReadAllBytes($integrityPath))) 'Receipt integrity reference changed after rejected overwrite.'

	$env:FIXTURE_CLIENT_CAPTURE = Join-Path $scratch 'client.json'
	$env:FIXTURE_BOOTSTRAP_CAPTURE = Join-Path $scratch 'bootstrap.txt'
	$env:FIXTURE_PROVISION_CAPTURE = Join-Path $scratch 'provision.txt'
	$env:FIXTURE_DATAPACKER_CAPTURE = Join-Path $scratch 'datapacker.txt'
	$startScript = Join-Path $destinationScripts 'Start-AgentWorktreeSession.ps1'
	$invokeStart = {
		Remove-Item -LiteralPath $env:FIXTURE_CLIENT_CAPTURE -Force -ErrorAction SilentlyContinue
		$unused = @(& "$PSHOME\pwsh.exe" -NoProfile -File $startScript -Client codex -RepositoryRoot $primary -ReattachWorktree $worktree -LegacySessionsClosed -ClientExecutable $env:ComSpec -ClientArguments ('/c ' + $clientCommand) 2>&1)
		return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $unused }
	}
	$success = & $invokeStart
	Assert-True ($success.ExitCode -eq 0) "Valid reattach failed: $($success.Output -join '; ')"
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE -PathType Leaf) 'Valid reattach did not launch the stub client.'
	$clientState = Get-Content -Raw -LiteralPath $env:FIXTURE_CLIENT_CAPTURE | ConvertFrom-Json
	Assert-True ($clientState.owner -ceq $sessionOwner -and $clientState.worktree -ceq $worktree -and $clientState.branch -ceq $sessionBranch -and $clientState.targetBranch -ceq $branch -and $clientState.baseline -ceq $baseline -and $clientState.client -ceq 'codex' -and $clientState.admissionMode -ceq 'session') 'Valid reattach did not restore exact owner/provenance environment.'
	Assert-True ([string]::IsNullOrEmpty($clientState.clientArgumentTransport)) 'Client argument transport leaked to launched client.'
	Assert-True (Test-Path -LiteralPath $receiptPath -PathType Leaf) 'Cleanup removed the durable receipt.'
	$ledgerPath = (Get-WorktreeCliRepositoryIdentity $primary).LedgerPath
	$baselineLedgerBytes = Get-BytesOrNull $ledgerPath
	$baselineRegistration = (Invoke-Git @('-C', $primary, 'worktree', 'list', '--porcelain')) -join "`n"
	$quiescenceSidecar = Join-Path $sourceRoot '.agents\skills\finalize-changes\scripts\Wait-AgentToolsQuiescence.ps1'
	$quiescenceOutput = @(& "$PSHOME\pwsh.exe" -NoProfile -File $quiescenceSidecar -RepositoryRoot $primary -CooperatingSessionOwner $sessionOwner -WaitSeconds 0)
	Assert-True ($LASTEXITCODE -eq 0) 'Shared quiescence sidecar failed on an empty ledger.'
	$quiescence = ($quiescenceOutput -join "`n") | ConvertFrom-Json
	Assert-True ($quiescence.schemaVersion -ceq 'broken-engine-shared-quiescence/v1' -and $quiescence.disposition -ceq 'quiescent' -and -not $quiescence.requiresUserAuthority -and @($quiescence.liveBlockers).Count -eq 0) 'Shared quiescence sidecar did not return the typed quiescent disposition.'
	$quiescenceBlocker = [guid]::NewGuid().ToString()
	Register-WorktreeCliSession -RepositoryRoot $primary -Owner $quiescenceBlocker -Label 'quiescence blocker' -Worktree $worktree -LegacySessionsClosed | Out-Null
	try {
		$quiescenceOutput = @(& "$PSHOME\pwsh.exe" -NoProfile -File $quiescenceSidecar -RepositoryRoot $primary -CooperatingSessionOwner $sessionOwner -WaitSeconds 0)
		Assert-True ($LASTEXITCODE -eq 0) 'Shared quiescence sidecar failed on a live foreign session.'
		$quiescence = ($quiescenceOutput -join "`n") | ConvertFrom-Json
		Assert-True ($quiescence.disposition -ceq 'shared-quiescence' -and -not $quiescence.requiresUserAuthority -and @($quiescence.liveBlockers).Count -eq 1 -and $quiescence.liveBlockers[0].owner -ceq $quiescenceBlocker) 'Shared quiescence sidecar did not report its typed non-authority blocker.'
	}
	finally { Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $quiescenceBlocker }
	[IO.File]::WriteAllText($ledgerPath, '{', [Text.UTF8Encoding]::new($false))
	$quiescenceOutput = @(& "$PSHOME\pwsh.exe" -NoProfile -File $quiescenceSidecar -RepositoryRoot $primary -CooperatingSessionOwner $sessionOwner -WaitSeconds 0)
	Assert-True ($LASTEXITCODE -eq 2) 'Shared quiescence sidecar did not block unreadable coordination ledger state.'
	$quiescence = ($quiescenceOutput -join "`n") | ConvertFrom-Json
	Assert-True ($quiescence.disposition -ceq 'authority-required' -and $quiescence.requiresUserAuthority) 'Shared quiescence sidecar did not classify unreadable ledger state as authority-required.'
	if ($null -eq $baselineLedgerBytes) { Remove-Item -LiteralPath $ledgerPath -Force -ErrorAction SilentlyContinue }
	else { [IO.File]::WriteAllBytes($ledgerPath, $baselineLedgerBytes) }

	function Reset-FixtureState {
		$env:PATH = $oldPath
		$env:FIXTURE_GIT_MODE = $null
		& git -C $worktree reset --hard $baseline 2>$null | Out-Null
		foreach ($marker in @('MERGE_HEAD', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'BISECT_LOG', 'rebase-merge', 'rebase-apply', 'sequencer')) {
			$markerPath = @(Invoke-Git @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-path', $marker))[0].Trim()
			Remove-Item -LiteralPath $markerPath -Recurse -Force -ErrorAction SilentlyContinue
		}
		[IO.File]::WriteAllBytes($receiptPath, $receiptBytes)
		[IO.File]::WriteAllBytes($integrityPath, $integrityBytes)
		if ($null -eq $baselineLedgerBytes) { Remove-Item -LiteralPath $ledgerPath -Force -ErrorAction SilentlyContinue }
		else { New-Item -ItemType Directory -Path (Split-Path -Parent $ledgerPath) -Force | Out-Null; [IO.File]::WriteAllBytes($ledgerPath, $baselineLedgerBytes) }
	}

	function Set-ReceiptMutation([scriptblock] $Mutation) {
		$value = ([Text.UTF8Encoding]::new($false, $true).GetString($receiptBytes) | ConvertFrom-Json -DateKind String)
		& $Mutation $value
		[IO.File]::WriteAllText($receiptPath, ($value | ConvertTo-Json -Depth 8 -Compress), [Text.UTF8Encoding]::new($false))
	}

	function Invoke-Rejection([string] $Name, [scriptblock] $Mutation, [string] $ExtraPath = $null) {
		Reset-FixtureState
		foreach ($environmentName in $environmentNames) { [Environment]::SetEnvironmentVariable($environmentName, "fixture-$environmentName", 'Process') }
		& $Mutation
		$ledgerBefore = Get-BytesOrNull $ledgerPath
		$receiptBefore = Get-BytesOrNull $receiptPath
		$integrityBefore = Get-BytesOrNull $integrityPath
		$extraBefore = if ([string]::IsNullOrWhiteSpace($ExtraPath)) { $null } else { Get-BytesOrNull $ExtraPath }
		$registrationBefore = (Invoke-Git @('-C', $primary, 'worktree', 'list', '--porcelain')) -join "`n"
		$run = & $invokeStart
		Assert-True ($run.ExitCode -ne 0) "$Name unexpectedly reattached."
		Assert-True (Test-BytesEqual $ledgerBefore (Get-BytesOrNull $ledgerPath)) "$Name changed ledger bytes."
		Assert-True (Test-BytesEqual $receiptBefore (Get-BytesOrNull $receiptPath)) "$Name changed receipt bytes."
		Assert-True (Test-BytesEqual $integrityBefore (Get-BytesOrNull $integrityPath)) "$Name changed receipt integrity reference bytes."
		if (-not [string]::IsNullOrWhiteSpace($ExtraPath)) { Assert-True (Test-BytesEqual $extraBefore (Get-BytesOrNull $ExtraPath)) "$Name changed moved receipt bytes." }
		Assert-True ($registrationBefore -ceq ((Invoke-Git @('-C', $primary, 'worktree', 'list', '--porcelain')) -join "`n")) "$Name changed worktree registration state."
		foreach ($environmentName in $environmentNames) { Assert-True ([Environment]::GetEnvironmentVariable($environmentName, 'Process') -ceq "fixture-$environmentName") "$Name changed relevant parent environment '$environmentName'." }
		Assert-True (-not (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE)) "$Name launched the stub client."
		Write-Host "PASS $Name"
	}

	Invoke-Rejection 'missing receipt / legacy retained worktree' { Remove-Item -LiteralPath $receiptPath -Force }
	$wrongLocation = Join-Path $privateGitDirectory 'MovedReceipt.json'
	Invoke-Rejection 'moved receipt' { Move-Item -LiteralPath $receiptPath -Destination $wrongLocation } $wrongLocation
	Invoke-Rejection 'edited receipt client' { Set-ReceiptMutation { param($value) $value.client = 'claude'; $value.branch = "claude/$uuid" } }
	Invoke-Rejection 'owner-only schema and Git-consistent receipt edit' { Set-ReceiptMutation { param($value) $value.sessionOwner = [guid]::NewGuid().ToString() } }
	Reset-FixtureState
	$secondProof = Get-AgentWorktreeReattachProof -Client codex -RepositoryRoot $primary -Worktree $worktree
	Set-ReceiptMutation { param($value) $value.sessionOwner = [guid]::NewGuid().ToString() }
	$changedBytes = [IO.File]::ReadAllBytes($receiptPath)
	[IO.File]::WriteAllText($integrityPath, ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($changedBytes)).ToLowerInvariant()), [Text.UTF8Encoding]::new($false))
	$secondProofRejected = $false
	try { Get-AgentWorktreeReattachProof -Client codex -RepositoryRoot $primary -Worktree $worktree -ExpectedReceiptBytes $secondProof.Receipt.Bytes -ExpectedReceiptIntegrityBytes $secondProof.Receipt.IntegrityBytes | Out-Null } catch { $secondProofRejected = $true }
	Assert-True $secondProofRejected 'Second proof accepted changed receipt and matching integrity reference.'
	Invoke-Rejection 'wrong receipt worktree path' { Set-ReceiptMutation { param($value) $value.worktree = $primaryIdentity.Root } }
	Invoke-Rejection 'wrong receipt common directory' { Set-ReceiptMutation { param($value) $value.gitCommonDirectory = (Join-Path $scratch 'wrong-common') } }
	Invoke-Rejection 'wrong receipt UUID / branch' { Set-ReceiptMutation { param($value) $value.worktreeId = [guid]::NewGuid().ToString(); $value.branch = "codex/$($value.worktreeId)" } }
	Invoke-Rejection 'primary target branch mismatch' { Set-ReceiptMutation { param($value) $value.targetBranch = 'other-target' } }
	Invoke-Rejection 'strict schema' {
		[IO.File]::WriteAllText($receiptPath, '{}', [Text.UTF8Encoding]::new($false))
	}
	Invoke-Rejection 'strict UTC timestamp' { Set-ReceiptMutation { param($value) $value.createdUtc = '2020-01-01T00:00:00Z' } }
	$tree = @(Invoke-Git @('-C', $primary, 'rev-parse', 'HEAD^{tree}'))[0].Trim()
	$orphan = @(Invoke-Git @('-C', $primary, 'commit-tree', $tree, '-m', 'orphan fixture'))[0].Trim()
	Invoke-Rejection 'baseline non-ancestor of primary' { Set-ReceiptMutation { param($value) $value.baseline = $orphan } }
	Invoke-Rejection 'baseline non-ancestor of worktree' { Invoke-Git @('-C', $worktree, 'reset', '--hard', $orphan) | Out-Null }
	Invoke-Rejection 'active Git operation marker' {
		$markerPath = @(Invoke-Git @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-path', 'MERGE_HEAD'))[0].Trim()
		[IO.File]::WriteAllText($markerPath, $baseline)
	}

	$gitWrapperDirectory = Join-Path $scratch 'git-wrapper'
	New-Item -ItemType Directory -Path $gitWrapperDirectory | Out-Null
	$env:FIXTURE_REAL_GIT = (Get-Command git.exe -CommandType Application).Source
	$env:FIXTURE_PWSH = "$PSHOME\pwsh.exe"
	$env:FIXTURE_GIT_TARGET = $worktree
	$env:FIXTURE_GIT_HEAD = $baseline
	$env:FIXTURE_GIT_BRANCH = $sessionBranch
	[IO.File]::WriteAllText((Join-Path $gitWrapperDirectory 'git.cmd'), '@"%FIXTURE_PWSH%" -NoProfile -File "%~dp0GitFixture.ps1" %*' + "`r`nexit /b %errorlevel%`r`n")
	[IO.File]::WriteAllText((Join-Path $gitWrapperDirectory 'GitFixture.ps1'), @'
param([Parameter(ValueFromRemainingArguments = $true)][string[]] $Arguments)
$output = @(& $env:FIXTURE_REAL_GIT @Arguments 2>&1)
$exitCode = $LASTEXITCODE
$text = $output -join "`n"
if (($Arguments -join ' ') -ceq 'worktree list --porcelain -z') {
	$block = "worktree $env:FIXTURE_GIT_TARGET`0HEAD $env:FIXTURE_GIT_HEAD`0branch refs/heads/$env:FIXTURE_GIT_BRANCH`0"
	if ($env:FIXTURE_GIT_MODE -ceq 'duplicate') { $text += "`0$block" }
	elseif ($env:FIXTURE_GIT_MODE -ceq 'prunable') { $text = $text.Replace($block, "$block`0prunable fixture removal reason`0") }
}
[Console]::Out.Write($text)
exit $exitCode
'@, [Text.UTF8Encoding]::new($false))
	Invoke-Rejection 'non-unique registered worktree' { $env:PATH = "$gitWrapperDirectory;$oldPath"; $env:FIXTURE_GIT_MODE = 'duplicate' }
	Invoke-Rejection 'prunable registered worktree' { $env:PATH = "$gitWrapperDirectory;$oldPath"; $env:FIXTURE_GIT_MODE = 'prunable' }
	Reset-FixtureState
	$boundaryOwner = [guid]::NewGuid().ToString()
	foreach ($environmentName in $environmentNames) { [Environment]::SetEnvironmentVariable($environmentName, "fixture-$environmentName", 'Process') }
	$boundaryFirstProof = Get-AgentWorktreeReattachProof -Client codex -RepositoryRoot $primary -Worktree $worktree
	$boundaryLedgerBefore = Get-BytesOrNull $ledgerPath
	$boundaryIntegrityBefore = Get-BytesOrNull $integrityPath
	$boundaryRegistrationBefore = (Invoke-Git @('-C', $primary, 'worktree', 'list', '--porcelain')) -join "`n"
	$boundaryRejected = $false
	try {
		Restore-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner -Label 'fixture mutex-bound admission' -Worktree $worktree -LegacySessionsClosed `
			-BeforeAdmission {
				Set-ReceiptMutation { param($value) $value.sessionOwner = $boundaryOwner }
				Get-AgentWorktreeReattachProof -Client codex -RepositoryRoot $primary -Worktree $worktree -ExpectedReceiptBytes $boundaryFirstProof.Receipt.Bytes -ExpectedReceiptIntegrityBytes $boundaryFirstProof.Receipt.IntegrityBytes
			} | Out-Null
	}
	catch { $boundaryRejected = $true }
	Assert-True $boundaryRejected 'Mutex-bound receipt mutation unexpectedly admitted a session.'
	Assert-True (Test-BytesEqual $boundaryLedgerBefore (Get-BytesOrNull $ledgerPath)) 'Mutex-bound receipt mutation changed ledger bytes.'
	$boundaryReceiptAfter = ([Text.UTF8Encoding]::new($false, $true).GetString([IO.File]::ReadAllBytes($receiptPath)) | ConvertFrom-Json -DateKind String)
	Assert-True ($boundaryReceiptAfter.sessionOwner -ceq $boundaryOwner) 'Mutex-bound receipt mutation did not preserve the external receipt edit.'
	Assert-True (Test-BytesEqual $boundaryIntegrityBefore (Get-BytesOrNull $integrityPath)) 'Mutex-bound receipt mutation changed integrity reference bytes.'
	Assert-True ($boundaryRegistrationBefore -ceq ((Invoke-Git @('-C', $primary, 'worktree', 'list', '--porcelain')) -join "`n")) 'Mutex-bound receipt mutation changed worktree registration state.'
	foreach ($environmentName in $environmentNames) { Assert-True ([Environment]::GetEnvironmentVariable($environmentName, 'Process') -ceq "fixture-$environmentName") "Mutex-bound receipt mutation changed relevant parent environment '$environmentName'." }
	Assert-True (-not (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE)) 'Mutex-bound receipt mutation launched the stub client.'
	Write-Host 'PASS mutex-bound receipt mutation blocks admission without side effects'
	Reset-FixtureState
	$leasedFirstProof = Get-AgentWorktreeReattachProof -Client codex -RepositoryRoot $primary -Worktree $worktree
	$leasedLedgerBefore = Get-BytesOrNull $ledgerPath
	$leasedClaim = Restore-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner -Label 'fixture receipt lease admission' -Worktree $worktree -LegacySessionsClosed `
		-BeforeAdmission {
			$lease = Open-AgentWorktreeReceiptReadLease $worktree
			try {
				$leasedProof = Get-AgentWorktreeReattachProof -Client codex -RepositoryRoot $primary -Worktree $worktree -ExpectedReceiptBytes $leasedFirstProof.Receipt.Bytes -ExpectedReceiptIntegrityBytes $leasedFirstProof.Receipt.IntegrityBytes -ReadLease $lease
				return [pscustomobject]@{ Proof = $leasedProof; Lease = $lease }
			}
			catch { $lease.ReceiptStream.Dispose(); $lease.IntegrityStream.Dispose(); throw }
		} -BeforeClaimWrite {
			foreach ($authorityPath in @($receiptPath, $integrityPath)) {
				$mutationStream = $null
				try { $mutationStream = [IO.File]::Open($authorityPath, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::Read) }
				catch [IO.IOException] { continue }
				finally { if ($null -ne $mutationStream) { $mutationStream.Dispose() } }
				throw "Receipt lease permitted mutation after proof: '$authorityPath'."
			}
		}
	Assert-True ($leasedClaim.Mode -ceq 'session') 'Receipt lease fixture did not install the expected session claim.'
	Assert-True (Test-BytesEqual $leasedFirstProof.Receipt.Bytes ([IO.File]::ReadAllBytes($receiptPath))) 'Receipt lease admission did not retain matching receipt bytes.'
	Assert-True (Test-BytesEqual $leasedFirstProof.Receipt.IntegrityBytes ([IO.File]::ReadAllBytes($integrityPath))) 'Receipt lease admission did not retain matching integrity bytes.'
	Assert-True (Test-BytesEqual $leasedFirstProof.Receipt.Bytes $leasedClaim.AdmissionProof.Receipt.Bytes) 'Receipt lease admission did not use the matching proven receipt bytes.'
	Assert-True (Test-BytesEqual $leasedFirstProof.Receipt.IntegrityBytes $leasedClaim.AdmissionProof.Receipt.IntegrityBytes) 'Receipt lease admission did not use the matching proven integrity bytes.'
	Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner
	Assert-True (Test-BytesEqual $leasedLedgerBefore (Get-BytesOrNull $ledgerPath)) 'Receipt lease fixture did not restore ledger bytes after claim release.'
	Write-Host 'PASS receipt lease blocks post-proof mutation through claim write'

	$liveOwner = [guid]::NewGuid().ToString()
	Invoke-Rejection 'duplicate live owner' {
		Register-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner -Label 'fixture collision' -Worktree $worktree -LegacySessionsClosed | Out-Null
	}
	try { Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $sessionOwner } catch { }
	Invoke-Rejection 'duplicate live worktree' {
		Register-WorktreeCliSession -RepositoryRoot $primary -Owner $liveOwner -Label 'fixture collision' -Worktree $worktree -LegacySessionsClosed | Out-Null
	}
	try { Unregister-WorktreeCliSession -RepositoryRoot $primary -Owner $liveOwner } catch { }

	# Wrapper forwarding uses a fake Start script. No wrapper test can launch a real client.
	$fakeStart = @'
param([string] $Client, [string] $RepositoryRoot, [string[]] $ClientArguments, [string] $ReattachWorktree, [switch] $LegacySessionsClosed)
$value = [ordered]@{ client = $Client; repositoryRoot = $RepositoryRoot; reattachWorktree = $ReattachWorktree; clientArguments = @($ClientArguments); encodedClientArguments = $env:BROKEN_ENGINE_CLIENT_ARGUMENTS }
[IO.File]::WriteAllText($env:FIXTURE_WRAPPER_CAPTURE, ($value | ConvertTo-Json -Depth 8 -Compress), [Text.UTF8Encoding]::new($false))
'@
	[IO.File]::WriteAllText($startScript, $fakeStart, [Text.UTF8Encoding]::new($false))
	New-Item -ItemType Directory -Path (Join-Path $primary '.codex'), (Join-Path $primary '.claude') -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $sourceRoot '.codex\codex-worktree.ps1') -Destination (Join-Path $primary '.codex\codex-worktree.ps1')
	Copy-Item -LiteralPath (Join-Path $sourceRoot '.claude\claude-worktree.sh') -Destination (Join-Path $primary '.claude\claude-worktree.sh')
	$env:FIXTURE_WRAPPER_CAPTURE = Join-Path $scratch 'wrapper.json'
	Remove-Item -LiteralPath $env:FIXTURE_WRAPPER_CAPTURE -Force -ErrorAction SilentlyContinue
	Push-Location $primary
	try { & "$PSHOME\pwsh.exe" -NoProfile -File (Join-Path $primary '.codex\codex-worktree.ps1') -ReattachWorktree $worktree }
	finally { Pop-Location }
	Assert-True ($LASTEXITCODE -eq 0) 'Codex wrapper forwarding failed.'
	$codexForwarding = Get-Content -Raw -LiteralPath $env:FIXTURE_WRAPPER_CAPTURE | ConvertFrom-Json
	Assert-True ($codexForwarding.client -ceq 'codex' -and $codexForwarding.reattachWorktree -ceq $worktree -and @($codexForwarding.clientArguments).Count -eq 1 -and $codexForwarding.clientArguments[0] -ceq '--dangerously-bypass-approvals-and-sandbox') 'Codex wrapper did not forward reattach separately from client arguments.'
	$bash = 'C:\Program Files\Git\bin\bash.exe'
	Assert-True (Test-Path -LiteralPath $bash -PathType Leaf) 'Git Bash is required for Claude wrapper fixture.'
	Remove-Item -LiteralPath $env:FIXTURE_WRAPPER_CAPTURE -Force -ErrorAction SilentlyContinue
	$bashCommand = "cd '$($primary.Replace('\', '/'))' && './.claude/claude-worktree.sh' --reattach-worktree '$($worktree.Replace('\', '/'))' --fixture-client-argument"
	& $bash -lc $bashCommand
	Assert-True ($LASTEXITCODE -eq 0) 'Claude wrapper forwarding failed.'
	$claudeForwarding = Get-Content -Raw -LiteralPath $env:FIXTURE_WRAPPER_CAPTURE | ConvertFrom-Json
	$decodedArguments = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($claudeForwarding.encodedClientArguments)) -split "`0" | Where-Object { $_.Length -ne 0 }
	Assert-True ($claudeForwarding.client -ceq 'claude' -and -not [string]::IsNullOrWhiteSpace($claudeForwarding.reattachWorktree) -and $decodedArguments -contains '--fixture-client-argument' -and $decodedArguments -notcontains '--reattach-worktree' -and $decodedArguments -notcontains $worktree) 'Claude wrapper leaked reattach input into client arguments.'
	Write-Host 'PASS wrapper forwarding: Codex and Claude'
	Write-Host 'PASS Test-AgentWorktreeReattachFixtures'
}
finally {
	foreach ($environmentName in $environmentNames) { [Environment]::SetEnvironmentVariable($environmentName, $oldEnvironment[$environmentName], 'Process') }
	$env:LOCALAPPDATA = $oldLocalAppData
	$env:PATH = $oldPath
	$env:FIXTURE_REAL_GIT = $oldFixtureRealGit
	$env:FIXTURE_GIT_MODE = $oldFixtureGitMode
	$env:FIXTURE_GIT_TARGET = $oldFixtureGitTarget
	$env:FIXTURE_GIT_HEAD = $oldFixtureGitHead
	$env:FIXTURE_GIT_BRANCH = $oldFixtureGitBranch
	$env:FIXTURE_PWSH = $oldFixturePwsh
	if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
}
