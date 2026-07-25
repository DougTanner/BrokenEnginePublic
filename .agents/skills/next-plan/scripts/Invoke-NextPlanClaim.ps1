[CmdletBinding()]
param([string] $Plan)
$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest
$result=[ordered]@{schemaVersion='broken-engine-next-plan-claim-result/v2';status='error';code='internal.error';message='Claim did not run.';receipt=$null;claim=$null}
function Complete-Claim([int]$ExitCode,[string]$Status,[string]$Code,[string]$Message){$result.status=$Status;$result.code=$Code;$result.message=$Message;[Console]::Out.Write(($result|ConvertTo-Json -Depth 100 -Compress));exit $ExitCode}
try {
 Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
 if ($Plan) { $Plan=$Plan.Replace('\','/'); Assert-NextPlanGitPath $Plan; if (-not $Plan.StartsWith('Documents/Plans/',[StringComparison]::Ordinal)) { throw 'Only Documents/Plans paths are scheduler inputs.' } }
 $context=Get-NextPlanContext
 $status=Invoke-NextPlanProcess 'git.exe' @('-C',$context.Worktree,'status','--porcelain=v1','--untracked-files=all') $context.Worktree
 if($status.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($status.Stdout)){throw (New-NextPlanStateBlocker 'Session worktree must be clean before a plan claim.')}
 $validate=Invoke-NextPlanProcess $context.WorktreeCli @('plan','validate','--repo',$context.CommonDirectory,'--worktree',$context.Worktree,'--baseline',$context.Baseline) $context.Worktree
 $validation=ConvertFrom-NextPlanProcessJson $validate 'plan validate'; $projection=[ordered]@{}; foreach($name in @('status','code','message','diagnostics','notices','healedClaims')){if($validation.PSObject.Properties.Name -ccontains $name){$projection[$name]=$validation.$name}}; $result.validation=$projection
 if($validate.ExitCode -ne 0){$exit=if($validate.ExitCode -eq 2){2}else{1};Complete-Claim $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'plan.validation-failed' 'Plan validation failed.'}
 $receiptPath=Join-Path $context.Worktree ('Temp\next-plan-claim-'+[Guid]::NewGuid().ToString('N')+'.json')
 $args=@('plan','claim-next','--repo',$context.CommonDirectory,'--primary-worktree',$context.Primary,'--worktree',$context.Worktree,'--branch',$context.SessionBranch,'--owner',$context.Owner,'--session',$context.Session,'--write-claim-receipt',$receiptPath);if($Plan){$args+=@('--plan',$Plan)}
 $response=Invoke-NextPlanProcess $context.WorktreeCli $args $context.Worktree;$claim=ConvertFrom-NextPlanProcessJson $response 'plan claim-next';$result.claim=$claim
 if($response.ExitCode -ne 0){$exit=if($response.ExitCode -eq 2){2}else{1};Complete-Claim $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'claim.rejected' 'WorktreeCli rejected the plan claim.'}
 if(-not $claim.claimed){Complete-Claim 0 'pass' 'none-available' 'No eligible Plans plan is available.'}
 foreach($name in @('plan','digest','receipt')){if($claim.PSObject.Properties.Name -cnotcontains $name){throw "Claim response omitted '$name'."}}
 if([string]$claim.plan -notlike 'Documents/Plans/*' -or [string]$claim.digest -cnotmatch '^[0-9a-f]{64}$'){throw 'Claim response has an invalid plan identity.'}
 $receipt=$claim.receipt; foreach($name in @('path','sha256','size')){if($receipt.PSObject.Properties.Name -cnotcontains $name){throw "Claim receipt omitted '$name'."}}
 $receiptPath=Assert-NextPlanRepositoryPath $context.Worktree ([string]$receipt.path) 'Claim receipt'
 if([string]$receipt.sha256 -cnotmatch '^[0-9a-f]{64}$' -or (Get-NextPlanFileSha256 $receiptPath) -cne $receipt.sha256){throw 'Claim receipt digest does not match durable receipt bytes.'}
 $result.receipt=[ordered]@{path=$receiptPath;sha256=[string]$receipt.sha256;bytes=[int64]$receipt.size}
 Complete-Claim 0 'pass' 'ok' 'Plan claimed from wrapper-derived state.'
} catch {if(Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue){if(Test-NextPlanStateBlocker $_){Complete-Claim 2 'blocked' 'claim.context-conflict' $_.Exception.Message}};Complete-Claim 1 'error' 'claim.failed' $_.Exception.Message}
