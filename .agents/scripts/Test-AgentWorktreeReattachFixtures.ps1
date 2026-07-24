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
$environmentNames = @('BROKEN_ENGINE_SESSION_OWNER', 'BROKEN_ENGINE_WORKTREE_PATH', 'BROKEN_ENGINE_SESSION_BRANCH', 'BROKEN_ENGINE_PRIMARY_CHECKOUT', 'BROKEN_ENGINE_TARGET_BRANCH', 'BROKEN_ENGINE_BASELINE', 'BROKEN_ENGINE_AGENT_CLIENT', 'BROKEN_ENGINE_CLIENT_ARGUMENTS')
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
	foreach ($name in @('AgentScriptCommon.psm1', 'WorktreeCliSessionExclusion.psm1', 'AgentWorktreeSession.psm1', 'Start-AgentWorktreeSession.ps1', 'Repair-AgentWorktreeSquashedBaseline.ps1')) {
		Copy-Item -LiteralPath (Join-Path $sourceScripts $name) -Destination (Join-Path $destinationScripts $name)
	}
	[IO.File]::WriteAllText((Join-Path $destinationScripts 'Bootstrap-AgentTools.ps1'), @'
param([string] $RepositoryRoot, [int] $WaitSeconds)
if ([string]::IsNullOrWhiteSpace($env:BROKEN_ENGINE_SESSION_OWNER)) { throw 'fixture bootstrap did not receive owner' }
[IO.File]::WriteAllText($env:FIXTURE_BOOTSTRAP_CAPTURE, $env:BROKEN_ENGINE_SESSION_OWNER)
'@, [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText((Join-Path $destinationScripts 'Provision-WorktreeThirdParty.ps1'), @'
param([string] $RepositoryRoot, [int] $WaitSeconds)
if ([string]::IsNullOrWhiteSpace($env:BROKEN_ENGINE_SESSION_OWNER)) { throw 'fixture provision did not receive owner' }
[IO.File]::WriteAllText($env:FIXTURE_PROVISION_CAPTURE, $env:BROKEN_ENGINE_SESSION_OWNER)
'@, [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText((Join-Path $destinationScripts 'Build-WorktreeDataPacker.ps1'), @'
param([string] $Worktree, [string] $WorktreeCliExecutable, [string] $PrimaryCheckout)
if ([string]::IsNullOrWhiteSpace($env:BROKEN_ENGINE_SESSION_OWNER)) { throw 'fixture datapacker did not receive owner' }
if (-not (Test-Path -LiteralPath $WorktreeCliExecutable -PathType Leaf)) { throw 'fixture datapacker did not receive the primary WorktreeCli executable' }
if ([string]::IsNullOrWhiteSpace($PrimaryCheckout)) { throw 'fixture datapacker did not receive the primary checkout' }
[IO.File]::WriteAllText($env:FIXTURE_DATAPACKER_CAPTURE, $env:BROKEN_ENGINE_SESSION_OWNER)
'@, [Text.UTF8Encoding]::new($false))
	$clientStub = Join-Path $scratch 'StubClient.ps1'
	[IO.File]::WriteAllText($clientStub, @'
$value = [ordered]@{
	owner = $env:BROKEN_ENGINE_SESSION_OWNER
	worktree = $env:BROKEN_ENGINE_WORKTREE_PATH
	branch = $env:BROKEN_ENGINE_SESSION_BRANCH
	targetBranch = $env:BROKEN_ENGINE_TARGET_BRANCH
	baseline = $env:BROKEN_ENGINE_BASELINE
	client = $env:BROKEN_ENGINE_AGENT_CLIENT
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
	# The squash-repair sidecar shells out to WorktreeCli.exe for `plan reparent-claims`, so the fixture
	# executable must be a real PE that answers that command. No claim mutation happens in this scratch repo,
	# so the stub always returns the no-claims result (real claim reparenting is covered by
	# Test-WorktreeCliPlanScheduler.ps1 against the built CLI).
	$worktreeCliStub = Join-Path $toolOutput 'WorktreeCli.exe'
	$stubSource = Join-Path $scratch 'WorktreeCliReparentStub.cs'
	[IO.File]::WriteAllText($stubSource, @'
using System;
public static class WorktreeCliReparentStub {
 public static int Main(string[] args) {
  Console.Write("{\"operation\":\"reparent-claims\",\"status\":\"ok\",\"code\":\"no-claims\",\"reparentedClaims\":[],\"updatedReceipts\":[]}");
  return 0;
 }
}
'@, [Text.UTF8Encoding]::new($false))
	$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
	$stubCompile = & $csc '/nologo' '/target:exe' "/out:$worktreeCliStub" $stubSource 2>&1
	Assert-True (Test-Path -LiteralPath $worktreeCliStub -PathType Leaf) "WorktreeCli reparent stub compile failed: $($stubCompile -join '; ')"
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
		$unused = @(& "$PSHOME\pwsh.exe" -NoProfile -File $startScript -Client codex -RepositoryRoot $primary -ReattachWorktree $worktree -ClientExecutable $env:ComSpec -ClientArguments ('/c ' + $clientCommand) 2>&1)
		return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $unused }
	}
	$success = & $invokeStart
	Assert-True ($success.ExitCode -eq 0) "Valid reattach failed: $($success.Output -join '; ')"
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE -PathType Leaf) 'Valid reattach did not launch the stub client.'
	$clientState = Get-Content -Raw -LiteralPath $env:FIXTURE_CLIENT_CAPTURE | ConvertFrom-Json
	Assert-True ($clientState.owner -ceq $sessionOwner -and $clientState.worktree -ceq $worktree -and $clientState.branch -ceq $sessionBranch -and $clientState.targetBranch -ceq $branch -and $clientState.baseline -ceq $baseline -and $clientState.client -ceq 'codex') 'Valid reattach did not restore exact owner/provenance environment.'
	Assert-True ([string]::IsNullOrEmpty($clientState.clientArgumentTransport)) 'Client argument transport leaked to launched client.'
	Assert-True (Test-Path -LiteralPath $receiptPath -PathType Leaf) 'Cleanup removed the durable receipt.'
	$ledgerPath = (Get-WorktreeCliRepositoryIdentity $primary).LedgerPath
	Assert-True (-not (Test-Path -LiteralPath $ledgerPath)) 'Valid reattach registered a session ledger claim; the wrapper must register nothing.'
	$baselineLedgerBytes = Get-BytesOrNull $ledgerPath
	$baselineRegistration = (Invoke-Git @('-C', $primary, 'worktree', 'list', '--porcelain')) -join "`n"
	$quiescenceSidecar = Join-Path $sourceRoot '.agents\skills\finalize-changes\scripts\Wait-AgentToolsQuiescence.ps1'
	$quiescenceOutput = @(& "$PSHOME\pwsh.exe" -NoProfile -File $quiescenceSidecar -RepositoryRoot $primary -CooperatingSessionOwner $sessionOwner -WaitSeconds 0)
	Assert-True ($LASTEXITCODE -eq 0) 'Shared quiescence sidecar failed on an empty ledger.'
	$quiescence = ($quiescenceOutput -join "`n") | ConvertFrom-Json
	Assert-True ($quiescence.schemaVersion -ceq 'broken-engine-shared-quiescence/v1' -and $quiescence.disposition -ceq 'quiescent' -and -not $quiescence.requiresUserAuthority -and @($quiescence.liveBlockers).Count -eq 0) 'Shared quiescence sidecar did not return the typed quiescent disposition.'
	$quiescenceBlocker = [guid]::NewGuid().ToString()
	Register-WorktreeCliSession -RepositoryRoot $primary -Owner $quiescenceBlocker -Label 'quiescence blocker' -Worktree $worktree | Out-Null
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
	# Clear the git-wrapper PATH and mode left by the rejection scenarios before invoking real wrappers.
	Reset-FixtureState

	# Primary-squash re-parent: after the user rewrites the primary tip the immutable receipt baseline is no
	# longer reachable, so reattach first runs Repair-AgentWorktreeSquashedBaseline to `git rebase --onto` the
	# session onto the squashed tip and rewrite the receipt baseline. `git commit-tree` (parentless, mirroring
	# the orphan fixture above) stands in for the end-of-day `git reset --soft <root> && git commit` squash.
	function New-SquashedPrimary([string] $TrackedContent) {
		if ($null -ne $TrackedContent) {
			[IO.File]::WriteAllText((Join-Path $primary 'tracked.txt'), $TrackedContent, [Text.UTF8Encoding]::new($false))
			Invoke-Git @('-C', $primary, 'add', '--', 'tracked.txt') | Out-Null
		}
		$tree = @(Invoke-Git @('-C', $primary, 'write-tree'))[0].Trim()
		$squashed = @(Invoke-Git @('-C', $primary, 'commit-tree', $tree, '-m', 'squashed day'))[0].Trim()
		Invoke-Git @('-C', $primary, 'reset', '--hard', $squashed) | Out-Null
		return $squashed
	}
	function Reset-SquashFixtureState {
		Invoke-Git @('-C', $primary, 'reset', '--hard', $baseline) | Out-Null
		Reset-FixtureState
	}
	function Get-ReceiptBaseline { return (Read-AgentWorktreeSessionReceipt -Worktree $worktree).Value.baseline }
	function Get-OutputText([object] $Run) { return (($Run.Output | ForEach-Object { "$_" }) -join "`n") }
	function Get-RepairResult([object] $Run) {
		$line = $Run.Output | Where-Object { "$_" -match 'broken-engine-baseline-reparent/v1' } | Select-Object -First 1
		if ($null -eq $line) { return $null }
		return ("$line" | ConvertFrom-Json)
	}

	# (a) Clean re-parent: a session commit is replayed onto the squashed tip and the old parent leaves history.
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $worktree 'session-work.txt'), 'session change', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $worktree, 'add', '--', 'session-work.txt') | Out-Null
	Invoke-Git @('-C', $worktree, 'commit', '-qm', 'session work') | Out-Null
	$cleanNew = New-SquashedPrimary $null
	Remove-Item -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE -Force -ErrorAction SilentlyContinue
	$cleanRun = & $invokeStart
	Assert-True ($cleanRun.ExitCode -eq 0) "Clean re-parent reattach failed: $($cleanRun.Output -join '; ')"
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE -PathType Leaf) 'Clean re-parent did not launch the client.'
	$cleanClient = Get-Content -Raw -LiteralPath $env:FIXTURE_CLIENT_CAPTURE | ConvertFrom-Json
	Assert-True ($cleanClient.baseline -ceq $cleanNew) 'Clean re-parent client baseline is not the squashed tip.'
	Assert-True ((Get-ReceiptBaseline) -ceq $cleanNew) 'Clean re-parent did not rewrite the receipt baseline.'
	Assert-True (-not (Test-BytesEqual $receiptBytes (Get-BytesOrNull $receiptPath))) 'Clean re-parent left the receipt bytes unchanged.'
	$cleanWorktreeHead = @(Invoke-Git @('-C', $worktree, 'rev-parse', 'HEAD'))[0].Trim()
	& git -C $worktree merge-base --is-ancestor $cleanNew $cleanWorktreeHead *> $null
	Assert-True ($LASTEXITCODE -eq 0) 'Squashed tip is not an ancestor of the re-parented worktree HEAD.'
	Assert-True (-not (@(Invoke-Git @('-C', $worktree, 'rev-list', 'HEAD')) -ccontains $baseline)) 'Old parent commit is still reachable from the re-parented session HEAD.'
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE -PathType Leaf) 'Clean re-parent skipped the DataPacker build.'
	$cleanRepair = Get-RepairResult $cleanRun
	Assert-True ($null -ne $cleanRepair -and $cleanRepair.status -ceq 'reparented') 'Clean re-parent did not report the reparented status.'
	Write-Host 'PASS clean re-parent after squash'

	# (b) Conflict: a divergent session edit collides with the squash, leaving a resolvable rebase.
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $worktree 'tracked.txt'), 'session version', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $worktree, 'commit', '-aqm', 'session divergent') | Out-Null
	$conflictNew = New-SquashedPrimary 'squashed version'
	Remove-Item -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE -Force -ErrorAction SilentlyContinue
	$conflictRun = & $invokeStart
	Assert-True ($conflictRun.ExitCode -eq 0) "Conflict re-parent reattach failed: $($conflictRun.Output -join '; ')"
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE -PathType Leaf) 'Conflict re-parent did not launch the client.'
	$conflictClient = Get-Content -Raw -LiteralPath $env:FIXTURE_CLIENT_CAPTURE | ConvertFrom-Json
	Assert-True ($conflictClient.baseline -ceq $conflictNew) 'Conflict re-parent client baseline is not the squashed tip.'
	Assert-True ((Get-ReceiptBaseline) -ceq $conflictNew) 'Conflict re-parent did not rewrite the receipt baseline before launch.'
	$conflictRebaseMerge = @(Invoke-Git @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-path', 'rebase-merge'))[0].Trim()
	Assert-True (Test-Path -LiteralPath $conflictRebaseMerge) 'Conflict re-parent left no rebase markers for the session to resolve.'
	Assert-True (-not (Test-Path -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE)) 'Conflict re-parent did not skip the DataPacker build.'
	$conflictText = Get-OutputText $conflictRun
	Assert-True ($conflictText -match 'FIRST TASK') 'Conflict banner omitted the resolve-first instruction.'
	Assert-True ($conflictText -match 'git rebase --continue') 'Conflict banner omitted the rebase --continue instruction.'
	Assert-True ($conflictText -match 'NO scheduler operation') 'Conflict banner omitted the no-scheduler-ops instruction.'
	Assert-True ($conflictText -match 'Build-WorktreeDataPacker') 'Conflict banner omitted the deferred DataPacker build instruction.'
	$conflictRepair = Get-RepairResult $conflictRun
	Assert-True ($null -ne $conflictRepair -and $conflictRepair.status -ceq 'reparented-conflict') 'Conflict re-parent did not report the reparented-conflict status.'
	Write-Host 'PASS conflict re-parent leaves a resolvable rebase and launches the client'
	[IO.File]::WriteAllText((Join-Path $worktree 'tracked.txt'), 'resolved', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $worktree, 'add', '--', 'tracked.txt') | Out-Null
	$previousGitEditor = $env:GIT_EDITOR
	$env:GIT_EDITOR = 'true'
	try { & git -C $worktree rebase --continue *> $null; Assert-True ($LASTEXITCODE -eq 0) 'Manual conflict resolution and rebase --continue failed.' }
	finally { $env:GIT_EDITOR = $previousGitEditor }
	$continuedRun = & $invokeStart
	Assert-True ($continuedRun.ExitCode -eq 0) "Post-continue reattach failed: $($continuedRun.Output -join '; ')"
	$continuedText = Get-OutputText $continuedRun
	Assert-True (-not ($continuedText -match 'FIRST TASK')) 'Post-continue reattach re-emitted the conflict banner.'
	Assert-True ($null -eq (Get-RepairResult $continuedRun)) 'Post-continue reattach re-ran the squash repair instead of reporting not-needed.'
	Write-Host 'PASS conflict re-parent resolves to a normal reattach'

	# (c) Dirty worktree: an uncommitted tracked edit is autostashed across the re-parent and restored.
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $worktree 'tracked.txt'), 'dirty local edit', [Text.UTF8Encoding]::new($false))
	$dirtyNew = New-SquashedPrimary $null
	$dirtyRun = & $invokeStart
	Assert-True ($dirtyRun.ExitCode -eq 0) "Dirty-tree re-parent reattach failed: $($dirtyRun.Output -join '; ')"
	Assert-True ((Get-ReceiptBaseline) -ceq $dirtyNew) 'Dirty-tree re-parent did not rewrite the receipt baseline.'
	Assert-True ((@(Invoke-Git @('-C', $worktree, 'rev-parse', 'HEAD'))[0].Trim()) -ceq $dirtyNew) 'Dirty-tree autostash re-parent did not land the worktree on the squashed tip.'
	Assert-True ((Get-Content -Raw -LiteralPath (Join-Path $worktree 'tracked.txt')) -match 'dirty local edit') 'Autostash did not restore the uncommitted change after the re-parent.'
	Write-Host 'PASS dirty-tree autostash re-parent preserves uncommitted work'

	# (d) Tampered receipt: a hand-edited baseline with a stale integrity sha fails closed and starts no rebase.
	Reset-SquashFixtureState
	New-SquashedPrimary $null | Out-Null
	Set-ReceiptMutation { param($value) $value.baseline = $orphan }
	$tamperedReceiptBytes = Get-BytesOrNull $receiptPath
	$tamperedIntegrityBytes = Get-BytesOrNull $integrityPath
	$tamperRun = & $invokeStart
	Assert-True ($tamperRun.ExitCode -ne 0) 'Tampered receipt was accepted during a squash re-parent.'
	Assert-True (-not (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE)) 'Tampered receipt re-parent launched the client.'
	$tamperRebaseMerge = @(Invoke-Git @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-path', 'rebase-merge'))[0].Trim()
	$tamperRebaseApply = @(Invoke-Git @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-path', 'rebase-apply'))[0].Trim()
	Assert-True (-not (Test-Path -LiteralPath $tamperRebaseMerge) -and -not (Test-Path -LiteralPath $tamperRebaseApply)) 'Tampered receipt re-parent started a rebase before failing closed.'
	Assert-True (Test-BytesEqual $tamperedReceiptBytes (Get-BytesOrNull $receiptPath)) 'Tampered receipt re-parent rewrote the receipt.'
	Assert-True (Test-BytesEqual $tamperedIntegrityBytes (Get-BytesOrNull $integrityPath)) 'Tampered receipt re-parent rewrote the integrity reference.'
	Write-Host 'PASS tampered receipt fails closed without starting a re-parent'

	# (e) Idempotence: after a clean re-parent a direct sidecar rerun is not-needed and reattach is normal.
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $worktree 'session-work.txt'), 'session change', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $worktree, 'add', '--', 'session-work.txt') | Out-Null
	Invoke-Git @('-C', $worktree, 'commit', '-qm', 'session work') | Out-Null
	$idempotentNew = New-SquashedPrimary $null
	$firstRun = & $invokeStart
	Assert-True ($firstRun.ExitCode -eq 0) "Idempotence setup re-parent failed: $($firstRun.Output -join '; ')"
	Assert-True ((Get-ReceiptBaseline) -ceq $idempotentNew) 'Idempotence setup did not re-parent.'
	$repairScript = Join-Path $destinationScripts 'Repair-AgentWorktreeSquashedBaseline.ps1'
	$notNeededOutput = @(& "$PSHOME\pwsh.exe" -NoProfile -File $repairScript -RepositoryRoot $primary -Worktree $worktree -WorktreeCliExecutable $worktreeCliStub)
	Assert-True ($LASTEXITCODE -eq 0) 'Idempotent repair rerun did not exit 0.'
	$notNeeded = ($notNeededOutput -join "`n") | ConvertFrom-Json
	Assert-True ($notNeeded.status -ceq 'not-needed') 'Idempotent repair rerun did not report not-needed.'
	$secondRun = & $invokeStart
	Assert-True ($secondRun.ExitCode -eq 0) "Idempotent second reattach failed: $($secondRun.Output -join '; ')"
	Assert-True ($null -eq (Get-RepairResult $secondRun)) 'Idempotent second reattach re-ran the squash repair.'
	Write-Host 'PASS idempotent re-parent: rerun is not-needed and reattach is normal'

	# (f) Autostash pop-conflict: an uncommitted edit collides with the squashed content when the autostash is
	# popped. Unlike (b), there is no session commit, so the rebase itself completes (exit 0, HEAD reattaches at
	# the squashed tip) and the conflict is only in the kept stash. The repair reports reparented-conflict with
	# rebaseInProgress false, and the banner tells the session to resolve then `git stash drop` (no rebase
	# --continue; scheduler ops are safe because HEAD is attached). DataPacker is still skipped.
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $worktree 'tracked.txt'), 'uncommitted local edit', [Text.UTF8Encoding]::new($false))
	$popNew = New-SquashedPrimary 'squashed conflicting content'
	Remove-Item -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE -Force -ErrorAction SilentlyContinue
	$popRun = & $invokeStart
	Assert-True ($popRun.ExitCode -eq 0) "Autostash pop-conflict reattach failed: $($popRun.Output -join '; ')"
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE -PathType Leaf) 'Autostash pop-conflict did not launch the client.'
	$popClient = Get-Content -Raw -LiteralPath $env:FIXTURE_CLIENT_CAPTURE | ConvertFrom-Json
	Assert-True ($popClient.baseline -ceq $popNew) 'Autostash pop-conflict client baseline is not the squashed tip.'
	Assert-True ((Get-ReceiptBaseline) -ceq $popNew) 'Autostash pop-conflict did not rewrite the receipt baseline.'
	# The rebase completed: HEAD is attached at the squashed tip and no rebase state remains (unlike (b)), even
	# though the working tree carries the unresolved stash-pop conflict and git kept the stash.
	Assert-True ((@(Invoke-Git @('-C', $worktree, 'rev-parse', 'HEAD'))[0].Trim()) -ceq $popNew) 'Autostash pop-conflict did not reattach HEAD at the squashed tip.'
	$popRebaseMerge = @(Invoke-Git @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-path', 'rebase-merge'))[0].Trim()
	$popRebaseApply = @(Invoke-Git @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-path', 'rebase-apply'))[0].Trim()
	Assert-True (-not (Test-Path -LiteralPath $popRebaseMerge) -and -not (Test-Path -LiteralPath $popRebaseApply)) 'Autostash pop-conflict left a rebase in progress.'
	Assert-True (@(@(Invoke-Git @('-C', $worktree, 'status', '--porcelain')) | Where-Object { $_ -match '^(UU|AA|DD|AU|UA|UD|DU) ' }).Count -ge 1) 'Autostash pop-conflict left no unmerged working-tree entries.'
	Assert-True (@(Invoke-Git @('-C', $worktree, 'stash', 'list')).Count -ge 1) 'Autostash pop-conflict did not keep the conflicting stash.'
	Assert-True (-not (Test-Path -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE)) 'Autostash pop-conflict did not skip the DataPacker build.'
	$popRepair = Get-RepairResult $popRun
	Assert-True ($null -ne $popRepair -and $popRepair.status -ceq 'reparented-conflict') 'Autostash pop-conflict did not report the reparented-conflict status.'
	Assert-True (($popRepair.PSObject.Properties.Name -ccontains 'rebaseInProgress') -and (-not $popRepair.rebaseInProgress)) 'Autostash pop-conflict did not report rebaseInProgress false.'
	$popText = Get-OutputText $popRun
	Assert-True ($popText -match 'FIRST TASK') 'Autostash pop-conflict banner omitted the resolve-first instruction.'
	Assert-True ($popText -match 'git stash drop') 'Autostash pop-conflict banner omitted the stash-drop instruction.'
	Assert-True (-not ($popText -match 'git rebase --continue')) 'Autostash pop-conflict banner wrongly told the session to run rebase --continue.'
	Assert-True ($popText -match 'Build-WorktreeDataPacker') 'Autostash pop-conflict banner omitted the deferred DataPacker build instruction.'
	Write-Host 'PASS autostash pop-conflict reports rebaseInProgress false with a stash-drop banner'
	Invoke-Git @('-C', $worktree, 'stash', 'clear') | Out-Null
	Reset-SquashFixtureState

	# (g) [Finding B] Advance-then-squash: a pre-claim primary advance (or manual fast-forward) moves the
	# branch's true fork point above the immutable receipt baseline. The re-parent must derive that fork
	# point from the primary reflog and replay ONLY commits above it - never the squashed-away primary
	# commits the fast-forward pulled onto the branch (rebasing from the stale baseline would resurrect them).
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $primary 'tracked.txt'), 'advanced B1', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $primary, 'commit', '-aqm', 'advance B1') | Out-Null
	$advanceB1 = @(Invoke-Git @('-C', $primary, 'rev-parse', 'HEAD'))[0].Trim()
	# Fast-forward the session worktree onto B1 WITHOUT rewriting the receipt (mirrors the pre-claim advance).
	Invoke-Git @('-C', $worktree, 'reset', '--hard', $advanceB1) | Out-Null
	Assert-True ((Get-ReceiptBaseline) -ceq $baseline) 'Advance-then-squash setup unexpectedly rewrote the receipt baseline.'
	[IO.File]::WriteAllText((Join-Path $worktree 'session-work.txt'), 'session change', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $worktree, 'add', '--', 'session-work.txt') | Out-Null
	Invoke-Git @('-C', $worktree, 'commit', '-qm', 'session work') | Out-Null
	# Squash primary: parentless commit-tree orphans both B0 and B1, mirroring the end-of-day squash.
	$advanceSquashed = New-SquashedPrimary $null
	$advanceRun = & $invokeStart
	Assert-True ($advanceRun.ExitCode -eq 0) "Advance-then-squash reattach failed: $($advanceRun.Output -join '; ')"
	$advanceRepair = Get-RepairResult $advanceRun
	Assert-True ($null -ne $advanceRepair -and $advanceRepair.status -ceq 'reparented') 'Advance-then-squash did not re-parent.'
	Assert-True ($advanceRepair.oldBaseline -ceq $baseline) 'Advance-then-squash oldBaseline is not the immutable receipt baseline.'
	Assert-True ($advanceRepair.forkBound -ceq $advanceB1) 'Advance-then-squash did not derive the fork bound as the fast-forwarded primary tip.'
	Assert-True ((Get-ReceiptBaseline) -ceq $advanceSquashed) 'Advance-then-squash did not rewrite the receipt to the squashed tip.'
	$advanceRevList = @(Invoke-Git @('-C', $worktree, 'rev-list', 'HEAD'))
	Assert-True (-not ($advanceRevList -ccontains $baseline)) 'Advance-then-squash replayed the squashed-away receipt-baseline commit.'
	Assert-True (-not ($advanceRevList -ccontains $advanceB1)) 'Advance-then-squash replayed the squashed-away advanced primary commit B1.'
	$advanceSubjects = @(Invoke-Git @('-C', $worktree, 'log', '--format=%s', 'HEAD'))
	Assert-True (-not ($advanceSubjects -ccontains 'advance B1')) 'Advance-then-squash resurrected the squashed-away primary commit subject.'
	Assert-True ($advanceSubjects -ccontains 'session work') 'Advance-then-squash dropped the session commit.'
	& git -C $worktree merge-base --is-ancestor $advanceSquashed (@(Invoke-Git @('-C', $worktree, 'rev-parse', 'HEAD'))[0].Trim()) *> $null
	Assert-True ($LASTEXITCODE -eq 0) 'Advance-then-squash did not re-parent onto the squashed tip.'
	Write-Host 'PASS advance-then-squash replays only commits above the true fork bound'
	Reset-SquashFixtureState

	# (h) [Finding B converse] Partial squash orphaning B1 but keeping B0: the receipt baseline stays
	# reachable, so the old baseline-ancestor test would wrongly report not-needed while the orphaned primary
	# commit still rides the session branch. The reflog-derived fork bound (B1) is NOT reachable, so repair
	# still triggers and drops the orphaned commit while retaining the kept primary line.
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $primary 'tracked.txt'), 'advanced B1', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $primary, 'commit', '-aqm', 'advance B1') | Out-Null
	$partialB1 = @(Invoke-Git @('-C', $primary, 'rev-parse', 'HEAD'))[0].Trim()
	Invoke-Git @('-C', $worktree, 'reset', '--hard', $partialB1) | Out-Null
	[IO.File]::WriteAllText((Join-Path $worktree 'session-work.txt'), 'session change', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $worktree, 'add', '--', 'session-work.txt') | Out-Null
	Invoke-Git @('-C', $worktree, 'commit', '-qm', 'session work') | Out-Null
	# Partial squash: reset --soft back to B0 and re-commit B1's content, orphaning B1 but keeping B0 reachable.
	Invoke-Git @('-C', $primary, 'reset', '--soft', $baseline) | Out-Null
	Invoke-Git @('-C', $primary, 'commit', '-qm', 'partial squash') | Out-Null
	$partialTip = @(Invoke-Git @('-C', $primary, 'rev-parse', 'HEAD'))[0].Trim()
	& git -C $primary merge-base --is-ancestor $baseline $partialTip *> $null
	Assert-True ($LASTEXITCODE -eq 0) 'Partial-squash setup did not keep the receipt baseline reachable (converse premise).'
	$partialRun = & $invokeStart
	Assert-True ($partialRun.ExitCode -eq 0) "Partial-squash reattach failed: $($partialRun.Output -join '; ')"
	$partialRepair = Get-RepairResult $partialRun
	Assert-True ($null -ne $partialRepair -and $partialRepair.status -ceq 'reparented') 'Partial-squash reported not-needed while an orphaned primary commit remained.'
	Assert-True ($partialRepair.forkBound -ceq $partialB1) 'Partial-squash did not derive the orphaned primary tip as the fork bound.'
	$partialRevList = @(Invoke-Git @('-C', $worktree, 'rev-list', 'HEAD'))
	Assert-True (-not ($partialRevList -ccontains $partialB1)) 'Partial-squash left the orphaned primary commit on the session branch.'
	$partialSubjects = @(Invoke-Git @('-C', $worktree, 'log', '--format=%s', 'HEAD'))
	Assert-True (-not ($partialSubjects -ccontains 'advance B1')) 'Partial-squash resurrected the orphaned primary commit subject.'
	Assert-True ($partialSubjects -ccontains 'session work') 'Partial-squash dropped the session commit.'
	Assert-True ($partialSubjects -ccontains 'partial squash') 'Partial-squash did not re-parent onto the kept primary line.'
	Write-Host 'PASS partial-squash converse re-parents off the orphaned primary commit'
	Reset-SquashFixtureState

	# (i) [Finding A] Crash after a conflicted re-parent, then re-reattach: the wrapper now always runs the
	# sidecar (never gated on a merge-base probe), so an unresolved rebase-conflict left in progress is
	# re-detected on the next reattach - reparented-conflict, FIRST-TASK banner, DataPacker skipped, receipt
	# unchanged - instead of silently building DataPacker on a conflict tree with no banner.
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $worktree 'tracked.txt'), 'session version', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $worktree, 'commit', '-aqm', 'session divergent') | Out-Null
	$reentryNew = New-SquashedPrimary 'squashed version'
	$firstConflict = & $invokeStart
	Assert-True ($firstConflict.ExitCode -eq 0) "Rebase-conflict re-entry setup failed: $($firstConflict.Output -join '; ')"
	Assert-True ((Get-RepairResult $firstConflict).status -ceq 'reparented-conflict') 'Rebase-conflict re-entry setup did not leave a conflict.'
	$reentryRebaseMerge = @(Invoke-Git @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-path', 'rebase-merge'))[0].Trim()
	Assert-True (Test-Path -LiteralPath $reentryRebaseMerge) 'Rebase-conflict re-entry setup left no rebase in progress.'
	# Simulate a session crash: do NOT resolve. Reattach again with the rebase still in progress.
	Remove-Item -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE -Force -ErrorAction SilentlyContinue
	$reentryRun = & $invokeStart
	Assert-True ($reentryRun.ExitCode -eq 0) "Rebase-conflict re-entry reattach failed: $($reentryRun.Output -join '; ')"
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE -PathType Leaf) 'Rebase-conflict re-entry did not launch the client.'
	$reentryRepair = Get-RepairResult $reentryRun
	Assert-True ($null -ne $reentryRepair -and $reentryRepair.status -ceq 'reparented-conflict') 'Rebase-conflict re-entry did not re-report reparented-conflict.'
	Assert-True ($reentryRepair.rebaseInProgress) 'Rebase-conflict re-entry did not report rebaseInProgress true.'
	Assert-True ((Get-ReceiptBaseline) -ceq $reentryNew) 'Rebase-conflict re-entry changed the receipt baseline.'
	Assert-True (-not (Test-Path -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE)) 'Rebase-conflict re-entry did not skip the DataPacker build.'
	$reentryText = Get-OutputText $reentryRun
	Assert-True ($reentryText -match 'FIRST TASK') 'Rebase-conflict re-entry banner omitted the resolve-first instruction.'
	Assert-True ($reentryText -match 'NO scheduler operation') 'Rebase-conflict re-entry banner omitted the no-scheduler-ops instruction.'
	Assert-True ($reentryText -match 'git rebase --continue') 'Rebase-conflict re-entry banner omitted the rebase --continue instruction.'
	Write-Host 'PASS rebase-conflict re-entry re-reports the conflict on a second reattach'
	& git -C $worktree rebase --abort *> $null
	Reset-SquashFixtureState

	# (j) [Finding A] The same must hold for a leftover autostash POP-conflict (rebaseInProgress false): the
	# rebase already completed and the receipt was already rewritten, so a naive not-needed test would swallow
	# it. The sidecar's pop-conflict re-entry detection (unmerged tree + attached HEAD + no rebase + kept
	# autostash) re-reports it, keeping the stash-drop banner and the DataPacker skip on the next reattach.
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $worktree 'tracked.txt'), 'uncommitted local edit', [Text.UTF8Encoding]::new($false))
	$popReentryNew = New-SquashedPrimary 'squashed conflicting content'
	$firstPop = & $invokeStart
	Assert-True ($firstPop.ExitCode -eq 0) "Pop-conflict re-entry setup failed: $($firstPop.Output -join '; ')"
	Assert-True ((Get-RepairResult $firstPop).status -ceq 'reparented-conflict') 'Pop-conflict re-entry setup did not leave a pop-conflict.'
	$popReentryRebaseMerge = @(Invoke-Git @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-path', 'rebase-merge'))[0].Trim()
	Assert-True (-not (Test-Path -LiteralPath $popReentryRebaseMerge)) 'Pop-conflict re-entry setup left a rebase in progress.'
	Assert-True (@(@(Invoke-Git @('-C', $worktree, 'status', '--porcelain')) | Where-Object { $_ -match '^(UU|AA|DD|AU|UA|UD|DU) ' }).Count -ge 1) 'Pop-conflict re-entry setup left no unmerged entries.'
	Assert-True (@(Invoke-Git @('-C', $worktree, 'stash', 'list')).Count -ge 1) 'Pop-conflict re-entry setup kept no autostash.'
	# Simulate a session crash: do NOT resolve. Reattach again.
	Remove-Item -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE -Force -ErrorAction SilentlyContinue
	$popReentryRun = & $invokeStart
	Assert-True ($popReentryRun.ExitCode -eq 0) "Pop-conflict re-entry reattach failed: $($popReentryRun.Output -join '; ')"
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE -PathType Leaf) 'Pop-conflict re-entry did not launch the client.'
	$popReentryRepair = Get-RepairResult $popReentryRun
	Assert-True ($null -ne $popReentryRepair -and $popReentryRepair.status -ceq 'reparented-conflict') 'Pop-conflict re-entry did not re-report reparented-conflict.'
	Assert-True (($popReentryRepair.PSObject.Properties.Name -ccontains 'rebaseInProgress') -and (-not $popReentryRepair.rebaseInProgress)) 'Pop-conflict re-entry did not report rebaseInProgress false.'
	Assert-True ((Get-ReceiptBaseline) -ceq $popReentryNew) 'Pop-conflict re-entry changed the receipt baseline.'
	Assert-True (-not (Test-Path -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE)) 'Pop-conflict re-entry did not skip the DataPacker build.'
	$popReentryText = Get-OutputText $popReentryRun
	Assert-True ($popReentryText -match 'FIRST TASK') 'Pop-conflict re-entry banner omitted the resolve-first instruction.'
	Assert-True ($popReentryText -match 'git stash drop') 'Pop-conflict re-entry banner omitted the stash-drop instruction.'
	Assert-True (-not ($popReentryText -match 'git rebase --continue')) 'Pop-conflict re-entry banner wrongly told the session to run rebase --continue.'
	Write-Host 'PASS pop-conflict re-entry re-reports rebaseInProgress false on a second reattach'
	Invoke-Git @('-C', $worktree, 'stash', 'clear') | Out-Null
	Reset-SquashFixtureState

	# (k) [Re-review] Crash in the window AFTER a completed clean rebase but BEFORE the claim/receipt mutation
	# (reparent-claims threw on a transient scheduler-guard busy, or the process died before the receipt
	# rewrite). Simulate it by performing the rebase --onto by hand and leaving the receipt on the squashed-away
	# baseline. The next reattach must FINISH the mutation (rewrite the receipt to the landed squash tip, status
	# reparented, proof passes) rather than dead-end on the bound-not-ancestor precondition.
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $worktree 'session-work.txt'), 'session change', [Text.UTF8Encoding]::new($false))
	Invoke-Git @('-C', $worktree, 'add', '--', 'session-work.txt') | Out-Null
	Invoke-Git @('-C', $worktree, 'commit', '-qm', 'session work') | Out-Null
	$finishNew = New-SquashedPrimary $null
	# Perform only the rebase the sidecar would run; leave claims/receipt untouched (the interrupted window).
	Invoke-Git @('-C', $worktree, 'rebase', '--onto', $finishNew, $baseline, $sessionBranch) | Out-Null
	Assert-True ((Get-ReceiptBaseline) -ceq $baseline) 'Finish-clean setup unexpectedly rewrote the receipt (window not simulated).'
	& git -C $worktree merge-base --is-ancestor $baseline (@(Invoke-Git @('-C', $worktree, 'rev-parse', 'HEAD'))[0].Trim()) *> $null
	Assert-True ($LASTEXITCODE -ne 0) 'Finish-clean setup left the stale baseline parenting HEAD.'
	Remove-Item -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE -Force -ErrorAction SilentlyContinue
	$finishRun = & $invokeStart
	Assert-True ($finishRun.ExitCode -eq 0) "Finish-clean reattach failed: $($finishRun.Output -join '; ')"
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE -PathType Leaf) 'Finish-clean did not launch the client.'
	$finishRepair = Get-RepairResult $finishRun
	Assert-True ($null -ne $finishRepair -and $finishRepair.status -ceq 'reparented') 'Finish-clean did not complete the interrupted mutation as reparented.'
	Assert-True ($finishRepair.newBaseline -ceq $finishNew) 'Finish-clean did not report the landed squash tip as the new baseline.'
	Assert-True ((Get-ReceiptBaseline) -ceq $finishNew) 'Finish-clean did not rewrite the stale receipt to the landed squash tip.'
	$finishClient = Get-Content -Raw -LiteralPath $env:FIXTURE_CLIENT_CAPTURE | ConvertFrom-Json
	Assert-True ($finishClient.baseline -ceq $finishNew) 'Finish-clean client baseline is not the landed squash tip (proof did not pass).'
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE -PathType Leaf) 'Finish-clean skipped the DataPacker build.'
	Write-Host 'PASS interrupted repair (clean) completes the mutation on the next reattach'
	Reset-SquashFixtureState

	# (l) [Re-review] Same interrupted window but with a leftover autostash POP-conflict tree: the rebase
	# completed (HEAD attached at the squash tip, unmerged tree, kept autostash) yet the mutation never ran.
	# The next reattach must finish the mutation AND keep the reparented-conflict (rebaseInProgress false) shape
	# with the stash-drop banner and DataPacker skip - not re-report "scheduler ops safe" over a stale receipt.
	Reset-SquashFixtureState
	[IO.File]::WriteAllText((Join-Path $worktree 'tracked.txt'), 'uncommitted local edit', [Text.UTF8Encoding]::new($false))
	$finishPopNew = New-SquashedPrimary 'squashed conflicting content'
	# Perform only the autostash rebase the sidecar would run; leave claims/receipt untouched.
	& git -C $worktree rebase --autostash --onto $finishPopNew $baseline $sessionBranch *> $null
	Assert-True ((@(Invoke-Git @('-C', $worktree, 'rev-parse', 'HEAD'))[0].Trim()) -ceq $finishPopNew) 'Finish-pop setup did not land HEAD on the squash tip.'
	Assert-True ((Get-ReceiptBaseline) -ceq $baseline) 'Finish-pop setup unexpectedly rewrote the receipt (window not simulated).'
	$finishPopRebaseMerge = @(Invoke-Git @('-C', $worktree, 'rev-parse', '--path-format=absolute', '--git-path', 'rebase-merge'))[0].Trim()
	Assert-True (-not (Test-Path -LiteralPath $finishPopRebaseMerge)) 'Finish-pop setup left a rebase in progress.'
	Assert-True (@(@(Invoke-Git @('-C', $worktree, 'status', '--porcelain')) | Where-Object { $_ -match '^(UU|AA|DD|AU|UA|UD|DU) ' }).Count -ge 1) 'Finish-pop setup left no unmerged entries.'
	Assert-True (@(Invoke-Git @('-C', $worktree, 'stash', 'list')).Count -ge 1) 'Finish-pop setup kept no autostash.'
	Remove-Item -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE -Force -ErrorAction SilentlyContinue
	$finishPopRun = & $invokeStart
	Assert-True ($finishPopRun.ExitCode -eq 0) "Finish-pop reattach failed: $($finishPopRun.Output -join '; ')"
	Assert-True (Test-Path -LiteralPath $env:FIXTURE_CLIENT_CAPTURE -PathType Leaf) 'Finish-pop did not launch the client.'
	$finishPopRepair = Get-RepairResult $finishPopRun
	Assert-True ($null -ne $finishPopRepair -and $finishPopRepair.status -ceq 'reparented-conflict') 'Finish-pop did not complete the mutation as reparented-conflict.'
	Assert-True (($finishPopRepair.PSObject.Properties.Name -ccontains 'rebaseInProgress') -and (-not $finishPopRepair.rebaseInProgress)) 'Finish-pop did not report rebaseInProgress false.'
	Assert-True ($finishPopRepair.newBaseline -ceq $finishPopNew) 'Finish-pop did not report the landed squash tip as the new baseline.'
	Assert-True ((Get-ReceiptBaseline) -ceq $finishPopNew) 'Finish-pop did not rewrite the stale receipt to the landed squash tip.'
	Assert-True (-not (Test-Path -LiteralPath $env:FIXTURE_DATAPACKER_CAPTURE)) 'Finish-pop did not skip the DataPacker build.'
	$finishPopText = Get-OutputText $finishPopRun
	Assert-True ($finishPopText -match 'FIRST TASK') 'Finish-pop banner omitted the resolve-first instruction.'
	Assert-True ($finishPopText -match 'git stash drop') 'Finish-pop banner omitted the stash-drop instruction.'
	Assert-True (-not ($finishPopText -match 'git rebase --continue')) 'Finish-pop banner wrongly told the session to run rebase --continue.'
	Write-Host 'PASS interrupted repair (pop-conflict) completes the mutation on the next reattach'
	Invoke-Git @('-C', $worktree, 'stash', 'clear') | Out-Null
	Reset-SquashFixtureState

	# Wrapper forwarding uses a fake Start script. No wrapper test can launch a real client.
	$fakeStart = @'
param([string] $Client, [string] $RepositoryRoot, [string[]] $ClientArguments, [string] $ReattachWorktree)
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
