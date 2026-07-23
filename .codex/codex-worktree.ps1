[CmdletBinding()]
param(
	[switch] $LegacySessionsClosed,
	[string] $ReattachWorktree
)

if ($MyInvocation.InvocationName -eq '.') { return }

function Invoke-CodexWorktree([switch] $LegacySessionsClosed, [string] $ReattachWorktree) {
	$repositoryRoot = git rev-parse --show-toplevel 2>$null

	if (($LASTEXITCODE -ne 0) -or [string]::IsNullOrWhiteSpace($repositoryRoot)) {
		throw 'codex-worktree must be run inside a Git repository.'
	}

	$repositoryRoot = [System.IO.Path]::GetFullPath($repositoryRoot.Trim())
	$startArguments = @{
		Client = 'codex'; RepositoryRoot = $repositoryRoot; LegacySessionsClosed = [bool]$LegacySessionsClosed
		ClientArguments = @('--dangerously-bypass-approvals-and-sandbox')
	}
	if (-not [string]::IsNullOrWhiteSpace($ReattachWorktree)) { $startArguments.ReattachWorktree = $ReattachWorktree }
	& (Join-Path $repositoryRoot '.agents\scripts\Start-AgentWorktreeSession.ps1') @startArguments
	exit $LASTEXITCODE
}

Invoke-CodexWorktree -LegacySessionsClosed:$LegacySessionsClosed -ReattachWorktree $ReattachWorktree
