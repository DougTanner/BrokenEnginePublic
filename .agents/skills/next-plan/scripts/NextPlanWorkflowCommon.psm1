Set-StrictMode -Version Latest

$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
Import-Module (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1') -Force
Import-Module (Join-Path $sharedScripts 'AgentWorktreeSession.psm1') -Force -DisableNameChecking

function New-NextPlanStateBlocker([string] $Message) {
	$exception = [InvalidOperationException]::new($Message)
	$exception.Data['NextPlanExitCode'] = 2
	return $exception
}
function Test-NextPlanStateBlocker($ErrorRecord) {
	return $null -ne $ErrorRecord -and $null -ne $ErrorRecord.Exception -and $ErrorRecord.Exception.Data.Contains('NextPlanExitCode') -and [int]$ErrorRecord.Exception.Data['NextPlanExitCode'] -eq 2
}
function Get-NextPlanContext {
	try {
		$session = Get-AgentWorktreeSessionContext -Worktree (Get-Location).Path
		if ([string]::IsNullOrWhiteSpace($session.SessionId)) { throw (New-NextPlanStateBlocker 'The current checkout is not an agent session worktree (no claude/<uuid> or codex/<uuid> branch).') }

		$worktree = Get-FinalizeGitIdentity $session.Worktree 'Session worktree'
		$current = Get-FinalizeExistingWindowsIdentity (Get-Location).Path 'Current directory'
		if (-not $current.Equals($worktree.Worktree,[StringComparison]::OrdinalIgnoreCase)) { throw (New-NextPlanStateBlocker 'Current directory and session worktree identities do not match.') }
		$primary = Get-FinalizeGitIdentity $session.PrimaryRoot 'Session primary checkout'
		if (-not $primary.CommonDirectory.Equals($worktree.CommonDirectory,[StringComparison]::OrdinalIgnoreCase)) { throw (New-NextPlanStateBlocker 'Session primary and worktree do not share a Git common directory.') }
		$primaryDotGit = Get-Item -LiteralPath (Join-Path $primary.Worktree '.git') -Force -ErrorAction Stop
		if (-not $primaryDotGit.PSIsContainer -or ($primaryDotGit.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw (New-NextPlanStateBlocker 'Session primary checkout is not the primary checkout.') }

		# The session runs the primary checkout's WorktreeCli through a directory link, so a wrong link
		# target or a placeholder executable would silently run a foreign binary.
		$relativeOutput = 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
		$sessionOutput = Join-Path $worktree.Worktree $relativeOutput; $primaryOutput = Join-Path $primary.Worktree $relativeOutput
		if (-not (Test-Path -LiteralPath $sessionOutput) -or -not (Test-Path -LiteralPath $primaryOutput)) { throw (New-NextPlanStateBlocker 'The session or primary WorktreeCli Output is missing.') }
		$sessionOutputItem = Get-Item -LiteralPath $sessionOutput -Force -ErrorAction Stop; $primaryOutputItem = Get-Item -LiteralPath $primaryOutput -Force -ErrorAction Stop
		if (-not $sessionOutputItem.PSIsContainer -or ($sessionOutputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) { throw (New-NextPlanStateBlocker "Session WorktreeCli Output is not a directory reparse point: '$sessionOutput'.") }
		if (-not $primaryOutputItem.PSIsContainer -or ($primaryOutputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw (New-NextPlanStateBlocker "Primary WorktreeCli Output is not an ordinary directory: '$primaryOutput'.") }
		$resolvedSessionOutput = Get-FinalizeExistingWindowsIdentity $sessionOutput 'Session WorktreeCli Output'; $resolvedPrimaryOutput = Get-FinalizeExistingWindowsIdentity $primaryOutput 'Primary WorktreeCli Output'
		if (-not $resolvedSessionOutput.Equals($resolvedPrimaryOutput,[StringComparison]::OrdinalIgnoreCase)) { throw (New-NextPlanStateBlocker 'Session WorktreeCli Output target is not primary Output.') }
		$worktreeCli = Join-Path $sessionOutput 'WorktreeCli.exe'; $worktreeCliItem = Get-Item -LiteralPath $worktreeCli -Force -ErrorAction Stop
		if ($worktreeCliItem.PSIsContainer -or ($worktreeCliItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $worktreeCliItem.Length -eq 0) { throw (New-NextPlanStateBlocker 'Provisioned WorktreeCli must be a nonempty ordinary file.') }

		return [pscustomobject]@{ Worktree=$worktree.Worktree; Primary=$primary.Worktree; CommonDirectory=$worktree.CommonDirectory; SessionBranch=$session.Branch; TargetBranch=$session.PrimaryBranch; Baseline=$session.Baseline; Owner=$session.SessionId; Session=$session.SessionId; WorktreeCli=(Get-Item -LiteralPath $worktreeCli -Force).FullName }
	} catch { if (Test-NextPlanStateBlocker $_) { throw $_.Exception }; throw (New-NextPlanStateBlocker $_.Exception.Message) }
}
function Invoke-NextPlanProcess([string] $Executable,[string[]] $Arguments,[string] $WorkingDirectory) { return Invoke-FinalizeNativeText -Executable $Executable -Arguments $Arguments -WorkingDirectory $WorkingDirectory }
function ConvertFrom-NextPlanProcessJson($Response,[string] $Operation) { if ([string]::IsNullOrWhiteSpace($Response.Stdout)) { throw "$Operation returned empty stdout. $($Response.Stderr.Trim())" }; try { return $Response.Stdout.Trim() | ConvertFrom-Json -Depth 100 -ErrorAction Stop } catch { throw "$Operation did not return one JSON value. $($_.Exception.Message)" } }
function Assert-NextPlanGitPath([string] $Path) { Assert-FinalizeGitPath $Path }
Export-ModuleMember -Function New-NextPlanStateBlocker,Test-NextPlanStateBlocker,Get-NextPlanContext,Invoke-NextPlanProcess,ConvertFrom-NextPlanProcessJson,Assert-NextPlanGitPath
