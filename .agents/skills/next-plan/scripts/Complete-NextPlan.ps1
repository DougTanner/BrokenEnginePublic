[CmdletBinding()]
param(
	[string] $ApprovalReceiptPath,
	[string] $ApprovalReceiptSha256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$result = [ordered]@{
	schemaVersion = 'broken-engine-next-plan-completion-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Completion did not run.'
	workflowTerminal = $false
	nextAction = 'finalize-changes'
	receipt = $null
	closure = $null
	completion = $null
}

function Complete-Workflow([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 100 -Compress))
	exit $ExitCode
}

try {
	Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
	$context = Get-NextPlanContext -AllowPrimaryAdvance
	$approvalArtifact = Read-NextPlanJsonArtifact $context.Worktree $ApprovalReceiptPath $ApprovalReceiptSha256 'broken-engine-next-plan-approval/v1'
	$approval = $approvalArtifact.Value
	if ([string]$approval.finalizationMode -cne 'session-landing') {
		Complete-Workflow 2 'blocked' 'completion.mode-invalid' 'The /next-plan approval mode is not session-landing.'
	}
	foreach ($field in @('owner','session','worktree','primary','commonDirectory','sessionBranch','targetBranch','baseline')) {
		$expected = [string]$context.($field.Substring(0,1).ToUpperInvariant() + $field.Substring(1))
		if ([string]$approval.$field -cne $expected) {
			Complete-Workflow 2 'blocked' 'completion.provenance-changed' "Approval field '$field' no longer matches wrapper provenance."
		}
	}
	$plan = [string]$approval.plan
	$order = [string]$approval.order
	$rowPlan = Get-NextPlanRowIdentity $order $plan
	if ($rowPlan -cne [string]$approval.rowPlan) { throw 'Approval row identity is malformed.' }
	$approvedCardPath = [string]$approval.executionCard.path
	if (-not (Test-Path -LiteralPath $approvedCardPath -PathType Leaf)) {
		Complete-Workflow 2 'blocked' 'completion.execution-card-changed' 'The approved execution card no longer exists.'
	}
	$cardPath = Assert-NextPlanTempPath $context.Worktree $approvedCardPath 'Execution card'
	$currentCardSha = Get-NextPlanFileSha256 $cardPath
	if ($currentCardSha -cne [string]$approval.executionCard.sha256) {
		Complete-Workflow 2 'blocked' 'completion.execution-card-changed' 'The execution-card bytes changed after approval.'
	}
	$expectedPlanPath = Join-Path $context.Worktree $plan
	if (-not (Test-Path -LiteralPath $expectedPlanPath -PathType Leaf)) {
		Complete-Workflow 2 'blocked' 'completion.plan-changed' 'The selected plan no longer exists.'
	}
	$planPath = Assert-NextPlanRepositoryPath $context.Worktree $expectedPlanPath 'Selected plan'
	$currentPlanSha = Get-NextPlanFileSha256 $planPath
	if ($currentPlanSha -cne [string]$approval.planSha256) {
		Complete-Workflow 2 'blocked' 'completion.plan-changed' 'The selected plan bytes changed after approval.'
	}

	$rowResponse = Invoke-NextPlanProcess $context.WorktreeCli @('plan','row','status','--repo',$context.CommonDirectory,'--order',$order,'--plan',$rowPlan,'--owner',$context.Owner) $context.Worktree
	$row = ConvertFrom-NextPlanProcessJson $rowResponse 'plan row status'
	if ($rowResponse.ExitCode -eq 1) { throw 'WorktreeCli plan-row status failed.' }
	if ($rowResponse.ExitCode -eq 2 -or -not $row.ownedByRequester -or $row.owner -cne $context.Owner) {
		Complete-Workflow 2 'blocked' 'completion.claim-lost' 'The selected plan row is not owned by the wrapper owner.'
	}

	$closureScript = Join-Path $PSScriptRoot '..\..\..\scripts\Find-PlanClosureReferences.ps1'
	$closureResponse = Invoke-NextPlanProcess (Join-Path $PSHOME 'pwsh.exe') @('-NoLogo','-NoProfile','-File',$closureScript,'-Worktree',$context.Worktree,'-Baseline',$context.Baseline,'-CompletedPlan',$plan) $context.Worktree
	$closure = ConvertFrom-NextPlanProcessJson $closureResponse 'plan closure scan'
	if ($closureResponse.ExitCode -ne 0) { throw 'Plan closure scan failed.' }
	$owningRowPrefix = "| [$rowPlan]($rowPlan) |"
	$unresolvedHits = @($closure.hits | Where-Object {
		-not (([string]$_.path).Equals($order, [StringComparison]::OrdinalIgnoreCase) -and ([string]$_.text).StartsWith($owningRowPrefix, [StringComparison]::Ordinal))
	})
	$result.closure = [ordered]@{ scan = $closure; unresolvedHits = $unresolvedHits }
	if ($unresolvedHits.Count -ne 0) {
		Complete-Workflow 2 'blocked' 'completion.closure-references' 'Live Plans/Features references to the completed plan remain.'
	}

	$completeResponse = Invoke-NextPlanProcess $context.WorktreeCli @(
		'plan','order','complete','--repo',$context.CommonDirectory,'--worktree',$context.Worktree,
		'--owner',$context.Owner,'--session',$context.Session,'--plan',$plan
	) $context.Worktree
	$complete = ConvertFrom-NextPlanProcessJson $completeResponse 'plan order complete'
	$result.completion = $complete
	if ($completeResponse.ExitCode -eq 2 -and -not ($complete.PSObject.Properties.Name -ccontains 'handled' -and $complete.handled)) {
		Complete-Workflow 2 'blocked' 'completion.conflict' 'WorktreeCli rejected completion because queue state changed.'
	}
	if ($completeResponse.ExitCode -eq 1 -or -not ($complete.PSObject.Properties.Name -ccontains 'handled') -or -not $complete.handled) {
		throw 'WorktreeCli did not complete the selected plan.'
	}
	if ($complete.claimOwner -cne $context.Owner -or $complete.plan -cne $plan) {
		throw 'WorktreeCli completion receipt does not match approval provenance.'
	}
	if ([string]$complete.removedPlanSha256 -cne [string]$approval.planSha256) {
		throw 'WorktreeCli removed plan bytes do not match the approval receipt.'
	}

	$validateResponse = Invoke-NextPlanProcess $context.WorktreeCli @('plan','order','validate','--repo',$context.CommonDirectory,'--worktree',$context.Worktree) $context.Worktree
	$validation = ConvertFrom-NextPlanProcessJson $validateResponse 'plan order validate'
	if ($validateResponse.ExitCode -eq 1) { throw 'WorktreeCli plan-order validation failed.' }
	if ($validateResponse.ExitCode -eq 2 -or -not $validation.ok) {
		Complete-Workflow 2 'blocked' 'completion.validation-failed' 'Completed session queue validation failed.'
	}
	$postRowResponse = Invoke-NextPlanProcess $context.WorktreeCli @('plan','row','status','--repo',$context.CommonDirectory,'--order',$order,'--plan',$rowPlan,'--owner',$context.Owner) $context.Worktree
	$postRow = ConvertFrom-NextPlanProcessJson $postRowResponse 'post-completion plan row status'
	if ($postRowResponse.ExitCode -eq 1) { throw 'WorktreeCli post-completion plan-row status failed.' }
	if ($postRowResponse.ExitCode -eq 2 -or -not $postRow.ownedByRequester -or $postRow.owner -cne $context.Owner) {
		Complete-Workflow 2 'blocked' 'completion.claim-not-retained' 'Completion did not retain the owner-held plan-row claim.'
	}
	$receiptValue = [ordered]@{
		schemaVersion = 'broken-engine-next-plan-completion/v1'
		owner = $context.Owner
		session = $context.Session
		worktree = $context.Worktree
		primary = $context.Primary
		commonDirectory = $context.CommonDirectory
		sessionBranch = $context.SessionBranch
		targetBranch = $context.TargetBranch
		baseline = $context.Baseline
		finalizationMode = [string]$approval.finalizationMode
		plan = $plan
		order = $order
		rowPlan = $rowPlan
		planSha256 = $currentPlanSha
		executionCard = [ordered]@{ path = $cardPath; sha256 = $currentCardSha }
		approvalReceipt = [ordered]@{ path = $approvalArtifact.Path; sha256 = $approvalArtifact.Sha256 }
		closure = [ordered]@{ scan = $closure; unresolvedHits = $unresolvedHits }
		worktreeCliReceipt = $complete
		validation = $validation
		postCompletionClaim = $postRow
		workflowTerminal = $false
		nextAction = 'finalize-changes'
	}
	$receiptArtifact = Write-NextPlanJsonArtifact $context.Worktree 'next-plan-completion' $receiptValue
	$result.receipt = [ordered]@{ path = $receiptArtifact.Path; sha256 = $receiptArtifact.Sha256; bytes = $receiptArtifact.Bytes }
	if ($completeResponse.ExitCode -eq 0) {
		Complete-Workflow 0 'pass' 'ok' 'Plan completion is verified and finalization is the mandatory next action.'
	}
	Complete-Workflow 2 'blocked' 'completion.unlock-failed' 'Plan completion is verified, but WorktreeCli reported a queue-unlock failure.'
}
catch {
	if (Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue) {
		if (Test-NextPlanStateBlocker $_) { Complete-Workflow 2 'blocked' 'completion.context-conflict' $_.Exception.Message }
	}
	Complete-Workflow 1 'error' 'completion.failed' $_.Exception.Message
}
