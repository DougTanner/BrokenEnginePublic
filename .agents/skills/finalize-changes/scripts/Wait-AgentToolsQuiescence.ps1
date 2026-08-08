# Read-only deadline-limited wait for the shared AgentTools promotion gate. The caller
# owns any retry loop and reconciliation; this script never asks for authority.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $RepositoryRoot,
	[string] $CooperatingSessionOwner,
	[ValidateRange(0, 55)]
	[int] $WaitSeconds = 55
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'WorktreeCliSessionExclusion.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
Import-Module (Join-Path $sharedScripts 'WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking

try {
	$result = Wait-WorktreeCliSharedQuiescence -RepositoryRoot $RepositoryRoot -CooperatingSessionOwner $CooperatingSessionOwner -WaitSeconds $WaitSeconds
	Write-Output ($result | ConvertTo-Json -Depth 16 -Compress)
	exit 0
}
catch {
	$ledgerFailure = $_.Exception.Message -match '^WorktreeCli session ledger (is unreadable or malformed|failed validation|contains an invalid)'
	$result = [ordered]@{
		schemaVersion = 'broken-engine-shared-quiescence/v1'
		disposition = if ($ledgerFailure) { 'authority-required' } else { 'terminal' }
		requiresUserAuthority = $ledgerFailure
		retryAfterSeconds = 0
		waitedMilliseconds = 0
		liveBlockers = @()
		message = $_.Exception.Message
	}
	Write-Output ($result | ConvertTo-Json -Depth 16 -Compress)
	exit $(if ($ledgerFailure) { 2 } else { 1 })
}
