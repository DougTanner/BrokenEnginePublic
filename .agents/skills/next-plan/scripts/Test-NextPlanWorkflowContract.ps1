[CmdletBinding()]
param(
	[string] $RepositoryRoot
)

$ErrorActionPreference = 'Stop'

function Write-Result
{
	param(
		[string] $Status,
		[string] $Code,
		[System.Collections.Generic.List[object]] $Diagnostics
	)

	[pscustomobject] @{
		schemaVersion = 'broken-engine-next-plan-contract-validation/v1'
		status = $Status
		code = $Code
		diagnostics = @($Diagnostics)
	} | ConvertTo-Json -Depth 5
}

try
{
	if ([string]::IsNullOrWhiteSpace($RepositoryRoot))
	{
		$RepositoryRoot = Join-Path (Split-Path -Parent $PSCommandPath) '../../../..'
	}
	$RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot)

	$contractRelative = '.agents/skills/next-plan/references/execution-gates.md'
	$contractPath = Join-Path $RepositoryRoot $contractRelative
	$receiptChainRelative = '.agents/skills/next-plan/scripts/Test-NextPlanReceiptChain.ps1'
	$presentationRelative = '.agents/skills/next-plan/scripts/New-NextPlanPresentation.ps1'
	$approvalRelative = '.agents/skills/next-plan/scripts/Confirm-NextPlanApproval.ps1'
	$completionRelative = '.agents/skills/next-plan/scripts/Complete-NextPlan.ps1'
	$consumers = [ordered] @{
		'.agents/skills/next-plan/SKILL.md' = 'references/execution-gates.md'
		'.agents/skills/next-plan/references/tier3-workflow.md' = 'execution-gates.md'
		'.agents/skills/plan-audit/SKILL.md' = '../next-plan/references/execution-gates.md'
		'.agents/skills/verify-changes/SKILL.md' = '../next-plan/references/execution-gates.md'
		'.agents/skills/finalize-changes/SKILL.md' = '../next-plan/references/execution-gates.md'
		'Documents/Plans/AGENTS.md' = '../../.agents/skills/next-plan/references/execution-gates.md'
	}
	$requiredAnchors = [ordered] @{
		'.agents/skills/next-plan/SKILL.md' = 'Persist the approval receipt before editing code.'
		'.agents/skills/next-plan/references/tier3-workflow.md' = "the contract's continuous-execution state"
		'.agents/skills/plan-audit/SKILL.md' = 'findings-only work and does not create an approval gate.'
		'.agents/skills/verify-changes/SKILL.md' = 'A completed `/next-plan` ledger must record the validator''s PASS result'
		'.agents/skills/finalize-changes/SKILL.md' = 'Confirm commit of verified manifest <manifest-sha256> on primary branch <primary-branch> at <primary-tip>?'
		'Documents/Plans/AGENTS.md' = 'only the exact primary-mutation summary can authorize primary history to change'
	}
	$receiptChainAnchors = [ordered] @{
		'.agents/skills/next-plan/SKILL.md' = "completion receipt path/hash and the validator's exact result into"
		'.agents/skills/verify-changes/SKILL.md' = 'Use only the validator-returned `presentationReceipt`,'
		'.agents/skills/finalize-changes/SKILL.md' = 'Require the validated `/next-plan` mode to be'
	}
	$diagnostics = [System.Collections.Generic.List[object]]::new()

	$requiredFiles = @($contractRelative, $receiptChainRelative, $presentationRelative, $approvalRelative, $completionRelative) + @($consumers.Keys)
	foreach ($relativePath in $requiredFiles)
	{
		if (-not [System.IO.File]::Exists((Join-Path $RepositoryRoot $relativePath)))
		{
			$diagnostics.Add([pscustomobject] @{ path = $relativePath; code = 'FILE_MISSING'; message = 'required workflow file does not exist' })
		}
	}

	if ($diagnostics.Count -eq 0)
	{
		$contractText = [System.IO.File]::ReadAllText($contractPath)
		foreach ($marker in @(
			'<!-- next-plan-gate-contract:v1 -->',
			'<!-- next-plan-gate:implementation-approval -->',
			'<!-- next-plan-gate:continuous-execution -->',
			'<!-- next-plan-gate:receipt-chain -->',
			'<!-- next-plan-gate:primary-mutation-confirmation -->'))
		{
			if (-not $contractText.Contains($marker, [System.StringComparison]::Ordinal))
			{
				$diagnostics.Add([pscustomobject] @{ path = $contractRelative; code = 'CONTRACT_MARKER_MISSING'; message = "missing contract marker: $marker" })
			}
		}

		foreach ($anchor in $receiptChainAnchors.GetEnumerator())
		{
			$text = [System.IO.File]::ReadAllText((Join-Path $RepositoryRoot $anchor.Key))
			if (-not $text.Contains($anchor.Value, [System.StringComparison]::Ordinal))
			{
				$diagnostics.Add([pscustomobject] @{ path = $anchor.Key; code = 'RECEIPT_CHAIN_CONTRACT_MISSING'; message = 'required next-plan receipt-chain binding is missing' })
			}
		}
		$finalizeText = [System.IO.File]::ReadAllText((Join-Path $RepositoryRoot '.agents/skills/finalize-changes/SKILL.md'))
		if (-not $finalizeText.Contains('preserve the caller-supplied mode contract and do not require a receipt chain.', [System.StringComparison]::Ordinal))
		{
			$diagnostics.Add([pscustomobject] @{ path = '.agents/skills/finalize-changes/SKILL.md'; code = 'NON_NEXT_PLAN_MODE_CONTRACT_MISSING'; message = 'non-next-plan finalization-mode behavior is not preserved' })
		}
		$nextPlanText = [System.IO.File]::ReadAllText((Join-Path $RepositoryRoot '.agents/skills/next-plan/SKILL.md'))
		if (-not $nextPlanText.Contains('worktree''s ignored `Temp/` directory, and `-FinalizationMode session-landing`.', [System.StringComparison]::Ordinal))
		{
			$diagnostics.Add([pscustomobject] @{ path = '.agents/skills/next-plan/SKILL.md'; code = 'NEXT_PLAN_PRESENTATION_INPUT_CONTRACT_MISSING'; message = 'next-plan execution-card Temp or session-landing contract is missing' })
		}
		if (-not $contractText.Contains('The `/next-plan` finalization mode is always `session-landing`.', [System.StringComparison]::Ordinal))
		{
			$diagnostics.Add([pscustomobject] @{ path = $contractRelative; code = 'NEXT_PLAN_MODE_CONTRACT_MISSING'; message = 'canonical next-plan mode is not fixed to session-landing' })
		}

		$presentationText = [System.IO.File]::ReadAllText((Join-Path $RepositoryRoot $presentationRelative))
		foreach ($requiredText in @("FinalizationMode -cne 'session-landing'", 'Assert-NextPlanTempPath'))
		{
			if (-not $presentationText.Contains($requiredText, [System.StringComparison]::Ordinal))
			{
				$diagnostics.Add([pscustomobject] @{ path = $presentationRelative; code = 'PRESENTATION_INPUT_ENFORCEMENT_MISSING'; message = "missing presentation enforcement token: $requiredText" })
			}
		}
		foreach ($relativePath in @($approvalRelative, $completionRelative, $receiptChainRelative))
		{
			$sidecarText = [System.IO.File]::ReadAllText((Join-Path $RepositoryRoot $relativePath))
			foreach ($requiredText in @("finalizationMode -cne 'session-landing'", 'Assert-NextPlanTempPath'))
			{
				if (-not $sidecarText.Contains($requiredText, [System.StringComparison]::Ordinal))
				{
					$diagnostics.Add([pscustomobject] @{ path = $relativePath; code = 'NEXT_PLAN_MODE_OR_CARD_ENFORCEMENT_MISSING'; message = "missing next-plan mode/card enforcement token: $requiredText" })
				}
			}
		}

		$receiptChainText = [System.IO.File]::ReadAllText((Join-Path $RepositoryRoot $receiptChainRelative))
		foreach ($requiredText in @(
			'CompletionReceiptPath',
			'CompletionReceiptSha256',
			'broken-engine-next-plan-receipt-chain-result/v1',
			'finalizationMode',
			'workflowTerminal',
			'nextAction',
			'completionReceipt',
			'approvalReceipt',
			'presentationReceipt'))
		{
			if (-not $receiptChainText.Contains($requiredText, [System.StringComparison]::Ordinal))
			{
				$diagnostics.Add([pscustomobject] @{ path = $receiptChainRelative; code = 'RECEIPT_CHAIN_VALIDATOR_CONTRACT_MISSING'; message = "missing receipt-chain validator contract token: $requiredText" })
			}
		}

		foreach ($consumer in $consumers.GetEnumerator())
		{
			$consumerPath = Join-Path $RepositoryRoot $consumer.Key
			$consumerText = [System.IO.File]::ReadAllText($consumerPath)
			$escapedTarget = [regex]::Escape($consumer.Value)
			if ($consumerText -notmatch "\[[^\]]+\]\($escapedTarget\)")
			{
				$diagnostics.Add([pscustomobject] @{ path = $consumer.Key; code = 'CONTRACT_LINK_MISSING'; message = "missing canonical contract link: $($consumer.Value)" })
				continue
			}

			$resolvedTarget = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $consumerPath) $consumer.Value))
			if (-not [System.IO.File]::Exists($resolvedTarget) -or $resolvedTarget -cne [System.IO.Path]::GetFullPath($contractPath))
			{
				$diagnostics.Add([pscustomobject] @{ path = $consumer.Key; code = 'CONTRACT_LINK_INVALID'; message = 'canonical contract link does not resolve to the contract file' })
			}
			if (-not $consumerText.Contains($requiredAnchors[$consumer.Key], [System.StringComparison]::Ordinal))
			{
				$diagnostics.Add([pscustomobject] @{ path = $consumer.Key; code = 'CONTRACT_ANCHOR_MISSING'; message = 'required route-specific gate anchor is missing' })
			}
		}

		$stalePatterns = [ordered] @{
			'APPROVE_CLAIM' = '(?i)\bapprove(?: the)? claim\b'
			'REQUEST_LANDING_PREPARATION' = '(?i)\brequest landing preparation\b'
			'READY_FOR_LANDING_PREPARATION' = '(?i)\bready for landing preparation\b'
		}
		foreach ($relativePath in $consumers.Keys)
		{
			$text = [System.IO.File]::ReadAllText((Join-Path $RepositoryRoot $relativePath))
			foreach ($pattern in $stalePatterns.GetEnumerator())
			{
				if ($text -match $pattern.Value)
				{
					$diagnostics.Add([pscustomobject] @{ path = $relativePath; code = 'STALE_GATE_LANGUAGE'; message = "stale gate language: $($pattern.Key)" })
				}
			}
		}

		$planAuditRelative = '.agents/skills/plan-audit/SKILL.md'
		$planAuditText = [System.IO.File]::ReadAllText((Join-Path $RepositoryRoot $planAuditRelative))
		$obsoleteAuditPatterns = [ordered] @{
			'SOURCE_SCOPE_PACKET' = '(?i)source-scope packet'
			'PROVENANCE_MAP' = '(?i)provenance map'
			'APPROVED_DELTA_LEDGER' = '(?i)approved-delta ledger'
			'AUTHORITY_IDS' = '(?i)\b[SPD]###\b|D authority'
		}
		foreach ($obsoletePattern in $obsoleteAuditPatterns.GetEnumerator())
		{
			if ($planAuditText -match $obsoletePattern.Value)
			{
				$diagnostics.Add([pscustomobject] @{ path = $planAuditRelative; code = 'OBSOLETE_AUDIT_AUTHORITY'; message = "obsolete plan-audit authority model: $($obsoletePattern.Key)" })
			}
		}
		foreach ($requiredText in @(
			'Use this skill only when a Tier-3 execution card has a material scope,',
			'Complete current plan, execution card, and fixed session baseline'))
		{
			if (-not $planAuditText.Contains($requiredText, [System.StringComparison]::Ordinal))
			{
				$diagnostics.Add([pscustomobject] @{ path = $planAuditRelative; code = 'AUDIT_CONTRACT_MISSING'; message = "missing conditional/current-input audit contract: $requiredText" })
			}
		}
	}

	if ($diagnostics.Count -gt 0)
	{
		Write-Result 'fail' 'contract.invalid' $diagnostics
		exit 2
	}

	Write-Result 'pass' 'ok' $diagnostics
	exit 0
}
catch
{
	$diagnostics = [System.Collections.Generic.List[object]]::new()
	$diagnostics.Add([pscustomobject] @{ path = $null; code = 'INTERNAL'; message = $_.Exception.Message })
	Write-Result 'error' 'internal.error' $diagnostics
	exit 1
}
