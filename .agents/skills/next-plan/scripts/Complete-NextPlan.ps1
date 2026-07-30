[CmdletBinding()]
param([switch]$Reject)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$result=[ordered]@{schemaVersion='broken-engine-next-plan-completion-result/v2';status='error';code='internal.error';message='Terminal preparation did not run.';workflowTerminal=$false;nextAction='finalize-changes';preparation=$null}
function Complete-Workflow([int]$ExitCode,[string]$Status,[string]$Code,[string]$Message){$result.status=$Status;$result.code=$Code;$result.message=$Message;[Console]::Out.Write(($result|ConvertTo-Json -Depth 100 -Compress));exit $ExitCode}
function Write-TerminalProof($Context,$Receipt,$Prepared) {
	if($Prepared.PSObject.Properties.Name -cnotcontains 'changedPaths' -or $Prepared.changedPaths -is [string] -or $Prepared.changedPaths -isnot [Collections.IEnumerable] -or $Prepared.PSObject.Properties.Name -cnotcontains 'manifestDigest' -or $Prepared.manifestDigest -isnot [string] -or $Prepared.manifestDigest -cnotmatch '^[0-9a-f]{64}$') { throw 'Terminal preparation response does not contain a canonical changed-path and manifest proof.' }
	$proof=[ordered]@{schemaVersion='broken-engine-next-plan-terminal-proof/v1';receiptSha256=$Receipt.Sha256;repository=$Receipt.Json.repository;worktree=$Receipt.Json.worktree;branch=$Receipt.Json.branch;disposition=$Prepared.disposition;claimState=$Prepared.claimState;changedPaths=@($Prepared.changedPaths);manifestDigest=$Prepared.manifestDigest}
	$path=Join-Path $Context.Worktree 'Temp\next-plan-terminal-result.json';$temporary="$path.$([guid]::NewGuid().ToString('N')).tmp"
	if (Test-Path -LiteralPath $path -PathType Leaf) {
		try { $existing=[IO.File]::ReadAllText($path,[Text.UTF8Encoding]::new($false,$true))|ConvertFrom-Json -Depth 32 -ErrorAction Stop }
		catch { throw 'Existing terminal proof is not valid UTF-8 JSON.' }
		if ($existing.schemaVersion -cne $proof.schemaVersion -or $existing.receiptSha256 -cne $proof.receiptSha256 -or $existing.repository -cne $proof.repository -or $existing.worktree -cne $proof.worktree -or $existing.branch -cne $proof.branch -or $existing.disposition -cne $proof.disposition -or $existing.claimState -cne $proof.claimState -or $existing.manifestDigest -cnotmatch '^[0-9a-f]{64}$' -or $existing.changedPaths -is [string] -or $existing.changedPaths -isnot [Collections.IEnumerable]) { throw 'Existing terminal proof does not match the deterministic receipt-bound terminal result.' }
		$result.terminalProof=[ordered]@{path=$path;receiptSha256=$Receipt.Sha256;disposition=$existing.disposition;changedPaths=@($existing.changedPaths);manifestDigest=$existing.manifestDigest};return
	}
	try { [IO.File]::WriteAllText($temporary,($proof|ConvertTo-Json -Depth 20 -Compress),[Text.UTF8Encoding]::new($false));Move-Item -LiteralPath $temporary -Destination $path -Force }
	finally { Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue }
	$result.terminalProof=[ordered]@{path=$path;receiptSha256=$Receipt.Sha256;disposition=$Prepared.disposition;changedPaths=@($Prepared.changedPaths);manifestDigest=$Prepared.manifestDigest}
}
try {
 Import-Module (Join-Path $PSScriptRoot 'NextPlanWorkflowCommon.psm1') -Force -DisableNameChecking
 $context=Get-NextPlanContext
	$receipt=Get-NextPlanClaimReceipt $context.Worktree
	if($null -eq $receipt){Complete-Workflow 0 'pass' 'no-claim' 'No Plan claim is present.'}
 $status=Invoke-NextPlanProcess $context.WorktreeCli @('plan','claim-status','--worktree',$context.Worktree,'--claim-receipt',$receipt.Path,'--claim-receipt-sha256',$receipt.Sha256) $context.Worktree
 $claimStatus=ConvertFrom-NextPlanProcessJson $status 'plan claim-status'
 if($status.ExitCode -ne 0){$exit=if($status.ExitCode -eq 2){2}else{1};Complete-Workflow $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'completion.claim-status-failed' 'WorktreeCli could not verify the Plan claim.'}
 if(-not $claimStatus.ownedByReceipt){Complete-Workflow 2 'blocked' 'completion.claim-lost' 'The Plan claim is no longer owned by this receipt.'}
	$expectedDisposition=if($Reject){'rejected'}else{'completed'}
	if($claimStatus.PSObject.Properties.Name -ccontains 'claimState' -and $claimStatus.claimState -ceq 'awaiting-landing' -and $claimStatus.PSObject.Properties.Name -ccontains 'disposition' -and $claimStatus.disposition -cne $expectedDisposition){Complete-Workflow 2 'blocked' 'completion.disposition-mismatch' 'Prepared Plan terminal disposition does not match the requested operation.'}
 $operation=if($Reject){'prepare-rejection'}else{'prepare-completion'}
 $args=@('plan',$operation,'--repo',$context.CommonDirectory,'--worktree',$context.Worktree,'--claim-receipt',$receipt.Path,'--claim-receipt-sha256',$receipt.Sha256)
 if($Reject){$args+='--user-authorized-rejection'}
 $response=Invoke-NextPlanProcess $context.WorktreeCli $args $context.Worktree;$prepared=ConvertFrom-NextPlanProcessJson $response $operation;$result.preparation=$prepared
 if($response.ExitCode -ne 0){$exit=if($response.ExitCode -eq 2){2}else{1};Complete-Workflow $exit $(if($exit -eq 2){'blocked'}else{'error'}) 'completion.prepare-failed' 'WorktreeCli did not prepare Plan terminal state.'}
 if($prepared.PSObject.Properties.Name -cnotcontains 'disposition' -or $prepared.disposition -isnot [string] -or $prepared.disposition -cne $expectedDisposition){Complete-Workflow 2 'blocked' 'completion.disposition-mismatch' 'Prepared Plan terminal disposition does not match the requested operation.'}
 if(-not $prepared.prepared -or $prepared.claimState -cne 'awaiting-landing'){throw 'Terminal preparation response does not prove awaiting-landing state.'}
	Write-TerminalProof $context $receipt $prepared
 Complete-Workflow 0 'pass' 'ok' 'Plan terminal state prepared; finalization is the mandatory next action.'
} catch {if(Get-Command Test-NextPlanStateBlocker -ErrorAction SilentlyContinue){if(Test-NextPlanStateBlocker $_){Complete-Workflow 2 'blocked' 'completion.context-conflict' $_.Exception.Message}};Complete-Workflow 1 'error' 'completion.failed' $_.Exception.Message}
