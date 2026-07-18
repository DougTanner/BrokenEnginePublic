Set-StrictMode -Version Latest

$commonModule = Join-Path $PSScriptRoot 'FinalizeWorkflowCommon.psm1'
Import-Module $commonModule

function Get-AgentArtifactRoot([string] $Worktree) {
	$identity = Get-FinalizeGitIdentity $Worktree 'Agent artifact worktree'
	$localAppData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA')
	if ([string]::IsNullOrWhiteSpace($localAppData)) { throw 'LOCALAPPDATA is required for the agent artifact store.' }
	$commonBytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($identity.CommonDirectory.ToLowerInvariant())
	$repositoryId = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($commonBytes)).ToLowerInvariant()
	return Get-FinalizeRootPreservingFullPath (Join-Path $localAppData "BrokenEngine\AgentReports\$repositoryId")
}

function Ensure-AgentArtifactRoot([string] $Worktree) {
	$root = Get-AgentArtifactRoot $Worktree
	[IO.Directory]::CreateDirectory($root) | Out-Null
	return $root
}

function Test-AgentArtifactPathContainedBy([string] $Root, [string] $Candidate) {
	$relative = [IO.Path]::GetRelativePath($Root, $Candidate)
	return -not [IO.Path]::IsPathRooted($relative) -and $relative -ne '..' -and
		-not $relative.StartsWith("..$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::Ordinal) -and
		-not $relative.StartsWith("..$([IO.Path]::AltDirectorySeparatorChar)", [StringComparison]::Ordinal)
}

function Assert-AgentArtifactPath([string] $Worktree, [string] $Path) {
	$root = Ensure-AgentArtifactRoot $Worktree
	$rootItem = Get-Item -LiteralPath $root -Force -ErrorAction Stop
	if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse points are not allowed in the agent artifact root: '$root'." }
	$candidate = Get-FinalizeRootPreservingFullPath $Path
	if (-not (Test-AgentArtifactPathContainedBy $root $candidate)) { throw "Agent artifact path is not contained by '$root': '$candidate'." }
	if ([IO.Path]::GetDirectoryName($candidate) -cne $root) { throw "Agent artifact path must be a direct file beneath '$root'." }
	if (Test-Path -LiteralPath $candidate) {
		$candidateItem = Get-Item -LiteralPath $candidate -Force -ErrorAction Stop
		if (($candidateItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse points are not allowed in agent artifact paths: '$candidate'." }
	}
	$current = [IO.Path]::GetDirectoryName($candidate)
	while (-not $current.Equals($root, [StringComparison]::OrdinalIgnoreCase)) {
		$item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
		if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse points are not allowed in agent artifact paths: '$current'." }
		$current = Split-Path -Parent $current
	}
	return $candidate
}

function New-AgentArtifactPath([string] $Worktree, [string] $Purpose, [string] $Extension) {
	if ($Purpose -notmatch '^[a-z0-9][a-z0-9-]*$') { throw "Artifact purpose is invalid: '$Purpose'." }
	if ($Extension -cnotin @('md', 'json')) { throw "Artifact extension must be md or json: '$Extension'." }
	$root = Ensure-AgentArtifactRoot $Worktree
	$path = Join-Path $root (([guid]::NewGuid().ToString('N')) + "-$Purpose.$Extension")
	return Assert-AgentArtifactPath -Worktree $Worktree -Path $path
}

Export-ModuleMember -Function Get-AgentArtifactRoot,Ensure-AgentArtifactRoot,Assert-AgentArtifactPath,New-AgentArtifactPath
