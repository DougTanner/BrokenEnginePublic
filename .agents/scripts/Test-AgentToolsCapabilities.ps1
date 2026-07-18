[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliExecutable,
	[Parameter(Mandatory = $true)]
	[string] $AgentHarnessExecutable
)

$ErrorActionPreference = 'Stop'

function Get-Help([string] $Executable, [string] $Name) {
	$item = Get-Item -LiteralPath $Executable -Force -ErrorAction Stop
	if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.Length -eq 0) { throw "$Name executable must be a nonempty ordinary file: '$Executable'." }
	$help = @(& $Executable --help 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "$Name executable does not support --help: '$Executable'." }
	return $help -join "`n"
}

function Assert-Contains([string] $Text, [string[]] $Required, [string] $Name) {
	foreach ($value in $Required) { if (-not $Text.Contains($value, [StringComparison]::Ordinal)) { throw "$Name help is missing '$value'." } }
}

$worktreeHelp = Get-Help $WorktreeCliExecutable 'WorktreeCli'
Assert-Contains $worktreeHelp @(
	'Usage: WorktreeCli.exe lock ',
	'WorktreeCli.exe plan ',
	'WorktreeCli.exe build ',
	'WorktreeCli.exe --help',
	'WorktreeCli.exe plan order init --repo COMMON-DIR --worktree CHECKOUT [--force]',
	'WorktreeCli.exe plan order validate --repo COMMON-DIR --worktree CHECKOUT',
	'WorktreeCli.exe plan order <add|update> --repo COMMON-DIR --worktree CHECKOUT --owner TOKEN --session TOKEN --request TEMP-REPO-REL',
	'WorktreeCli.exe plan order claim-next --repo COMMON-DIR --primary-worktree CHECKOUT --worktree CHECKOUT --branch TARGET --owner TOKEN --session TOKEN --queue <plans|features>',
	'WorktreeCli.exe plan order complete --repo COMMON-DIR --worktree CHECKOUT --owner TOKEN --session TOKEN --plan PATH'
) 'WorktreeCli'
if ($worktreeHelp.Contains('AgentHarness.exe', [StringComparison]::Ordinal) -or $worktreeHelp.Contains('--domain', [StringComparison]::Ordinal) -or $worktreeHelp.Contains('--port', [StringComparison]::Ordinal) -or $worktreeHelp.Contains('--timeout-ms', [StringComparison]::Ordinal) -or $worktreeHelp.Contains('--key', [StringComparison]::Ordinal)) { throw 'WorktreeCli help exposes a harness-only or obsolete option.' }

$harnessHelp = Get-Help $AgentHarnessExecutable 'AgentHarness'
Assert-Contains $harnessHelp @(
	'Usage: AgentHarness.exe [--owner TOKEN] --port N [--timeout-ms 15000] -',
	'AgentHarness.exe lock <token|claim|status|release|steal> ...',
	'AgentHarness.exe --help'
) 'AgentHarness'
if ($harnessHelp.Contains('WorktreeCli.exe', [StringComparison]::Ordinal) -or $harnessHelp.Contains('--domain', [StringComparison]::Ordinal) -or $harnessHelp.Contains('--repo', [StringComparison]::Ordinal) -or $harnessHelp.Contains(' plan ', [StringComparison]::Ordinal) -or $harnessHelp.Contains(' build ', [StringComparison]::Ordinal)) { throw 'AgentHarness help exposes a worktree-only or obsolete option.' }

[pscustomobject]@{ WorktreeCli = $worktreeHelp; AgentHarness = $harnessHelp } | ConvertTo-Json -Depth 3
