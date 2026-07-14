[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $AgentCli,

	[Parameter(Mandatory = $true)]
	[string] $GitCommonDir,

	[Parameter(Mandatory = $true)]
	[string] $OrderPath,

	[Parameter(Mandatory = $true)]
	[string] $Plan,

	[Parameter(Mandatory = $true)]
	[string] $Owner,

	[Parameter(Mandatory = $true)]
	[string] $Session,

	[Parameter(Mandatory = $true)]
	[string] $Worktree
)

$ErrorActionPreference = 'Stop'

$claimExitCode = 1
$unlockExitCode = 1
$claimOutput = @()
$unlockOutput = @()

try {
	$claimOutput = @(& $AgentCli plan row claim --repo $GitCommonDir --order $OrderPath --plan $Plan --owner $Owner --session $Session --worktree $Worktree 2>&1 | ForEach-Object { "$_" })
	$claimExitCode = $LASTEXITCODE
}
catch {
	$claimOutput = @("$($_.Exception.Message)")
}
finally {
	try {
		$unlockOutput = @(& $AgentCli plan queue unlock --repo $GitCommonDir --order $OrderPath --owner $Owner 2>&1 | ForEach-Object { "$_" })
		$unlockExitCode = $LASTEXITCODE
	}
	catch {
		$unlockOutput = @("$($_.Exception.Message)")
	}
}

if ($unlockExitCode -ne 0) {
	$result = 'unlock_failed'
	$exitCode = 1
}
elseif ($claimExitCode -eq 0) {
	$result = 'claimed'
	$exitCode = 0
}
elseif ($claimExitCode -eq 2) {
	$result = 'conflict'
	$exitCode = 2
}
else {
	$result = 'claim_failed'
	$exitCode = 1
}

[PSCustomObject]@{
	result = $result
	claimExitCode = $claimExitCode
	unlockExitCode = $unlockExitCode
	claimOutput = $claimOutput
	unlockOutput = $unlockOutput
} | ConvertTo-Json -Compress -Depth 3

exit $exitCode
