[CmdletBinding()]
param([string] $Plan)
$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest
$result=[ordered]@{schemaVersion='broken-engine-next-plan-claim-result/v3';status='error';code='internal.error';message='Claim did not run.';claim=$null}
function Complete-Claim([int]$ExitCode,[string]$Status,[string]$Code,[string]$Message){$result.status=$Status;$result.code=$Code;$result.message=$Message;[Console]::Out.Write(($result|ConvertTo-Json -Depth 100 -Compress));exit $ExitCode}
try {
 Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
 $filename=$null
 if ($Plan) {
  $Plan=$Plan.Replace('\','/')
  Assert-NextPlanGitPath $Plan
  if ($Plan.StartsWith('Documents/Plans/',[StringComparison]::Ordinal)) {
   # WorktreeCli owns canonical path validation; preserve its exact-path contract.
  } elseif ($Plan.Contains('/',[StringComparison]::Ordinal)) {
   throw 'Only Documents/Plans paths or filename-only inputs are scheduler inputs.'
  } else {
   $filename=$Plan
   $Plan=$null
  }
 }
 $context=Get-NextPlanContext
 $status=Invoke-NextPlanProcess 'git.exe' @('-C',$context.Worktree,'status','--porcelain=v1','--untracked-files=all') $context.Worktree
 if($status.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($status.Stdout)){throw (New-NextPlanStateBlocker 'Session worktree must be clean before a plan claim.')}
 $validate=Invoke-NextPlanProcess $context.WorktreeCli @('plan','validate','--repo',$context.CommonDirectory,'--worktree',$context.Worktree,'--baseline',$context.Baseline) $context.Worktree
 $validation=ConvertFrom-NextPlanProcessJson $validate 'plan validate'; $projection=[ordered]@{}; foreach($name in @('status','code','message','diagnostics','notices','healedClaims')){if($validation.PSObject.Properties.Name -ccontains $name){$projection[$name]=$validation.$name}}; $result.validation=$projection
 if($validate.ExitCode -ne 0){$exit=if($validate.ExitCode -eq 2){2}else{1};Complete-Claim $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'plan.validation-failed' 'Plan validation failed.'}
 if($null -ne $filename){
  $candidates=[Collections.Generic.List[string]]::new()
  foreach($executablePlan in @($validation.plans)){
   if($executablePlan.PSObject.Properties.Name -ccontains 'path'){
    $candidate=[string]$executablePlan.path
    if([IO.Path]::GetFileName($candidate).Equals($filename,[StringComparison]::Ordinal)){$candidates.Add($candidate)}
   }
  }
  $candidates.Sort([StringComparer]::Ordinal)
  if($candidates.Count -eq 0){Complete-Claim 2 'blocked' 'plan-name-not-found' "No validated executable Plan has filename '$filename'."}
  if($candidates.Count -gt 1){$result.candidates=@($candidates);Complete-Claim 2 'blocked' 'plan-name-ambiguous' "Filename '$filename' matches multiple validated executable Plans; use a canonical Documents/Plans path."}
  $Plan=$candidates[0]
 }
	$receipt=Get-NextPlanClaimReceipt $context.Worktree
	if($null -ne $receipt){
		$status=Invoke-NextPlanProcess $context.WorktreeCli @('plan','claim-status','--worktree',$context.Worktree,'--claim-receipt',$receipt.Path,'--claim-receipt-sha256',$receipt.Sha256) $context.Worktree
		$claimStatus=ConvertFrom-NextPlanProcessJson $status 'plan claim-status'
		if($status.ExitCode -eq 0 -and $claimStatus.ownedByReceipt){ try { Assert-NextPlanClaimReceiptPlanBytes $receipt $context.Worktree } catch { Complete-Claim 2 'blocked' 'claim.plan-digest-mismatch' 'Claimed Plan bytes differ from the validated claim receipt.' }; $result.claim=[ordered]@{claimed=$true;plan=$receipt.Json.plan;state=$claimStatus.claimState}; Complete-Claim 0 'pass' 'reused' 'Existing Plan claim remains valid.' }
		if($status.ExitCode -eq 0){ Complete-Claim 2 'blocked' 'claim.receipt-not-owned' 'Deterministic Plan receipt is not the live claim owner.' }
		if($status.ExitCode -eq 2 -and $claimStatus.code -ceq 'receipt-invalid'){
			$unclaim=Invoke-NextPlanProcess $context.WorktreeCli @('plan','unclaim','--worktree',$context.Worktree,'--claim-receipt',$receipt.Path,'--claim-receipt-sha256',$receipt.Sha256) $context.Worktree
			$unclaimResult=ConvertFrom-NextPlanProcessJson $unclaim 'plan unclaim'
			if($unclaim.ExitCode -ne 0 -or $unclaimResult.code -cne 'already-absent'){ Complete-Claim $(if($unclaim.ExitCode -eq 2){2}else{1}) $(if($unclaim.ExitCode -eq 2){'blocked'}else{'error'}) 'claim.receipt-retirement-failed' 'Existing Plan receipt could not be authoritatively retired.' }
		} else { Complete-Claim $(if($status.ExitCode -eq 2){2}else{1}) $(if($status.ExitCode -eq 2){'blocked'}else{'error'}) 'claim.receipt-status-failed' 'Existing Plan receipt could not be validated.' }
		Remove-NextPlanClaimReceipt $receipt
	}
	$receiptPath=Get-NextPlanClaimReceiptPath $context.Worktree
 $args=@('plan','claim-next','--repo',$context.CommonDirectory,'--primary-worktree',$context.Primary,'--worktree',$context.Worktree,'--branch',$context.SessionBranch,'--owner',$context.Owner,'--session',$context.Session,'--write-claim-receipt',$receiptPath);if($Plan){$args+=@('--plan',$Plan)}
 $response=Invoke-NextPlanProcess $context.WorktreeCli $args $context.Worktree;$claim=ConvertFrom-NextPlanProcessJson $response 'plan claim-next'
 if($response.ExitCode -ne 0){$exit=if($response.ExitCode -eq 2){2}else{1};Complete-Claim $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'claim.rejected' 'WorktreeCli rejected the plan claim.'}
 if(-not $claim.claimed){Complete-Claim 0 'pass' 'none-available' 'No eligible Plans plan is available.'}
	$receipt=Get-NextPlanClaimReceipt $context.Worktree
	if($null -eq $receipt){throw 'WorktreeCli claimed a Plan without the deterministic receipt.'}
	$status=Invoke-NextPlanProcess $context.WorktreeCli @('plan','claim-status','--worktree',$context.Worktree,'--claim-receipt',$receipt.Path,'--claim-receipt-sha256',$receipt.Sha256) $context.Worktree
	$claimStatus=ConvertFrom-NextPlanProcessJson $status 'plan claim-status'
	if($status.ExitCode -ne 0 -or -not $claimStatus.ownedByReceipt){throw 'WorktreeCli did not validate the newly claimed receipt.'}
	try { Assert-NextPlanClaimReceiptPlanBytes $receipt $context.Worktree } catch { Complete-Claim 2 'blocked' 'claim.plan-digest-mismatch' 'Claimed Plan bytes differ from the validated claim receipt.' }
	$result.claim=[ordered]@{claimed=$true;plan=$receipt.Json.plan;state=$claimStatus.claimState}
 Complete-Claim 0 'pass' 'ok' 'Plan claimed from wrapper-derived state.'
} catch {if(Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue){if(Test-NextPlanStateBlocker $_){Complete-Claim 2 'blocked' 'claim.context-conflict' $_.Exception.Message}};Complete-Claim 1 'error' 'claim.failed' $_.Exception.Message}
