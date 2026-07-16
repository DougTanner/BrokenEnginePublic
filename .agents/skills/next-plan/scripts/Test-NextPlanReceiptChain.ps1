[CmdletBinding()]
param(
	[string] $CompletionReceiptPath,
	[string] $CompletionReceiptSha256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$result = [ordered]@{
	schemaVersion = 'broken-engine-next-plan-receipt-chain-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Receipt-chain validation did not run.'
	finalizationMode = $null
	workflowTerminal = $false
	nextAction = $null
	completionReceipt = $null
	approvalReceipt = $null
	presentationReceipt = $null
	presentation = $null
	executionCard = $null
}

function Complete-Chain([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 100 -Compress))
	exit $ExitCode
}

function Assert-ChainEqual([string] $Label, $Actual, $Expected) {
	if ([string]$Actual -cne [string]$Expected) {
		Complete-Chain 2 'blocked' 'chain.provenance-mismatch' "$Label differs across the receipt chain."
	}
}

try {
	Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
	if ([string]::IsNullOrWhiteSpace($CompletionReceiptPath) -or $CompletionReceiptSha256 -cnotmatch '^[0-9a-f]{64}$') {
		throw 'Completion receipt path or SHA-256 is malformed.'
	}
	$context = Get-NextPlanContext -AllowPrimaryAdvance
	try {
		$completionArtifact = Read-NextPlanJsonArtifact $context.Worktree $CompletionReceiptPath $CompletionReceiptSha256 'broken-engine-next-plan-completion/v1'
		$completion = $completionArtifact.Value
		$approvalArtifact = Read-NextPlanJsonArtifact $context.Worktree ([string]$completion.approvalReceipt.path) ([string]$completion.approvalReceipt.sha256) 'broken-engine-next-plan-approval/v1'
		$approval = $approvalArtifact.Value
		$presentationReceiptArtifact = Read-NextPlanJsonArtifact $context.Worktree ([string]$approval.presentationReceipt.path) ([string]$approval.presentationReceipt.sha256) 'broken-engine-next-plan-presentation/v1'
		$presentationReceipt = $presentationReceiptArtifact.Value
		$presentationArtifact = Read-NextPlanArtifact $context.Worktree ([string]$presentationReceipt.presentation.path) ([string]$presentationReceipt.presentation.sha256)
		if (-not (Test-Path -LiteralPath ([string]$completion.executionCard.path) -PathType Leaf)) {
			throw 'The completion-bound execution card no longer exists.'
		}
		$executionCardPath = Assert-NextPlanTempPath $context.Worktree ([string]$completion.executionCard.path) 'Execution card'
		$executionCardSha = Get-NextPlanFileSha256 $executionCardPath
	}
	catch {
		Complete-Chain 2 'blocked' 'chain.artifact-invalid' $_.Exception.Message
	}

	foreach ($field in @('owner','session','worktree','primary','commonDirectory','sessionBranch','targetBranch','baseline')) {
		$contextName = $field.Substring(0,1).ToUpperInvariant() + $field.Substring(1)
		Assert-ChainEqual "completion.$field" $completion.$field $context.$contextName
		Assert-ChainEqual "approval.$field" $approval.$field $completion.$field
		Assert-ChainEqual "presentation.$field" $presentationReceipt.$field $completion.$field
	}
	foreach ($field in @('plan','order','rowPlan','finalizationMode')) {
		Assert-ChainEqual "approval.$field" $approval.$field $completion.$field
		Assert-ChainEqual "presentation.$field" $presentationReceipt.$field $completion.$field
	}
	Assert-ChainEqual 'approval.planSha256' $approval.planSha256 $completion.planSha256
	Assert-ChainEqual 'presentation.planSha256' $presentationReceipt.planSha256 $completion.planSha256
	Assert-ChainEqual 'completion removedPlanSha256' $completion.worktreeCliReceipt.removedPlanSha256 $completion.planSha256
	Assert-ChainEqual 'approval.executionCard.path' $approval.executionCard.path $completion.executionCard.path
	Assert-ChainEqual 'approval.executionCard.sha256' $approval.executionCard.sha256 $completion.executionCard.sha256
	Assert-ChainEqual 'presentation.executionCard.path' $presentationReceipt.executionCard.path $completion.executionCard.path
	Assert-ChainEqual 'presentation.executionCard.sha256' $presentationReceipt.executionCard.sha256 $completion.executionCard.sha256
	Assert-ChainEqual 'current execution-card SHA-256' $executionCardSha $completion.executionCard.sha256
	if ([string]$completion.finalizationMode -cne 'session-landing') {
		Complete-Chain 2 'blocked' 'chain.mode-invalid' 'The /next-plan receipt chain mode is not session-landing.'
	}
	if (-not ($completion.worktreeCliReceipt.PSObject.Properties.Name -ccontains 'handled') -or -not $completion.worktreeCliReceipt.handled) {
		Complete-Chain 2 'blocked' 'chain.completion-unhandled' 'The completion receipt is not handled.'
	}
	if ([bool]$completion.workflowTerminal -or [string]$completion.nextAction -cne 'finalize-changes') {
		Complete-Chain 2 'blocked' 'chain.terminal-invalid' 'Completion must be nonterminal with finalize-changes as the next action.'
	}
	$currentRanges = @(Get-NextPlanPresentationRangesFromBytes $presentationArtifact.Bytes)
	if (($currentRanges -join "`n") -cne (@($presentationReceipt.presentation.ranges) -join "`n") -or
		($currentRanges -join "`n") -cne (@($approval.presentation.ranges) -join "`n")) {
		Complete-Chain 2 'blocked' 'chain.presentation-ranges-invalid' 'Presentation ranges do not cover the exact immutable artifact.'
	}

	$result.finalizationMode = [string]$completion.finalizationMode
	$result.workflowTerminal = $false
	$result.nextAction = 'finalize-changes'
	$result.completionReceipt = [ordered]@{ path = $completionArtifact.Path; sha256 = $completionArtifact.Sha256 }
	$result.approvalReceipt = [ordered]@{ path = $approvalArtifact.Path; sha256 = $approvalArtifact.Sha256 }
	$result.presentationReceipt = [ordered]@{ path = $presentationReceiptArtifact.Path; sha256 = $presentationReceiptArtifact.Sha256 }
	$result.presentation = [ordered]@{ path = $presentationArtifact.Path; sha256 = $presentationArtifact.Sha256; ranges = $currentRanges }
	$result.executionCard = [ordered]@{ path = $executionCardPath; sha256 = $executionCardSha }
	Complete-Chain 0 'pass' 'ok' 'The immutable next-plan receipt chain is valid.'
}
catch {
	if (Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue) {
		if (Test-NextPlanStateBlocker $_) { Complete-Chain 2 'blocked' 'chain.context-conflict' $_.Exception.Message }
	}
	Complete-Chain 1 'error' 'chain.failed' $_.Exception.Message
}
