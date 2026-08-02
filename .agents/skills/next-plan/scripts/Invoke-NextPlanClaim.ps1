[CmdletBinding()]
param([string] $Plan)
$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest
$result=[ordered]@{schemaVersion='broken-engine-next-plan-claim-result/v3';status='error';code='internal.error';message='Claim did not run.';claim=$null}
function Complete-Claim([int]$ExitCode,[string]$Status,[string]$Code,[string]$Message){$result.status=$Status;$result.code=$Code;$result.message=$Message;[Console]::Out.Write(($result|ConvertTo-Json -Depth 100 -Compress));exit $ExitCode}
try {
 Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
 $pattern=$null
 if ($Plan) {
  $Plan=$Plan.Replace('\','/')
  Assert-NextPlanGitPath $Plan
  if ($Plan.StartsWith('Documents/Plans/',[StringComparison]::Ordinal)) {
   # WorktreeCli owns canonical path validation; preserve its exact-path contract.
  } else {
   $pattern=$Plan
   $Plan=$null
  }
 }
 $context=Get-NextPlanContext
 $status=Invoke-NextPlanProcess 'git.exe' @('-C',$context.Worktree,'status','--porcelain=v1','--untracked-files=all') $context.Worktree
 if($status.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($status.Stdout)){throw (New-NextPlanStateBlocker 'Session worktree must be clean before a plan claim.')}
 $validate=Invoke-NextPlanProcess $context.WorktreeCli @('plan','validate','--repo',$context.CommonDirectory,'--worktree',$context.Worktree) $context.Worktree
 $validation=ConvertFrom-NextPlanProcessJson $validate 'plan validate'; $projection=[ordered]@{}; foreach($name in @('status','code','message','diagnostics','notices','healedClaims')){if($validation.PSObject.Properties.Name -ccontains $name){$projection[$name]=$validation.$name}}; $result.validation=$projection
 if($validate.ExitCode -ne 0){$exit=if($validate.ExitCode -eq 2){2}else{1};Complete-Claim $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'plan.validation-failed' 'Plan validation failed.'}
 if($null -ne $pattern){
  $candidates=[Collections.Generic.List[string]]::new()
  foreach($executablePlan in @($validation.plans)){
   if($executablePlan.PSObject.Properties.Name -ccontains 'path'){
    $candidate=[string]$executablePlan.path
    # Match below Documents/Plans/ so a pattern naming that prefix cannot match every Plan.
    $relative=if($candidate.StartsWith('Documents/Plans/',[StringComparison]::Ordinal)){$candidate.Substring('Documents/Plans/'.Length)}else{$candidate}
    if($relative.Contains($pattern,[StringComparison]::Ordinal)){$candidates.Add($candidate)}
   }
  }
  $candidates.Sort([StringComparer]::Ordinal)
  if($candidates.Count -eq 0){Complete-Claim 2 'blocked' 'plan-name-not-found' "Pattern '$pattern' matched no validated executable Plan."}
  if($candidates.Count -gt 1){$result.candidates=@($candidates);Complete-Claim 2 'blocked' 'plan-name-ambiguous' "Pattern '$pattern' matched multiple validated executable Plans; use a canonical Documents/Plans path."}
  $Plan=$candidates[0]
 }
 $claimArguments=@('plan','claim-next','--repo',$context.CommonDirectory,'--primary-worktree',$context.Primary,'--worktree',$context.Worktree,'--branch',$context.SessionBranch,'--owner',$context.Owner,'--session',$context.Session);if($Plan){$claimArguments+=@('--plan',$Plan)}
 $response=Invoke-NextPlanProcess $context.WorktreeCli $claimArguments $context.Worktree;$claim=ConvertFrom-NextPlanProcessJson $response 'plan claim-next'
 if($response.ExitCode -ne 0){$exit=if($response.ExitCode -eq 2){2}else{1};Complete-Claim $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'claim.rejected' 'WorktreeCli rejected the plan claim.'}
 $code=[string]$claim.code
 if($code -ceq 'none'){Complete-Claim 0 'pass' 'none-available' 'No eligible Plans plan is available.'}
 if($code -cne 'claimed' -and $code -cne 'existing'){throw "plan claim-next returned an unknown result code '$code'."}
 $result.claim=[ordered]@{claimed=$true;plan=[string]$claim.plan;state=$code}
 if($code -ceq 'existing'){Complete-Claim 0 'pass' 'reused' 'Existing Plan claim remains live for this session.'}
 Complete-Claim 0 'pass' 'ok' 'Plan claimed for this session.'
} catch {if(Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue){if(Test-NextPlanStateBlocker $_){Complete-Claim 2 'blocked' 'claim.context-conflict' $_.Exception.Message}};Complete-Claim 1 'error' 'claim.failed' $_.Exception.Message}
