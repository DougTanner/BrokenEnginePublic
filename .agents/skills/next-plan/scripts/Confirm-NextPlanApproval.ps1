[CmdletBinding()]
param(
	[string] $PresentationReceiptPath,
	[string] $PresentationReceiptSha256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$result = [ordered]@{
	schemaVersion = 'broken-engine-next-plan-approval-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Approval was not recorded.'
	receipt = $null
}

function Complete-Approval([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 100 -Compress))
	exit $ExitCode
}

try {
	Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
	$context = Get-NextPlanContext -RequireCleanPrimary
	$presentationReceiptArtifact = Read-NextPlanJsonArtifact $context.Worktree $PresentationReceiptPath $PresentationReceiptSha256 'broken-engine-next-plan-presentation/v1'
	$presentationReceipt = $presentationReceiptArtifact.Value
	if ([string]$presentationReceipt.finalizationMode -cne 'session-landing') {
		Complete-Approval 2 'blocked' 'approval.mode-invalid' 'The /next-plan presentation mode is not session-landing.'
	}
	foreach ($field in @('owner','session','worktree','primary','commonDirectory','sessionBranch','targetBranch','baseline')) {
		$expected = [string]$context.($field.Substring(0,1).ToUpperInvariant() + $field.Substring(1))
		if ([string]$presentationReceipt.$field -cne $expected) {
			Complete-Approval 2 'blocked' 'approval.provenance-changed' "Presentation field '$field' no longer matches wrapper provenance."
		}
	}
	$plan = [string]$presentationReceipt.plan
	$order = [string]$presentationReceipt.order
	Assert-NextPlanGitPath $plan
	Assert-NextPlanGitPath $order
	$allowed = Test-NextPlanOnlyAllowedPreCodeChanges $context.Worktree $context.Baseline $plan
	if (-not $allowed.Allowed) {
		Complete-Approval 2 'blocked' 'approval.unexpected-precode-change' "Unexpected pre-code paths: $($allowed.Unexpected -join ', ')."
	}
	if ((@($allowed.Changed) -join "`n") -cne (@($presentationReceipt.preCodeChangedPaths) -join "`n")) {
		Complete-Approval 2 'blocked' 'approval.precode-manifest-changed' 'The pre-code changed-path set differs from the presented state.'
	}
	$planPath = Assert-NextPlanRepositoryPath $context.Worktree (Join-Path $context.Worktree $plan) 'Selected plan'
	$currentPlanSha = Get-NextPlanFileSha256 $planPath
	if ($currentPlanSha -cne [string]$presentationReceipt.planSha256) {
		Complete-Approval 2 'blocked' 'approval.plan-changed' 'The selected plan bytes changed after presentation.'
	}
	if (-not (Test-Path -LiteralPath ([string]$presentationReceipt.executionCard.path) -PathType Leaf)) {
		Complete-Approval 2 'blocked' 'approval.execution-card-changed' 'The execution card no longer exists.'
	}
	$cardPath = Assert-NextPlanTempPath $context.Worktree ([string]$presentationReceipt.executionCard.path) 'Execution card'
	$currentCardSha = Get-NextPlanFileSha256 $cardPath
	if ($currentCardSha -cne [string]$presentationReceipt.executionCard.sha256 -or
		$currentCardSha -cne [string]$presentationReceipt.executionCardSha256) {
		Complete-Approval 2 'blocked' 'approval.execution-card-changed' 'The execution-card bytes changed after presentation.'
	}

	$presentation = Read-NextPlanArtifact $context.Worktree ([string]$presentationReceipt.presentation.path) ([string]$presentationReceipt.presentation.sha256)
	$currentRanges = @(Get-NextPlanPresentationRangesFromBytes $presentation.Bytes)
	if (($currentRanges -join "`n") -cne (@($presentationReceipt.presentation.ranges) -join "`n")) {
		Complete-Approval 2 'blocked' 'approval.presentation-ranges-changed' 'The presentation range manifest does not cover the exact current artifact.'
	}
	$rowPlan = Get-NextPlanRowIdentity $order $plan
	if ($rowPlan -cne [string]$presentationReceipt.rowPlan) { throw 'Presentation row identity is malformed.' }
	$rowResponse = Invoke-NextPlanProcess $context.WorktreeCli @('plan','row','status','--repo',$context.CommonDirectory,'--order',$order,'--plan',$rowPlan,'--owner',$context.Owner) $context.Worktree
	$row = ConvertFrom-NextPlanProcessJson $rowResponse 'plan row status'
	if ($rowResponse.ExitCode -eq 1) { throw 'WorktreeCli plan-row status failed.' }
	if ($rowResponse.ExitCode -eq 2 -or -not $row.ownedByRequester -or $row.owner -cne $context.Owner) {
		Complete-Approval 2 'blocked' 'approval.claim-lost' 'The selected plan row is not owned by the wrapper owner.'
	}
	$manifest = Get-NextPlanManifest $context.Worktree $context.Baseline
	$receiptValue = [ordered]@{
		schemaVersion = 'broken-engine-next-plan-approval/v1'
		owner = $context.Owner
		session = $context.Session
		worktree = $context.Worktree
		primary = $context.Primary
		commonDirectory = $context.CommonDirectory
		sessionBranch = $context.SessionBranch
		targetBranch = $context.TargetBranch
		baseline = $context.Baseline
		queue = [string]$presentationReceipt.queue
		plan = $plan
		order = $order
		rowPlan = $rowPlan
		finalizationMode = [string]$presentationReceipt.finalizationMode
		presentationReceipt = [ordered]@{ path = $presentationReceiptArtifact.Path; sha256 = $presentationReceiptArtifact.Sha256 }
		presentation = [ordered]@{ path = $presentation.Path; sha256 = $presentation.Sha256; ranges = $currentRanges }
		executionCard = [ordered]@{ path = $cardPath; sha256 = $currentCardSha }
		planSha256 = $currentPlanSha
		preCodeManifest = [ordered]@{ sha256 = $manifest.Sha256; rows = @($manifest.Rows) }
		approvalEvidence = 'affirmative-response-recorded-in-producing-transcript'
	}
	$receiptArtifact = Write-NextPlanJsonArtifact $context.Worktree 'next-plan-approval' $receiptValue
	$result.receipt = [ordered]@{ path = $receiptArtifact.Path; sha256 = $receiptArtifact.Sha256; bytes = $receiptArtifact.Bytes }
	Complete-Approval 0 'pass' 'ok' 'Approval was bound to the exact displayed presentation.'
}
catch {
	if (Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue) {
		if (Test-NextPlanStateBlocker $_) { Complete-Approval 2 'blocked' 'approval.context-conflict' $_.Exception.Message }
	}
	Complete-Approval 1 'error' 'approval.failed' $_.Exception.Message
}
