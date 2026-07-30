[CmdletBinding()]
param()
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$result=[ordered]@{schemaVersion='broken-engine-next-plan-deferral-result/v1';status='error';code='internal.error';message='Deferral did not run.';claim=$null}
function Complete-Deferral([int]$ExitCode,[string]$Status,[string]$Code,[string]$Message){$result.status=$Status;$result.code=$Code;$result.message=$Message;[Console]::Out.Write(($result|ConvertTo-Json -Depth 20 -Compress));exit $ExitCode}
try {
	Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
	$context=Get-NextPlanContext;$receipt=Get-NextPlanClaimReceipt $context.Worktree
	if($null -eq $receipt){Complete-Deferral 0 'pass' 'no-claim' 'No Plan claim is present.'}
	$status=Invoke-NextPlanProcess $context.WorktreeCli @('plan','claim-status','--worktree',$context.Worktree,'--claim-receipt',$receipt.Path,'--claim-receipt-sha256',$receipt.Sha256) $context.Worktree
	$claimStatus=ConvertFrom-NextPlanProcessJson $status 'plan claim-status'
	if($status.ExitCode -ne 0){Complete-Deferral $(if($status.ExitCode -eq 2){2}else{1}) $(if($status.ExitCode -eq 2){'blocked'}else{'error'}) 'defer.claim-status-failed' 'WorktreeCli could not verify the Plan claim.'}
	if(-not $claimStatus.ownedByReceipt -or $claimStatus.claimState -cne 'claimed'){Complete-Deferral 2 'blocked' 'defer.not-ordinary-claim' 'Only an ordinary live Plan claim may be deferred.'}
	$response=Invoke-NextPlanProcess $context.WorktreeCli @('plan','unclaim','--worktree',$context.Worktree,'--claim-receipt',$receipt.Path,'--claim-receipt-sha256',$receipt.Sha256) $context.Worktree
	$release=ConvertFrom-NextPlanProcessJson $response 'plan unclaim'
	if($response.ExitCode -ne 0 -or $release.code -notin @('released','already-absent')){Complete-Deferral $(if($response.ExitCode -eq 2){2}else{1}) $(if($response.ExitCode -eq 2){'blocked'}else{'error'}) 'defer.unclaim-failed' 'WorktreeCli did not prove Plan deferral.'}
	Remove-NextPlanClaimReceipt $receipt;$result.claim=[ordered]@{released=$true};Complete-Deferral 0 'pass' $release.code 'Plan claim deferred.'
} catch { Complete-Deferral 1 'error' 'defer.failed' $_.Exception.Message }
