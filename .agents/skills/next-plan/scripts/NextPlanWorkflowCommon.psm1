Set-StrictMode -Version Latest

$script:NextPlanUtf8 = [Text.UTF8Encoding]::new($false, $true)
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
Import-Module (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1') -Force
Import-Module (Join-Path $sharedScripts 'AgentArtifactStore.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $sharedScripts 'WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking

function New-NextPlanStateBlocker([string] $Message) {
	$exception = [InvalidOperationException]::new($Message)
	$exception.Data['NextPlanExitCode'] = 2
	return $exception
}
function Test-NextPlanStateBlocker($ErrorRecord) {
	return $null -ne $ErrorRecord -and $null -ne $ErrorRecord.Exception -and $ErrorRecord.Exception.Data.Contains('NextPlanExitCode') -and [int]$ErrorRecord.Exception.Data['NextPlanExitCode'] -eq 2
}
function Get-NextPlanEnvironmentValue([string] $Name) {
	$value = [Environment]::GetEnvironmentVariable($Name)
	if ([string]::IsNullOrWhiteSpace($value) -or $value.IndexOf("`r", [StringComparison]::Ordinal) -ge 0 -or $value.IndexOf("`n", [StringComparison]::Ordinal) -ge 0) { throw "Wrapper environment variable $Name is missing or invalid." }
	return $value
}
function Get-NextPlanContext([switch] $AllowPrimaryAdvance) {
	try {
		if ((Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE') -cne 'session') { throw 'The wrapper admission mode is not session.' }
		$worktreeValue = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_WORKTREE_PATH'
		$sessionWorktreeValue = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE'
		$primaryValue = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_PRIMARY_CHECKOUT'
		$sessionBranch = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_SESSION_BRANCH'
		$targetBranch = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_TARGET_BRANCH'
		$baseline = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_BASELINE'
		$owner = Get-NextPlanEnvironmentValue 'BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER'
		if ($baseline -cnotmatch '^[0-9a-f]{40}$') { throw 'BROKEN_ENGINE_BASELINE is not a lowercase commit ID.' }

		$worktree = Get-FinalizeGitIdentity $worktreeValue 'Wrapper session worktree'
		$sessionWorktree = Get-FinalizeExistingWindowsIdentity $sessionWorktreeValue 'Registered wrapper session worktree'
		$current = Get-FinalizeExistingWindowsIdentity (Get-Location).Path 'Current directory'
		if (-not $sessionWorktree.Equals($worktree.Worktree,[StringComparison]::OrdinalIgnoreCase) -or -not $current.Equals($worktree.Worktree,[StringComparison]::OrdinalIgnoreCase)) { throw (New-NextPlanStateBlocker 'Current, registered, and wrapper session worktree identities do not match.') }
		$primary = Get-FinalizeGitIdentity $primaryValue 'Wrapper primary checkout'
		if (-not $primary.CommonDirectory.Equals($worktree.CommonDirectory,[StringComparison]::OrdinalIgnoreCase)) { throw (New-NextPlanStateBlocker 'Wrapper primary and session worktrees do not share a Git common directory.') }
		$primaryDotGit = Get-Item -LiteralPath (Join-Path $primary.Worktree '.git') -Force -ErrorAction Stop
		if (-not $primaryDotGit.PSIsContainer -or ($primaryDotGit.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw (New-NextPlanStateBlocker 'BROKEN_ENGINE_PRIMARY_CHECKOUT is not the primary checkout.') }
		if ($worktree.Branch -cne $sessionBranch -or $primary.Branch -cne $targetBranch) { throw (New-NextPlanStateBlocker 'Wrapper branch identities do not match the attached Git branches.') }
		if ($worktree.Head -cne $baseline) { throw (New-NextPlanStateBlocker 'Session worktree HEAD moved from the wrapper baseline.') }
		if (-not $AllowPrimaryAdvance -and $primary.Head -cne $baseline) { throw (New-NextPlanStateBlocker 'Primary HEAD advanced from the wrapper baseline; perform primary-advance recovery first.') }
		$classification = Get-WorktreeCliSessionClassification -RepositoryRoot $worktree.Worktree -Owner $owner -Worktree $worktree.Worktree
		if ($classification.Classification -cne 'expected-live') { throw (New-NextPlanStateBlocker "Wrapper WorktreeCli session classification is '$($classification.Classification)', expected 'expected-live'.") }

		$relativeOutput = 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
		$sessionOutput = Join-Path $worktree.Worktree $relativeOutput; $primaryOutput = Join-Path $primary.Worktree $relativeOutput
		if (-not (Test-Path -LiteralPath $sessionOutput) -or -not (Test-Path -LiteralPath $primaryOutput)) { throw (New-NextPlanStateBlocker 'The session or primary WorktreeCli Output is missing.') }
		$sessionOutputItem = Get-Item -LiteralPath $sessionOutput -Force -ErrorAction Stop; $primaryOutputItem = Get-Item -LiteralPath $primaryOutput -Force -ErrorAction Stop
		if (-not $sessionOutputItem.PSIsContainer -or ($sessionOutputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) { throw (New-NextPlanStateBlocker "Session WorktreeCli Output is not a directory reparse point: '$sessionOutput'.") }
		if (-not $primaryOutputItem.PSIsContainer -or ($primaryOutputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw (New-NextPlanStateBlocker "Primary WorktreeCli Output is not an ordinary directory: '$primaryOutput'.") }
		$resolvedSessionOutput = Get-FinalizeExistingWindowsIdentity $sessionOutput 'Session WorktreeCli Output'; $resolvedPrimaryOutput = Get-FinalizeExistingWindowsIdentity $primaryOutput 'Primary WorktreeCli Output'
		if (-not $resolvedSessionOutput.Equals($resolvedPrimaryOutput,[StringComparison]::OrdinalIgnoreCase)) { throw (New-NextPlanStateBlocker "Session WorktreeCli Output target is not primary Output.") }
		$worktreeCli = Join-Path $sessionOutput 'WorktreeCli.exe'; $worktreeCliItem = Get-Item -LiteralPath $worktreeCli -Force -ErrorAction Stop
		if ($worktreeCliItem.PSIsContainer -or ($worktreeCliItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $worktreeCliItem.Length -eq 0) { throw (New-NextPlanStateBlocker 'Provisioned WorktreeCli must be a nonempty ordinary file.') }
		$helpResponse = Invoke-FinalizeNativeText $worktreeCli @('--help') $worktree.Worktree
		if ($helpResponse.ExitCode -ne 0) { throw (New-NextPlanStateBlocker 'Provisioned WorktreeCli does not support --help.') }
		foreach ($capability in @(
			'WorktreeCli.exe plan validate --repo COMMON-DIR --worktree CHECKOUT --baseline COMMIT',
			'WorktreeCli.exe plan claim-next --repo COMMON-DIR --primary-worktree PRIMARY --worktree SESSION --branch TARGET --owner TOKEN --session TOKEN --write-claim-receipt Temp/RECEIPT [--plan Documents/Plans/...md]',
			'WorktreeCli.exe plan claim-status|unclaim --worktree SESSION --claim-receipt Temp/RECEIPT --claim-receipt-sha256 SHA256',
			'WorktreeCli.exe plan prepare-completion|prepare-rejection --worktree SESSION --claim-receipt Temp/RECEIPT --claim-receipt-sha256 SHA256'
		)) { if (-not $helpResponse.Stdout.Contains($capability,[StringComparison]::Ordinal)) { throw (New-NextPlanStateBlocker "Provisioned WorktreeCli help is missing '$capability'.") } }
		return [pscustomobject]@{ Worktree=$worktree.Worktree; Primary=$primary.Worktree; CommonDirectory=$worktree.CommonDirectory; SessionBranch=$sessionBranch; TargetBranch=$targetBranch; Baseline=$baseline; Owner=$owner; Session=$owner; WorktreeCli=(Get-Item -LiteralPath $worktreeCli -Force).FullName }
	} catch { if (Test-NextPlanStateBlocker $_) { throw $_.Exception }; throw (New-NextPlanStateBlocker $_.Exception.Message) }
}
function Invoke-NextPlanProcess([string] $Executable,[string[]] $Arguments,[string] $WorkingDirectory) { return Invoke-FinalizeNativeText -Executable $Executable -Arguments $Arguments -WorkingDirectory $WorkingDirectory }
function ConvertFrom-NextPlanProcessJson($Response,[string] $Operation) { if ([string]::IsNullOrWhiteSpace($Response.Stdout)) { throw "$Operation returned empty stdout. $($Response.Stderr.Trim())" }; try { return $Response.Stdout.Trim() | ConvertFrom-Json -Depth 100 -ErrorAction Stop } catch { throw "$Operation did not return one JSON value. $($_.Exception.Message)" } }
function Get-NextPlanSha256([byte[]] $Bytes) { return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant() }
function Get-NextPlanFileSha256([string] $Path) { return Get-NextPlanSha256 ([IO.File]::ReadAllBytes($Path)) }
function Assert-NextPlanRepositoryPath([string] $Worktree,[string] $Path,[string] $Label) { $full=Get-FinalizeRootPreservingFullPath $Path; $relative=[IO.Path]::GetRelativePath($Worktree,$full); if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith("..$([IO.Path]::DirectorySeparatorChar)",[StringComparison]::Ordinal)) { throw "$Label must be contained by the session worktree." }; $existing=Get-FinalizeExistingWindowsIdentity $full $Label; if (-not $existing.Equals($full,[StringComparison]::OrdinalIgnoreCase)) { throw "$Label uses a reparse point." }; return $full }
function Assert-NextPlanGitPath([string] $Path) { Assert-FinalizeGitPath $Path }
Export-ModuleMember -Function New-NextPlanStateBlocker,Test-NextPlanStateBlocker,Get-NextPlanContext,Invoke-NextPlanProcess,ConvertFrom-NextPlanProcessJson,Get-NextPlanSha256,Get-NextPlanFileSha256,Assert-NextPlanRepositoryPath,Assert-NextPlanGitPath
