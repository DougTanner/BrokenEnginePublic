[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$scriptDirectory = Split-Path -Parent $PSCommandPath
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDirectory '../../../..'))
$validator = Join-Path $scriptDirectory 'Test-NextPlanWorkflowContract.ps1'
$fixtureRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("BrokenEngine-NextPlanContract-{0}" -f [guid]::NewGuid().ToString('N'))

function Copy-FixtureFile
{
	param([string] $RelativePath)

	$destination = Join-Path $fixtureRoot $RelativePath
	[System.IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
	Copy-Item -LiteralPath (Join-Path $repositoryRoot $RelativePath) -Destination $destination
}

function Invoke-Validator
{
	$output = @(& pwsh -NoProfile -File $validator -RepositoryRoot $fixtureRoot 2>&1)
	return [pscustomobject] @{
		ExitCode = $LASTEXITCODE
		Text = $output -join "`n"
	}
}

function Assert-Pass
{
	$result = Invoke-Validator
	if ($result.ExitCode -ne 0 -or $result.Text -notmatch '"status":\s*"pass"')
	{
		throw "Expected contract validation to pass. Exit=$($result.ExitCode) Output=$($result.Text)"
	}
}

function Assert-FailsWith
{
	param([string] $Code)

	$result = Invoke-Validator
	if ($result.ExitCode -ne 2 -or -not $result.Text.Contains($Code, [System.StringComparison]::Ordinal))
	{
		throw "Expected deterministic failure containing '$Code'. Exit=$($result.ExitCode) Output=$($result.Text)"
	}
}

try
{
	$files = @(
		'.agents/skills/next-plan/SKILL.md',
		'.agents/skills/next-plan/references/execution-gates.md',
		'.agents/skills/next-plan/references/tier3-workflow.md',
		'.agents/skills/next-plan/scripts/New-NextPlanPresentation.ps1',
		'.agents/skills/next-plan/scripts/Confirm-NextPlanApproval.ps1',
		'.agents/skills/next-plan/scripts/Complete-NextPlan.ps1',
		'.agents/skills/next-plan/scripts/Test-NextPlanReceiptChain.ps1',
		'.agents/skills/plan-audit/SKILL.md',
		'.agents/skills/verify-changes/SKILL.md',
		'.agents/skills/finalize-changes/SKILL.md',
		'Documents/Plans/AGENTS.md'
	)
	foreach ($file in $files)
	{
		Copy-FixtureFile $file
	}

	Assert-Pass

	$nextPlanPath = Join-Path $fixtureRoot '.agents/skills/next-plan/SKILL.md'
	$originalNextPlan = [System.IO.File]::ReadAllText($nextPlanPath)
	[System.IO.File]::WriteAllText($nextPlanPath, $originalNextPlan.Replace('(references/execution-gates.md)', '(references/missing-gates.md)'))
	Assert-FailsWith 'CONTRACT_LINK_MISSING'
	[System.IO.File]::WriteAllText($nextPlanPath, $originalNextPlan)
	[System.IO.File]::WriteAllText($nextPlanPath, $originalNextPlan.Replace('worktree''s ignored `Temp/` directory, and `-FinalizationMode session-landing`.', 'worktree, with a caller-selected finalization mode.'))
	Assert-FailsWith 'NEXT_PLAN_PRESENTATION_INPUT_CONTRACT_MISSING'
	[System.IO.File]::WriteAllText($nextPlanPath, $originalNextPlan)

	$tier3Path = Join-Path $fixtureRoot '.agents/skills/next-plan/references/tier3-workflow.md'
	$originalTier3 = [System.IO.File]::ReadAllText($tier3Path)
	[System.IO.File]::WriteAllText($tier3Path, "$originalTier3`nApprove the claim before continuing.`n")
	Assert-FailsWith 'STALE_GATE_LANGUAGE'
	[System.IO.File]::WriteAllText($tier3Path, $originalTier3)

	$contractPath = Join-Path $fixtureRoot '.agents/skills/next-plan/references/execution-gates.md'
	$originalContract = [System.IO.File]::ReadAllText($contractPath)
	[System.IO.File]::WriteAllText($contractPath, $originalContract.Replace('<!-- next-plan-gate:continuous-execution -->', ''))
	Assert-FailsWith 'CONTRACT_MARKER_MISSING'
	[System.IO.File]::WriteAllText($contractPath, $originalContract)
	[System.IO.File]::WriteAllText($contractPath, $originalContract.Replace('The `/next-plan` finalization mode is always `session-landing`.', 'The caller selects the finalization mode.'))
	Assert-FailsWith 'NEXT_PLAN_MODE_CONTRACT_MISSING'
	[System.IO.File]::WriteAllText($contractPath, $originalContract)
	[System.IO.File]::WriteAllText($contractPath, $originalContract.Replace('<!-- next-plan-gate:receipt-chain -->', ''))
	Assert-FailsWith 'CONTRACT_MARKER_MISSING'
	[System.IO.File]::WriteAllText($contractPath, $originalContract)

	$verifyPath = Join-Path $fixtureRoot '.agents/skills/verify-changes/SKILL.md'
	$originalVerify = [System.IO.File]::ReadAllText($verifyPath)
	[System.IO.File]::WriteAllText($verifyPath, $originalVerify.Replace('Use only the validator-returned `presentationReceipt`,', 'Use caller-provided receipt metadata.'))
	Assert-FailsWith 'RECEIPT_CHAIN_CONTRACT_MISSING'
	[System.IO.File]::WriteAllText($verifyPath, $originalVerify)

	$finalizePath = Join-Path $fixtureRoot '.agents/skills/finalize-changes/SKILL.md'
	$originalFinalize = [System.IO.File]::ReadAllText($finalizePath)
	[System.IO.File]::WriteAllText($finalizePath, $originalFinalize.Replace('Confirm commit of verified manifest <manifest-sha256> on primary branch <primary-branch> at <primary-tip>?', 'Confirm the commit?'))
	Assert-FailsWith 'CONTRACT_ANCHOR_MISSING'
	[System.IO.File]::WriteAllText($finalizePath, $originalFinalize)
	[System.IO.File]::WriteAllText($finalizePath, $originalFinalize.Replace('Require the validated `/next-plan` mode to be', 'Select the finalization mode from the caller.'))
	Assert-FailsWith 'RECEIPT_CHAIN_CONTRACT_MISSING'
	[System.IO.File]::WriteAllText($finalizePath, $originalFinalize)
	[System.IO.File]::WriteAllText($finalizePath, $originalFinalize.Replace('preserve the caller-supplied mode contract and do not require a receipt chain.', 'require a receipt chain for every finalization route.'))
	Assert-FailsWith 'NON_NEXT_PLAN_MODE_CONTRACT_MISSING'
	[System.IO.File]::WriteAllText($finalizePath, $originalFinalize)

	$receiptChainPath = Join-Path $fixtureRoot '.agents/skills/next-plan/scripts/Test-NextPlanReceiptChain.ps1'
	$originalReceiptChain = [System.IO.File]::ReadAllText($receiptChainPath)
	[System.IO.File]::WriteAllText($receiptChainPath, $originalReceiptChain.Replace('broken-engine-next-plan-receipt-chain-result/v1', 'broken-engine-next-plan-receipt-chain-result/legacy'))
	Assert-FailsWith 'RECEIPT_CHAIN_VALIDATOR_CONTRACT_MISSING'
	[System.IO.File]::WriteAllText($receiptChainPath, $originalReceiptChain)

	$presentationPath = Join-Path $fixtureRoot '.agents/skills/next-plan/scripts/New-NextPlanPresentation.ps1'
	$originalPresentation = [System.IO.File]::ReadAllText($presentationPath)
	[System.IO.File]::WriteAllText($presentationPath, $originalPresentation.Replace("FinalizationMode -cne 'session-landing'", "FinalizationMode -cnotin @('session-landing', 'primary-commit')"))
	Assert-FailsWith 'PRESENTATION_INPUT_ENFORCEMENT_MISSING'
	[System.IO.File]::WriteAllText($presentationPath, $originalPresentation)

	$planAuditPath = Join-Path $fixtureRoot '.agents/skills/plan-audit/SKILL.md'
	$originalPlanAudit = [System.IO.File]::ReadAllText($planAuditPath)
	[System.IO.File]::WriteAllText($planAuditPath, "$originalPlanAudit`nRequire the immutable source-scope packet before review.`n")
	Assert-FailsWith 'OBSOLETE_AUDIT_AUTHORITY'
	[System.IO.File]::WriteAllText($planAuditPath, $originalPlanAudit.Replace('findings-only work and does not create an approval gate.', 'work may request approval before continuing.'))
	Assert-FailsWith 'CONTRACT_ANCHOR_MISSING'
	[System.IO.File]::WriteAllText($planAuditPath, $originalPlanAudit)

	Remove-Item -LiteralPath $contractPath -Force
	Assert-FailsWith 'FILE_MISSING'

	[pscustomobject] @{
		schemaVersion = 'broken-engine-next-plan-contract-fixtures/v1'
		status = 'pass'
		cases = 15
	} | ConvertTo-Json -Depth 3
}
finally
{
	if ([System.IO.Directory]::Exists($fixtureRoot))
	{
		Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
	}
}
