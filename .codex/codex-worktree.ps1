function codex-worktree {
	$codexCommand = Get-Command codex -CommandType Application -ErrorAction Stop
	$repositoryRoot = git rev-parse --show-toplevel 2>$null

	if (($LASTEXITCODE -ne 0) -or [string]::IsNullOrWhiteSpace($repositoryRoot)) {
		throw 'codex-worktree must be run inside a Git repository.'
	}

	$repositoryRoot = [System.IO.Path]::GetFullPath($repositoryRoot.Trim())
	$repositoryName = Split-Path -Leaf $repositoryRoot
	$targetBranch = (git -C $repositoryRoot branch --show-current).Trim()
	$baseline = (git -C $repositoryRoot rev-parse HEAD).Trim()
	$uuid = [guid]::NewGuid().ToString()
	$branchName = "codex/$uuid"
	$worktreeRoot = Join-Path $HOME ".codex\worktrees\$repositoryName"
	$worktreePath = Join-Path $worktreeRoot $uuid

	New-Item -ItemType Directory -Path $worktreeRoot -Force | Out-Null
	git -C $repositoryRoot worktree add -b $branchName $worktreePath $baseline

	if ($LASTEXITCODE -ne 0) {
		throw "Failed to create Git worktree at $worktreePath."
	}

	try {
		& (Join-Path $repositoryRoot '.agents\scripts\Provision-WorktreeThirdParty.ps1') -RepositoryRoot $worktreePath
		if ($LASTEXITCODE -ne 0) { throw "Provisioner exited with code $LASTEXITCODE." }
	}
	catch {
		throw "ThirdParty provisioning failed. Preserved worktree '$worktreePath' and branch '$branchName' for recovery. $($_.Exception.Message)"
	}

	$provenanceNames = @('BROKEN_ENGINE_WORKTREE_PATH', 'BROKEN_ENGINE_SESSION_BRANCH', 'BROKEN_ENGINE_PRIMARY_CHECKOUT', 'BROKEN_ENGINE_TARGET_BRANCH', 'BROKEN_ENGINE_BASELINE')
	foreach ($name in $provenanceNames) { Remove-Item "Env:$name" -ErrorAction SilentlyContinue }
	$env:BROKEN_ENGINE_WORKTREE_PATH = $worktreePath
	$env:BROKEN_ENGINE_SESSION_BRANCH = $branchName
	$env:BROKEN_ENGINE_PRIMARY_CHECKOUT = $repositoryRoot
	$env:BROKEN_ENGINE_TARGET_BRANCH = $targetBranch
	$env:BROKEN_ENGINE_BASELINE = $baseline

	Write-Host "Created worktree $worktreePath on branch $branchName"
	Push-Location -LiteralPath $worktreePath

	try {
		& $codexCommand.Source --dangerously-bypass-approvals-and-sandbox
	}
	finally {
		Pop-Location
	}
}
