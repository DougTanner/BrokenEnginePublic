$ErrorActionPreference = 'Stop'

$contractScript = Join-Path $PSScriptRoot 'Test-FinalizeObjectiveContract.ps1'
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("BrokenEngineObjectiveContract-" + [guid]::NewGuid().ToString('N'))

function Invoke-Contract {
	param(
		[string]$Root,
		[string]$LedgerPath,
		[string]$LivePrimaryWorktree,
		[string]$ExecutionControlPath,
		[string]$ExecutionControlSha256
	)
	$arguments = @('-NoProfile', '-File', $contractScript, '-RepositoryRoot', $Root)
	if (-not [string]::IsNullOrWhiteSpace($LedgerPath)) { $arguments += @('-ObjectiveLedgerPath', $LedgerPath) }
	if (-not [string]::IsNullOrWhiteSpace($LivePrimaryWorktree)) { $arguments += @('-LivePrimaryWorktree', $LivePrimaryWorktree) }
	if (-not [string]::IsNullOrWhiteSpace($ExecutionControlPath)) { $arguments += @('-ExecutionControlPath', $ExecutionControlPath) }
	if (-not [string]::IsNullOrWhiteSpace($ExecutionControlSha256)) { $arguments += @('-ExecutionControlSha256', $ExecutionControlSha256) }
	$output = & pwsh @arguments
	return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Result = ($output | ConvertFrom-Json) }
}

try {
	New-Item -ItemType Directory -Path (Join-Path $fixtureRoot '.agents\skills\finalize-changes') -Force | Out-Null
	New-Item -ItemType Directory -Path (Join-Path $fixtureRoot '.agents\references') -Force | Out-Null
	$repositoryOutput = @(& git -C $PSScriptRoot rev-parse --show-toplevel)
	$repositoryExit = $LASTEXITCODE
	$repositoryRoot = [string]$repositoryOutput[0]
	if ($repositoryExit -ne 0 -or [string]::IsNullOrWhiteSpace($repositoryRoot)) { throw 'Cannot resolve fixture repository root.' }
	$sourceWorktreeCli = Join-Path $repositoryRoot 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	$rootContract = @'
The manager records the complete user objective, every approved stage and deliverable with its disposition.
For a final-evidence gate, persist this execution control before implementation and bind its hash in the final ledger.
A passing acceptance matrix completes only the current stage.
The session is objective-terminal only when every stage is complete or each unfinished stage was explicitly deferred by the user and has a verified receipt in the live plan queue.
'@
	$finalizeContract = @'
Repository success completes only the current stage.
The session is terminal when each unfinished stage was explicitly deferred by the user and has a verified receipt in the live WorktreeCli plan queue.
If another stage remains active, continue it in this session; if its next action needs approval, present that gate.
The script verifies the prior record hash, exact stage/deliverable sets, and approved deferral identities.
'@
	Set-Content -LiteralPath (Join-Path $fixtureRoot 'AGENTS.md') -Value $rootContract -NoNewline
	Set-Content -LiteralPath (Join-Path $fixtureRoot '.agents\skills\finalize-changes\SKILL.md') -Value $finalizeContract -NoNewline
	Set-Content -LiteralPath (Join-Path $fixtureRoot '.agents\references\subagent-reporting.md') -Value 'Inline handoffs only.' -NoNewline
	$baseControl = @{
		schema = 'broken-engine-execution-control/v1'
		stages = @(
			@{ id = 'stage-a'; deliverables = @('policy') },
			@{ id = 'stage-b'; deliverables = @('queued-plan'); deferredQueue = 'Documents/Plans/Order.md'; deferredPlan = 'Documents/Plans/Tools/StructuredWorktreeCliBuildResults.md' }
		)
	}
	$baseControlPath = Join-Path $fixtureRoot 'execution-control.json'
	$baseControl | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $baseControlPath
	$baseControlSha256 = (Get-FileHash -LiteralPath $baseControlPath -Algorithm SHA256).Hash.ToLowerInvariant()

	$pass = Invoke-Contract -Root $fixtureRoot
	if ($pass.ExitCode -ne 0 -or $pass.Result.status -ne 'pass') { throw 'Valid objective contract fixture did not pass.' }

	Set-Content -LiteralPath (Join-Path $fixtureRoot 'AGENTS.md') -Value ($rootContract -replace 'A passing acceptance matrix completes only the current stage\.', '') -NoNewline
	$missingAnchor = Invoke-Contract -Root $fixtureRoot
	if ($missingAnchor.ExitCode -ne 2 -or $missingAnchor.Result.status -ne 'block') { throw 'Missing-anchor fixture did not block.' }

	Set-Content -LiteralPath (Join-Path $fixtureRoot 'AGENTS.md') -Value ($rootContract + "`n" + ('compac' + 'tion')) -NoNewline
	$retiredPolicy = Invoke-Contract -Root $fixtureRoot
	if ($retiredPolicy.ExitCode -ne 2 -or $retiredPolicy.Result.status -ne 'block') { throw 'Retired-policy fixture did not block.' }
	Set-Content -LiteralPath (Join-Path $fixtureRoot 'AGENTS.md') -Value $rootContract -NoNewline

	$completeLedger = @{
		schema = 'broken-engine-objective-ledger/v1'
		stages = @(@{ id = 'stage-a'; deliverables = @('policy'); disposition = 'complete' }, @{ id = 'stage-b'; deliverables = @('queued-plan'); disposition = 'complete' })
	}
	$completeLedger | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $fixtureRoot 'complete.json')
	$complete = Invoke-Contract -Root $fixtureRoot -LedgerPath 'complete.json' -ExecutionControlPath $baseControlPath -ExecutionControlSha256 $baseControlSha256
	if ($complete.ExitCode -ne 0 -or $complete.Result.objectiveTerminal -ne $true -or $complete.Result.nextAction -ne 'session-complete') { throw 'All-complete fixture was not terminal.' }
	$omittedLedger = @{ schema = 'broken-engine-objective-ledger/v1'; stages = @(@{ id = 'stage-a'; deliverables = @('policy'); disposition = 'complete' }) }
	$omittedPath = Join-Path $fixtureRoot 'omitted.json'
	$omittedLedger | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $omittedPath
	$omitted = Invoke-Contract -Root $fixtureRoot -LedgerPath $omittedPath -ExecutionControlPath $baseControlPath -ExecutionControlSha256 $baseControlSha256
	if ($omitted.ExitCode -ne 2 -or $omitted.Result.status -ne 'block' -or $omitted.Result.objectiveTerminal -ne $false) { throw 'Omitted-stage fixture did not block.' }

	$activeLedger = @{
		schema = 'broken-engine-objective-ledger/v1'
		stages = @(@{ id = 'stage-a'; deliverables = @('policy'); disposition = 'complete' }, @{ id = 'stage-b'; deliverables = @('queued-plan'); disposition = 'active' })
	}
	$activeLedger | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $fixtureRoot 'active.json')
	$active = Invoke-Contract -Root $fixtureRoot -LedgerPath 'active.json' -ExecutionControlPath $baseControlPath -ExecutionControlSha256 $baseControlSha256
	if ($active.ExitCode -ne 0 -or $active.Result.objectiveTerminal -ne $false -or $active.Result.nextAction -ne 'continue-objective') { throw 'Active-stage fixture did not remain nonterminal.' }

	$commonOutput = @(& git -C $repositoryRoot rev-parse --path-format=absolute --git-common-dir)
	$commonDir = [string]$commonOutput[0]
	$primaryWorktree = Split-Path -Parent $commonDir
	$headOutput = @(& git -C $primaryWorktree rev-parse HEAD)
	$primaryHead = [string]$headOutput[0]
	$validationOutput = @(& $sourceWorktreeCli plan order validate --repo $commonDir --worktree $primaryWorktree)
	if ($LASTEXITCODE -ne 0) { throw 'Cannot validate live primary for objective fixture.' }
	$validation = [string]::Join("`n", @($validationOutput | ForEach-Object { $_.ToString() })) | ConvertFrom-Json
	$liveRow = @($validation.rows)[0]
	$unverifiedLedger = @{
		schema = 'broken-engine-objective-ledger/v1'
		stages = @(@{ id = 'stage-a'; deliverables = @('policy'); disposition = 'complete' }, @{ id = 'stage-b'; deliverables = @('queued-plan'); disposition = 'deferred'; userDeferred = $true
			approvedQueue = 'Documents/Plans/Order.md'
			approvedPlan = 'Documents/Plans/Tools/StructuredWorktreeCliBuildResults.md'
			liveQueueReceipt = @{
			queue = $liveRow.queue
			plan = $liveRow.plan
			rowSha256 = $liveRow.rowSha256
			primaryHead = $primaryHead
		} })
	}
	$unverifiedPath = Join-Path $fixtureRoot 'unverified.json'
	$unverifiedLedger | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $unverifiedPath
	$unverified = Invoke-Contract -Root $repositoryRoot -LedgerPath $unverifiedPath -LivePrimaryWorktree $primaryWorktree -ExecutionControlPath $baseControlPath -ExecutionControlSha256 $baseControlSha256
	if ($unverified.ExitCode -ne 0 -or $unverified.Result.objectiveTerminal -ne $false -or $unverified.Result.nextAction -ne 'continue-objective') { throw "Unverified deferral fixture did not remain nonterminal: $($unverified | ConvertTo-Json -Depth 6 -Compress)" }
	$foreignPrimary = Join-Path $fixtureRoot 'foreign-primary'
	& git init --quiet $foreignPrimary
	if ($LASTEXITCODE -ne 0) { throw 'Cannot create foreign-primary fixture.' }
	$foreign = Invoke-Contract -Root $repositoryRoot -LedgerPath $unverifiedPath -LivePrimaryWorktree $foreignPrimary -ExecutionControlPath $baseControlPath -ExecutionControlSha256 $baseControlSha256
	if ($foreign.ExitCode -ne 2 -or $foreign.Result.status -ne 'block' -or $foreign.Result.objectiveTerminal -ne $false) { throw 'Foreign-primary fixture did not block.' }

	$verifiedLedger = @{
		schema = 'broken-engine-objective-ledger/v1'
		stages = @(
			@{ id = 'stage-a'; deliverables = @('policy'); disposition = 'complete' },
			@{ id = 'live-plan'; deliverables = @('queue-receipt'); disposition = 'deferred'; userDeferred = $true
				approvedQueue = $liveRow.queue
				approvedPlan = $liveRow.plan
				liveQueueReceipt = @{
				queue = $liveRow.queue
				plan = $liveRow.plan
				rowSha256 = $liveRow.rowSha256
				primaryHead = $primaryHead
			} }
		)
	}
	$verifiedControl = @{
		schema = 'broken-engine-execution-control/v1'
		stages = @(
			@{ id = 'stage-a'; deliverables = @('policy') },
			@{ id = 'live-plan'; deliverables = @('queue-receipt'); deferredQueue = $liveRow.queue; deferredPlan = $liveRow.plan }
		)
	}
	$verifiedControlPath = Join-Path $fixtureRoot 'verified-control.json'
	$verifiedControl | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $verifiedControlPath
	$verifiedControlSha256 = (Get-FileHash -LiteralPath $verifiedControlPath -Algorithm SHA256).Hash.ToLowerInvariant()
	$verifiedPath = Join-Path $fixtureRoot 'verified.json'
	$verifiedLedger | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $verifiedPath
	$verified = Invoke-Contract -Root $repositoryRoot -LedgerPath $verifiedPath -LivePrimaryWorktree $primaryWorktree -ExecutionControlPath $verifiedControlPath -ExecutionControlSha256 $verifiedControlSha256
	if ($verified.ExitCode -ne 0 -or $verified.Result.objectiveTerminal -ne $true -or $verified.Result.nextAction -ne 'session-complete' -or $verified.Result.liveQueueValidationSha256 -notmatch '^[0-9a-f]{64}$' -or $verified.Result.executionControlSha256 -cne $verifiedControlSha256) { throw 'Verified live-queue deferral fixture was not terminal.' }

	[ordered]@{
		schema = 'broken-engine-finalize-objective-contract-fixtures/v1'
		status = 'pass'
		cases = @('valid-policy', 'missing-objective-anchor', 'retired-policy', 'all-complete-terminal', 'omitted-stage-blocked', 'active-stage-continuing', 'unverified-deferral-continuing', 'foreign-primary-blocked', 'verified-live-deferral-terminal')
	} | ConvertTo-Json -Depth 3 -Compress
	exit 0
}
finally {
	if (Test-Path -LiteralPath $fixtureRoot) {
		Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
	}
}
