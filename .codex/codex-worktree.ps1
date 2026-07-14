[CmdletBinding()]
param([switch] $LegacySessionsClosed)

if ($MyInvocation.InvocationName -eq '.') { return }

function Invoke-CodexWorktree([switch] $LegacySessionsClosed) {
	$repositoryRoot = git rev-parse --show-toplevel 2>$null

	if (($LASTEXITCODE -ne 0) -or [string]::IsNullOrWhiteSpace($repositoryRoot)) {
		throw 'codex-worktree must be run inside a Git repository.'
	}

	$repositoryRoot = [System.IO.Path]::GetFullPath($repositoryRoot.Trim())
	& (Join-Path $repositoryRoot '.agents\scripts\Start-AgentWorktreeSession.ps1') -Client codex -RepositoryRoot $repositoryRoot -LegacySessionsClosed:$LegacySessionsClosed -ClientArguments '--dangerously-bypass-approvals-and-sandbox'
	exit $LASTEXITCODE
}

Invoke-CodexWorktree -LegacySessionsClosed:$LegacySessionsClosed
