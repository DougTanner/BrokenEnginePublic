[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $Executable,
	[switch] $PreMaintenanceBaseline
)

$ErrorActionPreference = 'Stop'

$item = Get-Item -LiteralPath $Executable -Force -ErrorAction Stop
if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.Length -eq 0) {
	throw "Primary AgentCli executable must be a nonempty ordinary file: '$Executable'."
}

$help = @(& $Executable --help 2>&1)
if ($LASTEXITCODE -ne 0) {
	throw "Primary AgentCli executable does not support --help: '$Executable'."
}
$helpText = $help -join "`n"
$requiredCommands = @(
	'Usage: AgentCli.exe',
	'AgentCli.exe lock ',
	'AgentCli.exe plan ',
	'AgentCli.exe build ',
	'AgentCli.exe --help'
)
if (-not $PreMaintenanceBaseline) {
	$requiredCommands += @(
		'AgentCli.exe plan order validate --repo COMMON-DIR --worktree CHECKOUT',
		'AgentCli.exe plan order <add|update> --repo COMMON-DIR --worktree CHECKOUT --owner TOKEN --session TOKEN --request TEMP-REPO-REL',
		'AgentCli.exe plan order claim-next --repo COMMON-DIR --primary-worktree CHECKOUT --worktree CHECKOUT --branch TARGET --owner TOKEN --session TOKEN --queue <plans|features>',
		'AgentCli.exe plan order complete --repo COMMON-DIR --worktree CHECKOUT --owner TOKEN --session TOKEN --plan PATH [--reapply]'
	)
}
foreach ($requiredCommand in $requiredCommands) {
	if (-not $helpText.Contains($requiredCommand, [StringComparison]::Ordinal)) {
		throw "Primary AgentCli help is missing '$requiredCommand': '$Executable'."
	}
}
if ($helpText -match '(?im)^\s*AgentCli\.exe\s+install(?:\s|$)') {
	throw "Primary AgentCli help advertises the removed install command: '$Executable'."
}

Write-Output $helpText
