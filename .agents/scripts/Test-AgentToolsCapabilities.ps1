
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
	'WorktreeCli.exe plan validate ',
	'WorktreeCli.exe plan claim-next ',
	'--primary-worktree',
	'claim-status',
	'unclaim',
	'complete',
	'reject',
	'--user-authorized-rejection'
) 'WorktreeCli'
# Retired commands and options are usage errors and must never be advertised again.
foreach ($retired in @('release-after-landing', 'reparent-claims', 'prepare-completion', 'prepare-rejection', '--write-claim-receipt', '--claim-receipt', '--terminal-receipt', '--landed-commit', '--baseline')) {
	if ($worktreeHelp.Contains($retired, [StringComparison]::Ordinal)) { throw "WorktreeCli help still advertises the retired '$retired' surface." }
}
if ($worktreeHelp.Contains('AgentHarness.exe', [StringComparison]::Ordinal) -or $worktreeHelp.Contains('--domain', [StringComparison]::Ordinal) -or $worktreeHelp.Contains('--port', [StringComparison]::Ordinal) -or $worktreeHelp.Contains('--timeout-ms', [StringComparison]::Ordinal) -or $worktreeHelp.Contains('--key', [StringComparison]::Ordinal)) { throw 'WorktreeCli help exposes a harness-only or obsolete option.' }

$harnessHelp = Get-Help $AgentHarnessExecutable 'AgentHarness'
Assert-Contains $harnessHelp @(
	'Usage: AgentHarness.exe --owner TOKEN --port N [--timeout-ms 15000] -',
	'AgentHarness.exe --owner TOKEN --port N [--timeout-ms 15000] "<json>"',
	'AgentHarness.exe lock <token|claim|status|release|steal|heartbeat> ...',
	'AgentHarness.exe --help'
) 'AgentHarness'
if ($harnessHelp.Contains('[--owner TOKEN]', [StringComparison]::Ordinal) -or $harnessHelp.Contains('WorktreeCli.exe', [StringComparison]::Ordinal) -or $harnessHelp.Contains('--domain', [StringComparison]::Ordinal) -or $harnessHelp.Contains('--repo', [StringComparison]::Ordinal) -or $harnessHelp.Contains(' plan ', [StringComparison]::Ordinal) -or $harnessHelp.Contains(' build ', [StringComparison]::Ordinal)) { throw 'AgentHarness help exposes a worktree-only or obsolete option.' }

[pscustomobject]@{ WorktreeCli = $worktreeHelp; AgentHarness = $harnessHelp } | ConvertTo-Json -Depth 3
