[CmdletBinding()]
param([Parameter(Mandatory)][string] $Worktree,[Parameter(Mandatory)][string] $Baseline)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$result=[ordered]@{schemaVersion='broken-engine-verify-plan-prevalidation/v1';status='error';code='internal.error';message='Plan prevalidation did not run.';claim=[ordered]@{present=$false;state='absent';disposition='none';terminalProof=$false};validation=$null}
function Complete([int]$Exit,[string]$Status,[string]$Code,[string]$Message){$result.status=$Status;$result.code=$Code;$result.message=$Message;[Console]::Out.Write(($result|ConvertTo-Json -Depth 20 -Compress));exit $Exit}
try {
	if($Baseline -cnotmatch '^[0-9a-f]{40}$'){throw 'Baseline must be a lowercase 40-character commit ID.'}
	$root=[IO.Path]::GetFullPath($Worktree);if(-not [IO.Path]::IsPathRooted($Worktree)){throw 'Worktree must be absolute.'}
	$shared=Join-Path $PSScriptRoot '..\..\..\scripts';if(-not(Test-Path (Join-Path $shared 'PlanClaimReceipt.psm1'))){$shared=Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'}
	Import-Module (Join-Path $shared 'FinalizeWorkflowCommon.psm1') -Force -DisableNameChecking
	Import-Module (Join-Path $shared 'AgentWorktreeSession.psm1') -Force -DisableNameChecking
	Import-Module (Join-Path $shared 'PlanClaimReceipt.psm1') -Force -DisableNameChecking
	$provenance=Get-AgentWorktreeSessionProvenance -Worktree $root
	$identity=Get-FinalizeGitIdentity $provenance.Worktree 'Session worktree'
	if($identity.Worktree -cne $root -or $provenance.Baseline -cne $Baseline){throw 'Supplied worktree or baseline does not match wrapper provenance.'}
	$primary=Get-FinalizeGitIdentity $provenance.Primary 'Session primary checkout';if(-not $primary.CommonDirectory.Equals($identity.CommonDirectory,[StringComparison]::OrdinalIgnoreCase)){throw 'Session primary and worktree do not share a Git common directory.'}
	$relativeOutput='Tools\WorktreeCli\Platforms\VisualStudio2026\Output';$sessionOutput=Join-Path $identity.Worktree $relativeOutput;$primaryOutput=Join-Path $primary.Worktree $relativeOutput
	$sessionOutputItem=Get-Item -LiteralPath $sessionOutput -Force -ErrorAction Stop;$primaryOutputItem=Get-Item -LiteralPath $primaryOutput -Force -ErrorAction Stop
	if(-not $primaryOutputItem.PSIsContainer -or ($primaryOutputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Primary WorktreeCli Output must be an ordinary directory.'}
	if(-not $sessionOutputItem.PSIsContainer -or ($sessionOutputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0){throw 'Session WorktreeCli Output must be a directory reparse point.'}
	$resolvedSessionOutput=Get-FinalizeExistingWindowsIdentity $sessionOutput 'Session WorktreeCli Output';$resolvedPrimaryOutput=Get-FinalizeExistingWindowsIdentity $primaryOutput 'Primary WorktreeCli Output'
	if(-not $resolvedSessionOutput.Equals($resolvedPrimaryOutput,[StringComparison]::OrdinalIgnoreCase)){Complete 2 'blocked' 'context.output-wrong-target' 'Session WorktreeCli Output does not target primary Output.'}
	$cli=Join-Path $sessionOutput 'WorktreeCli.exe';$item=Get-Item -LiteralPath $cli -Force -ErrorAction Stop;if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $item.Length -eq 0){throw 'Provisioned WorktreeCli is not an ordinary nonempty file.'}
	$receipt=Get-PlanClaimReceipt $identity.Worktree;$args=@('plan','validate','--repo',$identity.CommonDirectory,'--worktree',$identity.Worktree,'--baseline',$Baseline)
	if($null -ne $receipt){
		$status=Invoke-FinalizeNativeText $cli @('plan','claim-status','--worktree',$identity.Worktree,'--claim-receipt',$receipt.Path,'--claim-receipt-sha256',$receipt.Sha256) $identity.Worktree
		$claimStatus=$status.Stdout.Trim()|ConvertFrom-Json -Depth 20
		if($status.ExitCode -ne 0 -or -not $claimStatus.ownedByReceipt){Complete 2 'blocked' 'claim.status-failed' 'Deterministic Plan receipt is not a live owned claim.'}
		$result.claim.present=$true;$result.claim.state=[string]$claimStatus.claimState;if($claimStatus.PSObject.Properties.Name -ccontains 'disposition'){$result.claim.disposition=[string]$claimStatus.disposition}
		if($result.claim.state -eq 'awaiting-landing' -and $result.claim.disposition -in @('completed','rejected')){$result.claim.terminalProof=$true}
		if($result.claim.state -eq 'claimed'){Assert-PlanClaimReceiptPlanBytes $receipt $identity.Worktree}elseif(-not $result.claim.terminalProof){Complete 2 'blocked' 'claim.terminal-proof-failed' 'Present Plan receipt is neither an ordinary claim nor a proven awaiting-landing terminal claim.'}
		if($result.claim.terminalProof){$args+=@('--terminal-receipt',$receipt.Path,'--terminal-receipt-sha256',$receipt.Sha256)}
	}
	$response=Invoke-FinalizeNativeText $cli $args $identity.Worktree;$validation=$response.Stdout.Trim()|ConvertFrom-Json -Depth 20
	$projection=[ordered]@{};foreach($n in @('status','code','message','diagnostics','notices','healedClaims')){if($validation.PSObject.Properties.Name -ccontains $n){$projection[$n]=$validation.$n}};$result.validation=$projection
	if($response.ExitCode -ne 0 -or $validation.status -cne 'valid' -or $validation.code -cne 'ok'){Complete 2 'blocked' 'validation.failed' 'WorktreeCli rejected Plan validation.'}
	Complete 0 'pass' 'ok' 'Plan prevalidation passed.'
}catch{Complete 1 'error' 'prevalidation.failed' $_.Exception.Message}
