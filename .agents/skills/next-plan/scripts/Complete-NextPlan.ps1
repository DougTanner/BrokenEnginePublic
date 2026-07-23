[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ClaimReceipt,[Parameter(Mandatory=$true)][string]$ClaimReceiptSha256,[switch]$Reject)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$result=[ordered]@{schemaVersion='broken-engine-next-plan-completion-result/v2';status='error';code='internal.error';message='Terminal preparation did not run.';workflowTerminal=$false;nextAction='finalize-changes';preparation=$null}
function Complete-Workflow([int]$ExitCode,[string]$Status,[string]$Code,[string]$Message){$result.status=$Status;$result.code=$Code;$result.message=$Message;[Console]::Out.Write(($result|ConvertTo-Json -Depth 100 -Compress));exit $ExitCode}
try {
 Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
 $context=Get-NextPlanContext -AllowPrimaryAdvance
 if($ClaimReceiptSha256 -cnotmatch '^[0-9a-f]{64}$'){throw 'ClaimReceiptSha256 must be lowercase SHA-256.'}
 $receipt=Assert-NextPlanRepositoryPath $context.Worktree $ClaimReceipt 'Claim receipt'
 if((Get-NextPlanFileSha256 $receipt) -cne $ClaimReceiptSha256){Complete-Workflow 2 'blocked' 'completion.receipt-byte-mismatch' 'Claim receipt bytes changed.'}
 $status=Invoke-NextPlanProcess $context.WorktreeCli @('plan','claim-status','--worktree',$context.Worktree,'--claim-receipt',$receipt,'--claim-receipt-sha256',$ClaimReceiptSha256) $context.Worktree
 $claimStatus=ConvertFrom-NextPlanProcessJson $status 'plan claim-status'
 if($status.ExitCode -ne 0){$exit=if($status.ExitCode -eq 2){2}else{1};Complete-Workflow $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'completion.claim-status-failed' 'WorktreeCli could not verify the Plan claim.'}
 if(-not $claimStatus.ownedByReceipt){Complete-Workflow 2 'blocked' 'completion.claim-lost' 'The Plan claim is no longer owned by this receipt.'}
 $operation=if($Reject){'prepare-rejection'}else{'prepare-completion'}
 $args=@('plan',$operation,'--repo',$context.CommonDirectory,'--worktree',$context.Worktree,'--claim-receipt',$receipt,'--claim-receipt-sha256',$ClaimReceiptSha256)
 if($Reject){$args+='--user-authorized-rejection'}
 $response=Invoke-NextPlanProcess $context.WorktreeCli $args $context.Worktree;$prepared=ConvertFrom-NextPlanProcessJson $response $operation;$result.preparation=$prepared
 if($response.ExitCode -ne 0){$exit=if($response.ExitCode -eq 2){2}else{1};Complete-Workflow $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'completion.prepare-failed' 'WorktreeCli did not prepare Plan terminal state.'}
 $expectedDisposition=if($Reject){'rejected'}else{'completed'}
 if($prepared.PSObject.Properties.Name -cnotcontains 'disposition' -or $prepared.disposition -isnot [string] -or $prepared.disposition -cne $expectedDisposition){Complete-Workflow 2 'blocked' 'completion.disposition-mismatch' 'Prepared Plan terminal disposition does not match the requested operation.'}
 if(-not $prepared.prepared -or $prepared.claimState -cne 'awaiting-landing'){throw 'Terminal preparation response does not prove awaiting-landing state.'}
 Complete-Workflow 0 'pass' 'ok' 'Plan terminal state prepared; finalization is the mandatory next action.'
} catch {if(Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue){if(Test-NextPlanStateBlocker $_){Complete-Workflow 2 'blocked' 'completion.context-conflict' $_.Exception.Message}};Complete-Workflow 1 'error' 'completion.failed' $_.Exception.Message}
