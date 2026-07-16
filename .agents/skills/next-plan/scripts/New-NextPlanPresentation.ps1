[CmdletBinding()]
param(
	[string] $ClaimReceiptPath,
	[string] $ClaimReceiptSha256,
	[string] $ExecutionCardPath,
	[string] $FinalizationMode
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$utf8 = [Text.UTF8Encoding]::new($false, $true)

$result = [ordered]@{
	schemaVersion = 'broken-engine-next-plan-presentation-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Presentation was not written.'
	presentation = $null
	receipt = $null
}

function Complete-Presentation([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 100 -Compress))
	exit $ExitCode
}

try {
	Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
	if ($FinalizationMode -cne 'session-landing') { throw 'FinalizationMode must be session-landing for /next-plan.' }
	$context = Get-NextPlanContext -RequireCleanPrimary
	$claimArtifact = Read-NextPlanJsonArtifact $context.Worktree $ClaimReceiptPath $ClaimReceiptSha256 'broken-engine-next-plan-claim/v1'
	$claim = $claimArtifact.Value
	foreach ($field in @('owner','session','worktree','primary','commonDirectory','sessionBranch','targetBranch','baseline')) {
		$expected = [string]$context.($field.Substring(0,1).ToUpperInvariant() + $field.Substring(1))
		if ([string]$claim.$field -cne $expected) { throw "Claim receipt field '$field' no longer matches wrapper provenance." }
	}
	$plan = [string]$claim.plan
	$order = [string]$claim.order
	Assert-NextPlanGitPath $plan
	Assert-NextPlanGitPath $order
	$cardPath = Assert-NextPlanTempPath $context.Worktree $ExecutionCardPath 'Execution card'
	$allowed = Test-NextPlanOnlyAllowedPreCodeChanges $context.Worktree $context.Baseline $plan
	if (-not $allowed.Allowed) { Complete-Presentation 2 'blocked' 'presentation.unexpected-precode-change' "Unexpected pre-code paths: $($allowed.Unexpected -join ', ')." }

	$planPath = Assert-NextPlanRepositoryPath $context.Worktree (Join-Path $context.Worktree $plan) 'Selected plan'
	$planBytes = [IO.File]::ReadAllBytes($planPath)
	$cardBytes = [IO.File]::ReadAllBytes($cardPath)
	try {
		$planText = $utf8.GetString($planBytes)
		$cardText = $utf8.GetString($cardBytes)
	}
	catch [Text.DecoderFallbackException] { throw 'The selected plan and execution card must be strict UTF-8.' }

	$rowPlan = Get-NextPlanRowIdentity $order $plan
	$rowResponse = Invoke-NextPlanProcess $context.WorktreeCli @('plan','row','status','--repo',$context.CommonDirectory,'--order',$order,'--plan',$rowPlan,'--owner',$context.Owner) $context.Worktree
	$row = ConvertFrom-NextPlanProcessJson $rowResponse 'plan row status'
	if ($rowResponse.ExitCode -eq 1) { throw 'WorktreeCli plan-row status failed.' }
	if ($rowResponse.ExitCode -eq 2 -or -not $row.ownedByRequester -or $row.owner -cne $context.Owner) {
		Complete-Presentation 2 'blocked' 'presentation.claim-lost' 'The selected plan row is not owned by the wrapper owner.'
	}

	$lines = [Collections.Generic.List[string]]::new()
	foreach ($line in @(
		'# Resolved `/next-plan` Presentation',
		'',
		"Claim: $plan",
		"Baseline: $($context.Baseline)",
		"Finalization mode: $FinalizationMode",
		"Claim receipt SHA-256: $ClaimReceiptSha256",
		'',
		'## Execution card',
		''
	)) { $lines.Add($line) }
	foreach ($line in [regex]::Split($cardText, "`r`n|`n|`r")) { $lines.Add($line) }
	$lines.Add('')
	$lines.Add('## Complete resolved plan')
	$lines.Add('')
	foreach ($line in [regex]::Split($planText, "`r`n|`n|`r")) { $lines.Add($line) }

	$ranges = @(Get-NextPlanPresentationRanges $lines.ToArray())
	$presentationBytes = $utf8.GetBytes(($lines -join "`n") + "`n")
	$presentationArtifact = Write-NextPlanArtifact $context.Worktree 'next-plan-presentation' 'md' $presentationBytes
	$receiptValue = [ordered]@{
		schemaVersion = 'broken-engine-next-plan-presentation/v1'
		owner = $context.Owner
		session = $context.Session
		worktree = $context.Worktree
		primary = $context.Primary
		commonDirectory = $context.CommonDirectory
		sessionBranch = $context.SessionBranch
		targetBranch = $context.TargetBranch
		baseline = $context.Baseline
		queue = [string]$claim.queue
		plan = $plan
		order = $order
		rowPlan = $rowPlan
		finalizationMode = $FinalizationMode
		claimReceipt = [ordered]@{ path = $claimArtifact.Path; sha256 = $claimArtifact.Sha256 }
		planSha256 = Get-NextPlanSha256 $planBytes
		executionCardSha256 = Get-NextPlanSha256 $cardBytes
		executionCard = [ordered]@{ path = $cardPath; sha256 = Get-NextPlanSha256 $cardBytes }
		preCodeChangedPaths = @($allowed.Changed)
		presentation = [ordered]@{ path = $presentationArtifact.Path; sha256 = $presentationArtifact.Sha256; bytes = $presentationArtifact.Bytes; ranges = $ranges }
	}
	$receiptArtifact = Write-NextPlanJsonArtifact $context.Worktree 'next-plan-presentation-receipt' $receiptValue
	$result.presentation = $receiptValue.presentation
	$result.receipt = [ordered]@{ path = $receiptArtifact.Path; sha256 = $receiptArtifact.Sha256; bytes = $receiptArtifact.Bytes }
	Complete-Presentation 0 'pass' 'ok' 'Exact full-plan presentation and immutable receipt were written.'
}
catch {
	if (Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue) {
		if (Test-NextPlanStateBlocker $_) { Complete-Presentation 2 'blocked' 'presentation.context-conflict' $_.Exception.Message }
	}
	Complete-Presentation 1 'error' 'presentation.failed' $_.Exception.Message
}
