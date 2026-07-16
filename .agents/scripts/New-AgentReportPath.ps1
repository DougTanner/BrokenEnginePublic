[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $Worktree,
	[Parameter(Mandatory)][string] $Purpose
)

$ErrorActionPreference = 'Stop'
$module = Join-Path $PSScriptRoot 'AgentArtifactStore.psm1'
Import-Module $module -Force -DisableNameChecking

New-AgentReportPath -Worktree $Worktree -Purpose $Purpose
