# Completion phase 1 (in-session): verify the wrapper owner still holds the selected
# plan row, run the closure scan for lingering live references, then delete the plan
# file as an ordinary tracked deletion (`git rm`). The queue row stays claimed and is
# removed post-landing by Invoke-FinalizeLanding.ps1 (`plan order complete`); this
# script never runs `plan order complete` or `plan order validate` and performs no
# post-deletion row checks. -Plan is the repository-relative plan path being completed;
# -PlanSha256 is the immutable digest returned by its claim.
[CmdletBinding()]
param(
	[string] $Plan,
	[string] $PlanSha256
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
	if ([string]::IsNullOrWhiteSpace($Plan)) { throw 'Plan is required.' }
	if ($PlanSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'PlanSha256 must be the lowercase SHA-256 from the claim receipt.' }
	$plan = $Plan.Replace('\', '/')
	Assert-NextPlanGitPath $plan
	$order = if ($plan.StartsWith('Documents/Plans/', [StringComparison]::Ordinal)) { 'Documents/Plans/Order.md' }
		elseif ($plan.StartsWith('Documents/Features/', [StringComparison]::Ordinal)) { 'Documents/Features/Order.md' }
		else { throw "Plan '$plan' does not belong to a known queue." }
	$rowPlan = Get-NextPlanRowIdentity $order $plan
	$expectedPlanPath = Join-Path $context.Worktree $plan
	if (-not (Test-Path -LiteralPath $expectedPlanPath -PathType Leaf)) {
		Complete-Workflow 2 'blocked' 'completion.plan-missing' 'The selected plan no longer exists.'
	}
	if ((Get-NextPlanFileSha256 $expectedPlanPath) -cne $PlanSha256) {
		Complete-Workflow 2 'blocked' 'completion.plan-byte-mismatch' 'Claimed plan bytes changed; stop this workflow and retain the claimed row.'
	}

	$rowResponse = Invoke-NextPlanProcess $context.WorktreeCli @('plan','row','status','--repo',$context.CommonDirectory,'--order',$order,'--plan',$rowPlan,'--owner',$context.Owner) $context.Worktree
	$row = ConvertFrom-NextPlanProcessJson $rowResponse 'plan row status'
	if ($rowResponse.ExitCode -eq 1) { throw 'WorktreeCli plan-row status failed.' }
	if ($rowResponse.ExitCode -eq 2 -or -not $row.ownedByRequester -or $row.owner -cne $context.Owner) {
		Complete-Workflow 2 'blocked' 'completion.claim-lost' 'The selected plan row is not owned by the wrapper owner.'
	}

	$closureScript = Join-Path $PSScriptRoot '..\..\..\scripts\Find-PlanClosureReferences.ps1'
	if (-not (Test-Path -LiteralPath $closureScript)) {
		$closureScript = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\Find-PlanClosureReferences.ps1'
	}
	$closureResponse = Invoke-NextPlanProcess (Join-Path $PSHOME 'pwsh.exe') @('-NoLogo','-NoProfile','-File',$closureScript,'-Worktree',$context.Worktree,'-Baseline',$context.Baseline,'-CompletedPlan',$plan) $context.Worktree
	$closure = ConvertFrom-NextPlanProcessJson $closureResponse 'plan closure scan'
	if ($closureResponse.ExitCode -ne 0) { throw 'Plan closure scan failed.' }
	# The queue table is machine-local, so no in-tree Order.md owning-row hit needs excluding; any
	# remaining hit is a genuine live reference to the completed plan.
	$unresolvedHits = @($closure.hits)
	$result.closure = [ordered]@{ scan = $closure; unresolvedHits = $unresolvedHits }
	if ($unresolvedHits.Count -ne 0) {
		Complete-Workflow 2 'blocked' 'completion.closure-references' 'Live Plans/Features references to the completed plan remain.'
	}

	$removeResponse = Invoke-NextPlanProcess 'git.exe' @('-C',$context.Worktree,'rm','--',$plan) $context.Worktree
	if ($removeResponse.ExitCode -ne 0) {
		throw "git rm of the completed plan failed: $($removeResponse.Stdout.Trim())$($removeResponse.Stderr.Trim())"
	}
	$result.completion = [ordered]@{ planFileDeleted = $true; plan = $plan; rowPlan = $rowPlan; order = $order }
	Complete-Workflow 0 'pass' 'ok' 'Plan file deleted; the row is removed post-landing and finalization is the mandatory next action.'
}
catch {
	if (Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue) {
		if (Test-NextPlanStateBlocker $_) { Complete-Workflow 2 'blocked' 'completion.context-conflict' $_.Exception.Message }
	}
	Complete-Workflow 1 'error' 'completion.failed' $_.Exception.Message
}
