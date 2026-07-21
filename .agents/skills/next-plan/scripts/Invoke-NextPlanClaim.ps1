[CmdletBinding()]
param(
	[string] $Queue = 'plans',
	[string] $Plan
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$result = [ordered]@{
	schemaVersion = 'broken-engine-next-plan-claim-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Claim did not run.'
	receipt = $null
	claim = $null
}

function Complete-Claim([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 100 -Compress))
	exit $ExitCode
}

try {
	Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
	if ($Queue -cnotin @('plans', 'features')) { throw 'Queue must be plans or features.' }
	$normalizedPlan = if ($null -eq $Plan) { '' } else { $Plan.Replace('\', '/') }
	if (-not [string]::IsNullOrEmpty($normalizedPlan)) {
		Assert-NextPlanGitPath $normalizedPlan
		$expectedPrefix = if ($Queue -ceq 'plans') { 'Documents/Plans/' } else { 'Documents/Features/' }
		if (-not $normalizedPlan.StartsWith($expectedPrefix, [StringComparison]::Ordinal)) {
			throw "Plan does not belong to queue '$Queue'."
		}
	}
	$context = Get-NextPlanContext
	$status = Invoke-NextPlanProcess 'git.exe' @('-C',$context.Worktree,'status','--porcelain=v1','--untracked-files=all') $context.Worktree
	if ($status.ExitCode -ne 0) { throw (New-NextPlanStateBlocker 'Could not verify the session worktree is clean before claim.') }
	if (-not [string]::IsNullOrWhiteSpace($status.Stdout)) {
		throw (New-NextPlanStateBlocker 'The session worktree must be clean before claim, including after primary-advance recovery.')
	}
	$validate = Invoke-NextPlanProcess $context.WorktreeCli @('plan','order','validate','--repo',$context.CommonDirectory,'--worktree',$context.Primary) $context.Worktree
	$validateJson = ConvertFrom-NextPlanProcessJson $validate 'plan order validate'
	if ($validate.ExitCode -ne 0 -or -not $validateJson.ok) {
		$result.claim = $validateJson
		Complete-Claim ($(if ($validate.ExitCode -eq 2) { 2 } else { 1 })) 'blocked' 'queue.validation-failed' 'Primary queue validation failed.'
	}
	$orphanPlans = @($validateJson.notices | Where-Object { $_.code -ceq 'orphan-plan' })
	if ($orphanPlans.Count -ne 0) {
		$result.claim = $validateJson
		Complete-Claim 2 'blocked' 'queue.orphan-plan' 'Primary queue validation reported orphan plan files; repair the queue before claiming.'
	}
	$arguments = @(
		'plan','order','claim-next','--repo',$context.CommonDirectory,
		'--primary-worktree',$context.Primary,'--worktree',$context.Worktree,
		'--branch',$context.TargetBranch,'--owner',$context.Owner,'--session',$context.Session,
		'--queue',$Queue
	)
	if (-not [string]::IsNullOrEmpty($normalizedPlan)) { $arguments += @('--plan', $normalizedPlan) }
	$response = Invoke-NextPlanProcess $context.WorktreeCli $arguments $context.Worktree
	$claim = ConvertFrom-NextPlanProcessJson $response 'plan order claim-next'
	$result.claim = $claim
	if ($response.ExitCode -ne 0 -and -not ($claim.PSObject.Properties.Name -ccontains 'claimed' -and $claim.claimed)) {
		Complete-Claim ($(if ($response.ExitCode -eq 2) { 2 } else { 1 })) 'blocked' 'claim.rejected' 'WorktreeCli did not create a plan-row claim.'
	}
	if (-not $claim.claimed -or $claim.owner -cne $context.Owner -or $claim.primaryCommit -cne $context.Baseline) {
		throw 'WorktreeCli claim receipt does not match wrapper provenance.'
	}
	$order = ([string]$claim.order).Replace('\', '/')
	Assert-NextPlanGitPath $order
	$receiptValue = [ordered]@{
		schemaVersion = 'broken-engine-next-plan-claim/v1'
		owner = $context.Owner
		session = $context.Session
		worktree = $context.Worktree
		primary = $context.Primary
		commonDirectory = $context.CommonDirectory
		sessionBranch = $context.SessionBranch
		targetBranch = $context.TargetBranch
		baseline = $context.Baseline
		queue = $Queue
		plan = [string]$claim.plan
		order = $order
		planSha256 = [string]$claim.planSha256
		worktreeCliReceipt = $claim
	}
	$receiptArtifact = Write-NextPlanJsonArtifact $context.Worktree 'next-plan-claim' $receiptValue
	$result.receipt = [ordered]@{ path = $receiptArtifact.Path; sha256 = $receiptArtifact.Sha256; bytes = $receiptArtifact.Bytes }
	if ($response.ExitCode -eq 0) {
		Complete-Claim 0 'pass' 'ok' 'Plan row was claimed from wrapper-derived state.'
	}
	Complete-Claim 2 'blocked' 'claim.unlock-failed' 'The row was claimed, but WorktreeCli reported a queue-unlock failure.'
}
catch {
	if (Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue) {
		if (Test-NextPlanStateBlocker $_) { Complete-Claim 2 'blocked' 'claim.context-conflict' $_.Exception.Message }
	}
	Complete-Claim 1 'error' 'claim.failed' $_.Exception.Message
}
