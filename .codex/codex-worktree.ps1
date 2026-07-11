function codex-worktree {
	$codexCommand = Get-Command codex -CommandType Application -ErrorAction Stop
	$repositoryRoot = git rev-parse --show-toplevel 2>$null

	if (($LASTEXITCODE -ne 0) -or [string]::IsNullOrWhiteSpace($repositoryRoot)) {
		throw 'codex-worktree must be run inside a Git repository.'
	}

	$repositoryRoot = [System.IO.Path]::GetFullPath($repositoryRoot.Trim())
	$repositoryName = Split-Path -Leaf $repositoryRoot
	$uuid = [guid]::NewGuid().ToString()
	$branchName = "codex/$uuid"
	$worktreeRoot = Join-Path $HOME ".codex\worktrees\$repositoryName"
	$worktreePath = Join-Path $worktreeRoot $uuid

	New-Item -ItemType Directory -Path $worktreeRoot -Force | Out-Null
	git -C $repositoryRoot worktree add -b $branchName $worktreePath HEAD

	if ($LASTEXITCODE -ne 0) {
		throw "Failed to create Git worktree at $worktreePath."
	}

	Write-Host "Created worktree $worktreePath on branch $branchName"
	Push-Location -LiteralPath $worktreePath

	try {
		& $codexCommand.Source --dangerously-bypass-approvals-and-sandbox
	}
	finally {
		Pop-Location
	}
}
