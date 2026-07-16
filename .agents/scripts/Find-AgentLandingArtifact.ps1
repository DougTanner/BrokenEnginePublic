[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $RepositoryRoot,
	[string] $Commit = 'HEAD'
)

$ErrorActionPreference = 'Stop'
$commonModule = Join-Path $PSScriptRoot 'FinalizeWorkflowCommon.psm1'
$module = Join-Path $PSScriptRoot 'AgentArtifactStore.psm1'
Import-Module $commonModule -Force
Import-Module $module -Force -DisableNameChecking

$identity = Get-FinalizeGitIdentity $RepositoryRoot 'Artifact repository'
$commitHash = (Invoke-FinalizeGit $identity.Worktree @('rev-parse', "$Commit^{commit}")).Trim()
$path = Get-AgentLandingArtifactPath -Worktree $identity.Worktree -Commit $commitHash
if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
	[pscustomobject]@{ schema = 'broken-engine-agent-landing-artifact-lookup/v1'; status = 'missing'; commit = $commitHash; artifactPath = $path } | ConvertTo-Json -Depth 8
}
else {
	$canonical = Assert-AgentArtifactPath -Worktree $identity.Worktree -Path $path
	$utf8 = [Text.UTF8Encoding]::new($false, $true)
	try { $artifact = $utf8.GetString([IO.File]::ReadAllBytes($canonical)) | ConvertFrom-Json -Depth 32 -ErrorAction Stop }
	catch { throw "Landing artifact is not valid strict UTF-8 JSON: '$canonical'." }
	if ($artifact.schema -ne 'broken-engine-agent-landing-artifact/v1' -or $artifact.commit -ne $commitHash) {
		throw "Landing artifact does not bind commit '$commitHash': '$canonical'."
	}
	[pscustomobject]@{ schema = 'broken-engine-agent-landing-artifact-lookup/v1'; status = 'found'; commit = $commitHash; artifactPath = $canonical; artifact = $artifact } | ConvertTo-Json -Depth 32
}
