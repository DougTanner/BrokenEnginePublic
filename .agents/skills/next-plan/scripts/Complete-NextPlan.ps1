[CmdletBinding()]
param([switch]$Reject)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$result=[ordered]@{schemaVersion='broken-engine-next-plan-completion-result/v3';status='error';code='internal.error';message='Terminal preparation did not run.';messageLength=33;messageTruncated=$false;workflowTerminal=$false;nextAction='finalize-changes';claim=[ordered]@{state='absent';disposition='none'};changes=[ordered]@{totalCount=0;items=@();truncated=$false;selector=$null;requery=$null};diagnostics=[ordered]@{totalCount=0;items=@();truncated=$false;selector=$null;requery=$null}}
function Get-BoundedText([AllowNull()]$Value,[int]$Limit) { if($null -eq $Value){return [pscustomobject]@{Text=$null;Length=0;Truncated=$false}};$Value=[string]$Value;$length=$Value.Length;[pscustomobject]@{Text=$(if($length -gt $Limit){$Value.Substring(0,$Limit)}else{$Value});Length=$length;Truncated=($length -gt $Limit)} }
function Set-ResultMessage([string]$Message) {$bounded=Get-BoundedText $Message 512;$result.message=$bounded.Text;$result.messageLength=$bounded.Length;$result.messageTruncated=$bounded.Truncated}
function Add-Diagnostic([string]$Source,[string]$Code,[AllowNull()][string]$Path,[string]$Message) {$codeText=Get-BoundedText $Code 128;$pathText=Get-BoundedText $Path 1024;$messageText=Get-BoundedText $Message 512;$result.diagnostics.items=@([ordered]@{source=$Source;code=$codeText.Text;codeLength=$codeText.Length;codeTruncated=$codeText.Truncated;path=$pathText.Text;pathLength=$pathText.Length;pathTruncated=$pathText.Truncated;message=$messageText.Text;messageLength=$messageText.Length;messageTruncated=$messageText.Truncated});$result.diagnostics.totalCount=1}
function Complete-Workflow([int]$ExitCode,[string]$Status,[string]$Code,[string]$Message){$result.status=$Status;$result.code=(Get-BoundedText $Code 128).Text;Set-ResultMessage $Message;[Console]::Out.Write(($result|ConvertTo-Json -Depth 10 -Compress));exit $ExitCode}
function Set-ChangedPaths($ChangedPaths) {$paths=@($ChangedPaths|ForEach-Object{[string]$_});[Array]::Sort($paths,[StringComparer]::Ordinal);$items=@($paths|Select-Object -First 16|ForEach-Object{$text=Get-BoundedText $_ 1024;[ordered]@{path=$text.Text;pathLength=$text.Length;pathTruncated=$text.Truncated}});$result.changes=[ordered]@{totalCount=$paths.Count;items=$items;truncated=($paths.Count -gt 16);selector=$null;requery=$null}}
try {
 Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
 $context=Get-NextPlanContext
 $claimArguments=@('--repo',$context.CommonDirectory,'--worktree',$context.Worktree,'--owner',$context.Owner,'--session',$context.Session)
 $status=Invoke-NextPlanProcess $context.WorktreeCli (@('plan','claim-status')+$claimArguments) $context.Worktree
 $claimStatus=ConvertFrom-NextPlanProcessJson $status 'plan claim-status'
 if($status.ExitCode -ne 0){$exit=if($status.ExitCode -eq 2){2}else{1};Complete-Workflow $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'completion.claim-status-failed' 'WorktreeCli could not report the Plan claim.'}
 if([string]$claimStatus.code -ceq 'none'){Complete-Workflow 0 'pass' 'no-claim' 'No Plan claim is present.'}
 $operation=if($Reject){'reject'}else{'complete'}
 $expectedDisposition=if($Reject){'rejected'}else{'completed'}
 $arguments=@('plan',$operation)+$claimArguments
 if($Reject){$arguments+='--user-authorized-rejection'}
 $response=Invoke-NextPlanProcess $context.WorktreeCli $arguments $context.Worktree;$prepared=ConvertFrom-NextPlanProcessJson $response $operation
 if($response.ExitCode -ne 0){$exit=if($response.ExitCode -eq 2){2}else{1};$downstreamCode=if($prepared.PSObject.Properties.Name -ccontains 'code'){[string]$prepared.code}else{'unknown'};$downstreamPath=if($prepared.PSObject.Properties.Name -ccontains 'plan'){[string]$prepared.plan}else{$null};$downstreamMessage=if($prepared.PSObject.Properties.Name -ccontains 'message'){[string]$prepared.message}else{'WorktreeCli did not return a diagnostic message.'};Add-Diagnostic 'WorktreeCli' $downstreamCode $downstreamPath $downstreamMessage;$result.diagnostics.requery='Complete-NextPlan';Complete-Workflow $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'completion.prepare-failed' 'WorktreeCli did not prepare Plan terminal state.'}
 if([string]$prepared.code -cne $expectedDisposition){Complete-Workflow 2 'blocked' 'completion.disposition-mismatch' 'Prepared Plan terminal disposition does not match the requested operation.'}
 $result.claim=[ordered]@{state='claimed';disposition=$expectedDisposition};Set-ChangedPaths $prepared.changedPaths
 Complete-Workflow 0 'pass' 'ok' 'Plan terminal state prepared; finalization is the mandatory next action.'
} catch {if(Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue){if(Test-NextPlanStateBlocker $_){Complete-Workflow 2 'blocked' 'completion.context-conflict' $_.Exception.Message}};Complete-Workflow 1 'error' 'completion.failed' $_.Exception.Message}
