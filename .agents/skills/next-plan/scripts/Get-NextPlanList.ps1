[CmdletBinding()]
param()
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$result=[ordered]@{schemaVersion='broken-engine-next-plan-list-error/v1';status='error';code='internal.error';message='Listing did not run.'}
function Complete-Listing([int]$ExitCode,[string]$Status,[string]$Code,[string]$Message){$result.status=$Status;$result.code=$Code;$result.message=$Message;[Console]::Out.Write(($result|ConvertTo-Json -Depth 20 -Compress));exit $ExitCode}
try {
	Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
	$context=Get-NextPlanContext
	$response=Invoke-NextPlanProcess $context.WorktreeCli @('plan','list','--repo',$context.CommonDirectory,'--worktree',$context.Worktree) $context.Worktree
	if($response.ExitCode -ne 0){Complete-Listing $(if($response.ExitCode -eq 2){2}else{1}) $(if($response.ExitCode -eq 2){'blocked'}else{'error'}) 'list.failed' 'WorktreeCli could not list the executable Plans.'}
	# WorktreeCli owns the listing contract, so this script only proves the response is one JSON value and emits it unchanged.
	ConvertFrom-NextPlanProcessJson $response 'plan list' | Out-Null
	[Console]::Out.Write($response.Stdout.Trim())
	exit 0
} catch {if(Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue){if(Test-NextPlanStateBlocker $_){Complete-Listing 2 'blocked' 'list.context-conflict' $_.Exception.Message}};Complete-Listing 1 'error' 'list.failed' $_.Exception.Message}
