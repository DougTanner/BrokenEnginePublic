Set-StrictMode -Version Latest

$finalizeModule = Join-Path $PSScriptRoot 'FinalizeWorkflowCommon.psm1'
Import-Module $finalizeModule -Global -DisableNameChecking

# The scheduler owns receipt contents.  This module owns only the one local
# location from which workflow sidecars may supply those contents back to it.
function Get-PlanClaimReceiptPath {
	param([Parameter(Mandatory)][string] $Worktree)
	$root = Get-FinalizeExistingWindowsIdentity $Worktree 'Session worktree'
	$temp = Join-Path $root 'Temp'
	if (-not (Test-Path -LiteralPath $temp -PathType Container)) { throw 'Session Temp directory is missing.' }
	$tempItem = Get-Item -LiteralPath $temp -Force -ErrorAction Stop
	if (($tempItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Session Temp directory must not be a reparse point.' }
	$tempIdentity = Get-FinalizeExistingWindowsIdentity $temp 'Session Temp directory'
	if (-not $tempIdentity.Equals($temp,[StringComparison]::OrdinalIgnoreCase)) { throw 'Session Temp directory resolves outside the worktree.' }
	return Join-Path $temp 'next-plan-claim.json'
}

function Get-PlanClaimReceipt {
	param([Parameter(Mandatory)][string] $Worktree)
	$path = Get-PlanClaimReceiptPath $Worktree
	if (-not (Test-Path -LiteralPath $path)) { return $null }
	$item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
	if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Plan claim receipt must be an ordinary file.' }
	$identity = Get-FinalizeExistingWindowsIdentity $path 'Plan claim receipt'
	if (-not $identity.Equals($path,[StringComparison]::OrdinalIgnoreCase)) { throw 'Plan claim receipt resolves outside the deterministic Temp path.' }
	$bytes = [IO.File]::ReadAllBytes($path)
	if ($bytes.Length -eq 0) { throw 'Plan claim receipt is empty.' }
	try { $json = [Text.UTF8Encoding]::new($false,$true).GetString($bytes) | ConvertFrom-Json -Depth 20 -ErrorAction Stop }
	catch { throw "Plan claim receipt is not valid UTF-8 JSON: $($_.Exception.Message)" }
	foreach ($name in @('schemaVersion','repository','worktree','branch','owner','plan','planSha256')) {
		if ($json.PSObject.Properties.Name -cnotcontains $name) { throw "Plan claim receipt omits '$name'." }
	}
	if ($json.schemaVersion -ne 1 -or $json.planSha256 -isnot [string] -or $json.planSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Plan claim receipt schema is invalid.' }
	return [pscustomobject]@{ Path=$path; Sha256=([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()); Json=$json }
}

function Assert-PlanClaimReceiptPlanBytes {
	param([Parameter(Mandatory)] $Receipt,[Parameter(Mandatory)][string] $Worktree)
	$plan = [string]($Receipt.Json.plan)
	if ($plan -notlike 'Documents/Plans/*') { throw 'Plan claim receipt has an invalid Plan path.' }
	$full = [IO.Path]::GetFullPath((Join-Path $Worktree $plan.Replace('/','\')))
	$root = Get-FinalizeExistingWindowsIdentity $Worktree 'Session worktree'
	$relative = [IO.Path]::GetRelativePath($root,$full)
	if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith('..' + [IO.Path]::DirectorySeparatorChar)) { throw 'Plan claim receipt Plan path escapes the worktree.' }
	if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "Claimed Plan file is missing: '$full'." }
	$actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([IO.File]::ReadAllBytes($full))).ToLowerInvariant()
	if ($actual -cne $Receipt.Json.planSha256) { throw 'Claimed Plan bytes differ from the validated claim receipt.' }
}

function Remove-PlanClaimReceipt {
	param([Parameter(Mandatory)] $Receipt)
	# Re-read by expected bytes so a reparent replacement is never deleted.
	$current = Get-PlanClaimReceipt $Receipt.Json.worktree
	if ($null -eq $current) { return }
	if ($current.Sha256 -cne $Receipt.Sha256) { throw 'Plan claim receipt changed before local retirement.' }
	Remove-Item -LiteralPath $Receipt.Path -Force -ErrorAction Stop
}

Export-ModuleMember -Function Get-PlanClaimReceiptPath,Get-PlanClaimReceipt,Assert-PlanClaimReceiptPlanBytes,Remove-PlanClaimReceipt
