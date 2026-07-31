[CmdletBinding()]
param()
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$result=[ordered]@{schemaVersion='broken-engine-next-plan-deferral-result/v1';status='error';code='internal.error';message='Deferral did not run.';claim=$null}
function Complete-Deferral([int]$ExitCode,[string]$Status,[string]$Code,[string]$Message){$result.status=$Status;$result.code=$Code;$result.message=$Message;[Console]::Out.Write(($result|ConvertTo-Json -Depth 20 -Compress));exit $ExitCode}
try {
	Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
	$context=Get-NextPlanContext
	$claimArguments=@('--repo',$context.CommonDirectory,'--worktree',$context.Worktree,'--owner',$context.Owner,'--session',$context.Session)
	$status=Invoke-NextPlanProcess $context.WorktreeCli (@('plan','claim-status')+$claimArguments) $context.Worktree
	$claimStatus=ConvertFrom-NextPlanProcessJson $status 'plan claim-status'
	if($status.ExitCode -ne 0){Complete-Deferral $(if($status.ExitCode -eq 2){2}else{1}) $(if($status.ExitCode -eq 2){'blocked'}else{'error'}) 'defer.claim-status-failed' 'WorktreeCli could not report the Plan claim.'}
	if([string]$claimStatus.code -ceq 'none'){Complete-Deferral 0 'pass' 'no-claim' 'No Plan claim is present.'}
	$response=Invoke-NextPlanProcess $context.WorktreeCli (@('plan','unclaim')+$claimArguments) $context.Worktree
	$release=ConvertFrom-NextPlanProcessJson $response 'plan unclaim'
	if($response.ExitCode -ne 0 -or [string]$release.code -notin @('released','already-absent')){Complete-Deferral $(if($response.ExitCode -eq 2){2}else{1}) $(if($response.ExitCode -eq 2){'blocked'}else{'error'}) 'defer.unclaim-failed' 'WorktreeCli did not release the Plan claim.'}
	$result.claim=[ordered]@{released=$true};Complete-Deferral 0 'pass' ([string]$release.code) 'Plan claim deferred.'
} catch {if(Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue){if(Test-NextPlanStateBlocker $_){Complete-Deferral 2 'blocked' 'defer.context-conflict' $_.Exception.Message}};Complete-Deferral 1 'error' 'defer.failed' $_.Exception.Message}
