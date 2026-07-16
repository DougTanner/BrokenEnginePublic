param(
	[Parameter(Mandatory = $false)]
	[string]$RepositoryRoot = (Get-Location).Path,

	[Parameter(Mandatory = $false)]
	[string]$ObjectiveLedgerPath,

	[Parameter(Mandatory = $false)]
	[string]$LivePrimaryWorktree,

	[Parameter(Mandatory = $false)]
	[string]$ExecutionControlPath,

	[Parameter(Mandatory = $false)]
	[string]$ExecutionControlSha256
)

$ErrorActionPreference = 'Stop'

function Write-Result {
	param(
		[string]$Status,
		[string[]]$Diagnostics,
		[object]$ObjectiveTerminal = $null,
		[string]$NextAction = 'contract-valid',
		[object]$LiveQueueValidationSha256 = $null,
		[object]$ExecutionControlSha256 = $null
	)

	[ordered]@{
		schema = 'broken-engine-finalize-objective-contract/v1'
		status = $Status
		objectiveTerminal = $ObjectiveTerminal
		nextAction = $NextAction
		liveQueueValidationSha256 = $LiveQueueValidationSha256
		executionControlSha256 = $ExecutionControlSha256
		diagnostics = @($Diagnostics)
	} | ConvertTo-Json -Depth 4 -Compress
}

function Get-ExecutionControl {
	param(
		[string]$Root,
		[string]$ControlPath,
		[string]$ExpectedSha256,
		[Collections.Generic.List[string]]$Diagnostics
	)

	if ([string]::IsNullOrWhiteSpace($ControlPath) -or $ExpectedSha256 -cnotmatch '^[0-9a-f]{64}$') {
		$Diagnostics.Add('Objective decision requires an execution-control path and lowercase SHA-256 from the final verification ledger.')
		return $null
	}
	$path = if ([IO.Path]::IsPathRooted($ControlPath)) { $ControlPath } else { Join-Path $Root $ControlPath }
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		$Diagnostics.Add("Execution-control record is missing: $path")
		return $null
	}
	$bytes = [IO.File]::ReadAllBytes($path)
	$actualSha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
	if ($actualSha256 -cne $ExpectedSha256) {
		$Diagnostics.Add('Execution-control record hash does not match the final verification ledger.')
		return $null
	}
	$control = [Text.Encoding]::UTF8.GetString($bytes) | ConvertFrom-Json
	if ($control.schema -ne 'broken-engine-execution-control/v1') {
		$Diagnostics.Add('Execution-control schema must be broken-engine-execution-control/v1.')
		return $null
	}
	$stages = @($control.stages)
	if ($stages.Count -eq 0) {
		$Diagnostics.Add('Execution-control record must contain at least one stage.')
		return $null
	}
	$ids = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($stage in $stages) {
		$id = [string]$stage.id
		$deliverables = @($stage.deliverables)
		if ([string]::IsNullOrWhiteSpace($id) -or -not $ids.Add($id) -or $deliverables.Count -eq 0) {
			$Diagnostics.Add('Every execution-control stage must have a unique ID and at least one deliverable.')
			continue
		}
		$deliverableIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
		foreach ($deliverable in $deliverables) {
			if ([string]::IsNullOrWhiteSpace([string]$deliverable) -or -not $deliverableIds.Add([string]$deliverable)) {
				$Diagnostics.Add("Execution-control stage '$id' has a blank or duplicate deliverable.")
			}
		}
	}
	if ($Diagnostics.Count -gt 0) { return $null }
	return [pscustomobject]@{ Record = $control; Sha256 = $actualSha256 }
}

function Get-LiveQueueEvidence {
	param(
		[string]$Root,
		[string]$PrimaryWorktree,
		[Collections.Generic.List[string]]$Diagnostics
	)

	if ([string]::IsNullOrWhiteSpace($PrimaryWorktree)) {
		return $null
	}
	$primary = [IO.Path]::GetFullPath($PrimaryWorktree).TrimEnd('\', '/')
	$primaryGit = Join-Path $primary '.git'
	if (-not (Test-Path -LiteralPath $primaryGit -PathType Container)) {
		$Diagnostics.Add("Live queue worktree is not the primary checkout: $primary")
		return $null
	}
	$commonOutput = @(& git -C $primary rev-parse --path-format=absolute --git-common-dir 2>$null)
	$commonExit = $LASTEXITCODE
	$commonDir = [string]$commonOutput[0]
	if ($commonExit -ne 0 -or [string]::IsNullOrWhiteSpace($commonDir) -or [IO.Path]::GetFullPath($commonDir).TrimEnd('\', '/') -ne [IO.Path]::GetFullPath($primaryGit).TrimEnd('\', '/')) {
		$Diagnostics.Add("Live queue Git common directory does not match primary: $primary")
		return $null
	}
	$rootCommonOutput = @(& git -C $Root rev-parse --path-format=absolute --git-common-dir 2>$null)
	$rootCommonExit = $LASTEXITCODE
	$rootCommonDir = [string]$rootCommonOutput[0]
	if ($rootCommonExit -ne 0 -or [string]::IsNullOrWhiteSpace($rootCommonDir) -or [IO.Path]::GetFullPath($rootCommonDir).TrimEnd('\', '/') -ne [IO.Path]::GetFullPath($commonDir).TrimEnd('\', '/')) {
		$Diagnostics.Add("Live queue primary is from a different repository: $primary")
		return $null
	}
	$status = @(& git -C $primary status --porcelain=v1 --untracked-files=all)
	if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0) {
		$Diagnostics.Add("Live queue primary is not clean: $primary")
		return $null
	}
	$headOutput = @(& git -C $primary rev-parse HEAD 2>$null)
	$headExit = $LASTEXITCODE
	$primaryHead = [string]$headOutput[0]
	if ($headExit -ne 0 -or $primaryHead -notmatch '^[0-9a-f]{40}$') {
		$Diagnostics.Add("Cannot resolve live queue primary commit: $primary")
		return $null
	}

	$worktreeCli = Join-Path $Root 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	if (-not (Test-Path -LiteralPath $worktreeCli -PathType Leaf)) {
		$Diagnostics.Add("WorktreeCli is missing: $worktreeCli")
		return $null
	}
	$validationOutput = @(& $worktreeCli plan order validate --repo $commonDir --worktree $primary 2>&1)
	$validationExit = $LASTEXITCODE
	$validationText = [string]::Join("`n", @($validationOutput | ForEach-Object { $_.ToString() }))
	if ($validationExit -ne 0) {
		$Diagnostics.Add("Live primary WorktreeCli validation failed with exit $validationExit.")
		return $null
	}
	$validation = $validationText | ConvertFrom-Json
	if ($validation.ok -ne $true -or @($validation.diagnostics).Count -ne 0) {
		$Diagnostics.Add('Live primary WorktreeCli validation did not report ok with zero diagnostics.')
		return $null
	}
	$validationHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($validationText))).ToLowerInvariant()
	return [pscustomobject]@{
		PrimaryHead = $primaryHead
		Rows = @($validation.rows)
		ValidationSha256 = $validationHash
	}
}

function Get-ObjectiveDecision {
	param(
		[string]$LedgerPath,
		[object]$ExecutionControl,
		[object]$LiveQueueEvidence,
		[Collections.Generic.List[string]]$Diagnostics
	)

	if (-not (Test-Path -LiteralPath $LedgerPath -PathType Leaf)) {
		$Diagnostics.Add("Objective ledger is missing: $LedgerPath")
		return $false
	}
	$ledger = Get-Content -LiteralPath $LedgerPath -Raw | ConvertFrom-Json
	if ($ledger.schema -ne 'broken-engine-objective-ledger/v1') {
		$Diagnostics.Add('Objective ledger schema must be broken-engine-objective-ledger/v1.')
		return $false
	}
	$stages = @($ledger.stages)
	if ($stages.Count -eq 0) {
		$Diagnostics.Add('Objective ledger must contain at least one stage.')
		return $false
	}
	$controlStages = @($ExecutionControl.Record.stages)
	$ledgerIds = @($stages | ForEach-Object { [string]$_.id } | Sort-Object -CaseSensitive)
	$controlIds = @($controlStages | ForEach-Object { [string]$_.id } | Sort-Object -CaseSensitive)
	if ($ledgerIds.Count -ne $controlIds.Count -or [string]::Join("`n", $ledgerIds) -cne [string]::Join("`n", $controlIds)) {
		$Diagnostics.Add('Objective ledger stage set does not match the approved execution-control record.')
		return $false
	}

	$ids = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	$terminal = $true
	foreach ($stage in $stages) {
		$id = [string]$stage.id
		$disposition = [string]$stage.disposition
		$controlStage = @($controlStages | Where-Object { [string]$_.id -ceq $id })[0]
		if ([string]::IsNullOrWhiteSpace($id) -or -not $ids.Add($id)) {
			$Diagnostics.Add('Every objective stage must have a unique nonempty ID.')
			continue
		}
		if ([string]::IsNullOrWhiteSpace($disposition)) {
			$Diagnostics.Add("Objective stage '$id' has no disposition.")
			continue
		}
		$ledgerDeliverables = @($stage.deliverables | ForEach-Object { [string]$_ } | Sort-Object -CaseSensitive)
		$controlDeliverables = @($controlStage.deliverables | ForEach-Object { [string]$_ } | Sort-Object -CaseSensitive)
		if ($ledgerDeliverables.Count -ne $controlDeliverables.Count -or [string]::Join("`n", $ledgerDeliverables) -cne [string]::Join("`n", $controlDeliverables)) {
			$Diagnostics.Add("Objective stage '$id' deliverables do not match the approved execution-control record.")
			continue
		}
		if ($disposition -eq 'complete') {
			continue
		}
		if ($disposition -ne 'deferred') {
			$terminal = $false
			continue
		}

		$receipt = $stage.liveQueueReceipt
		$approvedQueue = [string]$stage.approvedQueue
		$approvedPlan = [string]$stage.approvedPlan
		$matchingRows = if ($null -eq $LiveQueueEvidence -or $null -eq $receipt) { @() } else {
			@($LiveQueueEvidence.Rows | Where-Object {
				$_.queue -ceq [string]$receipt.queue -and
				$_.plan -ceq [string]$receipt.plan -and
				$_.rowSha256 -ceq [string]$receipt.rowSha256
			})
		}
		$verifiedDeferred = $stage.userDeferred -is [bool] -and $stage.userDeferred -eq $true -and
			$null -ne $LiveQueueEvidence -and $null -ne $receipt -and
			$approvedQueue -ceq [string]$controlStage.deferredQueue -and
			$approvedPlan -ceq [string]$controlStage.deferredPlan -and
			-not [string]::IsNullOrWhiteSpace($approvedQueue) -and
			-not [string]::IsNullOrWhiteSpace($approvedPlan) -and
			([string]$receipt.queue) -ceq $approvedQueue -and
			([string]$receipt.plan) -ceq $approvedPlan -and
			-not [string]::IsNullOrWhiteSpace([string]$receipt.queue) -and
			-not [string]::IsNullOrWhiteSpace([string]$receipt.plan) -and
			([string]$receipt.rowSha256) -match '^[0-9a-f]{64}$' -and
			([string]$receipt.primaryHead) -ceq $LiveQueueEvidence.PrimaryHead -and
			$matchingRows.Count -eq 1
		if (-not $verifiedDeferred) {
			$terminal = $false
		}
	}
	if ($Diagnostics.Count -gt 0) {
		return $false
	}
	return $terminal
}

try {
	$root = [IO.Path]::GetFullPath($RepositoryRoot)
	$rootInstructions = Join-Path $root 'AGENTS.md'
	$finalizeSkill = Join-Path $root '.agents\skills\finalize-changes\SKILL.md'
	$reportingReference = Join-Path $root '.agents\references\subagent-reporting.md'
	$requiredFiles = @($rootInstructions, $finalizeSkill, $reportingReference)
	$diagnostics = [Collections.Generic.List[string]]::new()

	foreach ($path in $requiredFiles) {
		if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
			$diagnostics.Add("Missing contract file: $path")
		}
	}

	if ($diagnostics.Count -eq 0) {
		$rootText = [regex]::Replace((Get-Content -LiteralPath $rootInstructions -Raw), '\s+', ' ')
		$finalizeText = [regex]::Replace((Get-Content -LiteralPath $finalizeSkill -Raw), '\s+', ' ')
		$requiredAnchors = @(
			@($rootText, 'complete user objective, every approved stage and deliverable'),
			@($rootText, 'For a final-evidence gate, persist this execution control before implementation and bind its hash in the final ledger.'),
			@($rootText, 'A passing acceptance matrix completes only the current stage.'),
			@($rootText, 'The session is objective-terminal only when every stage is complete or each unfinished stage was explicitly deferred by the user and has a verified receipt in the live plan queue.'),
			@($finalizeText, 'Repository success completes only the current stage.'),
			@($finalizeText, 'each unfinished stage was explicitly deferred by the user and has a verified receipt in the live WorktreeCli plan queue.'),
			@($finalizeText, 'If another stage remains active, continue it in this session; if its next action needs approval, present that gate.'),
			@($finalizeText, 'The script verifies the prior record hash, exact stage/deliverable sets, and approved deferral identities')
		)
		foreach ($anchor in $requiredAnchors) {
			if (-not $anchor[0].Contains($anchor[1], [StringComparison]::Ordinal)) {
				$diagnostics.Add("Missing objective-continuity anchor: $($anchor[1])")
			}
		}

		$retiredWords = @(
			('compac' + 'tion'),
			('compac' + 'ted'),
			('agent-lifecycle' + '-handoff'),
			('be-agent-lifecycle' + '-handoff')
		)
		$policyFiles = @($rootInstructions)
		$policyFiles += Get-ChildItem -LiteralPath (Join-Path $root '.agents') -Recurse -File |
			Where-Object { $_.Extension -in @('.md', '.txt') } |
			Select-Object -ExpandProperty FullName
		foreach ($path in $policyFiles) {
			$text = Get-Content -LiteralPath $path -Raw
			foreach ($word in $retiredWords) {
				if ($text.Contains($word, [StringComparison]::OrdinalIgnoreCase)) {
					$diagnostics.Add("Retired lifecycle policy remains in: $path")
					break
				}
			}
		}

		$retiredReference = Join-Path $root ('.agents\references\agent-lifecycle' + '-handoff.md')
		if (Test-Path -LiteralPath $retiredReference) {
			$diagnostics.Add("Retired lifecycle reference still exists: $retiredReference")
		}
	}

	if ($diagnostics.Count -gt 0) {
		Write-Result -Status 'block' -Diagnostics $diagnostics -NextAction 'repair-contract'
		exit 2
	}

	if (-not [string]::IsNullOrWhiteSpace($ObjectiveLedgerPath)) {
		$ledgerPath = if ([IO.Path]::IsPathRooted($ObjectiveLedgerPath)) { $ObjectiveLedgerPath } else { Join-Path $root $ObjectiveLedgerPath }
		$executionControl = Get-ExecutionControl -Root $root -ControlPath $ExecutionControlPath -ExpectedSha256 $ExecutionControlSha256 -Diagnostics $diagnostics
		$liveQueueEvidence = Get-LiveQueueEvidence -Root $root -PrimaryWorktree $LivePrimaryWorktree -Diagnostics $diagnostics
		$objectiveTerminal = if ($null -eq $executionControl) { $false } else { Get-ObjectiveDecision -LedgerPath $ledgerPath -ExecutionControl $executionControl -LiveQueueEvidence $liveQueueEvidence -Diagnostics $diagnostics }
		if ($diagnostics.Count -gt 0) {
			Write-Result -Status 'block' -Diagnostics $diagnostics -ObjectiveTerminal $false -NextAction 'repair-objective-ledger'
			exit 2
		}
		$nextAction = if ($objectiveTerminal) { 'session-complete' } else { 'continue-objective' }
		$validationHash = if ($null -eq $liveQueueEvidence) { $null } else { $liveQueueEvidence.ValidationSha256 }
		Write-Result -Status 'pass' -Diagnostics @() -ObjectiveTerminal $objectiveTerminal -NextAction $nextAction -LiveQueueValidationSha256 $validationHash -ExecutionControlSha256 $executionControl.Sha256
		exit 0
	}

	Write-Result -Status 'pass' -Diagnostics @()
	exit 0
}
catch {
	Write-Result -Status 'error' -Diagnostics @($_.Exception.Message) -NextAction 'repair-invocation'
	exit 1
}
