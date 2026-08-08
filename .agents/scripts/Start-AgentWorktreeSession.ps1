[CmdletBinding()]
param(
	[Parameter(Mandatory)][ValidateSet('claude', 'codex')][string] $Client,
	[Parameter(Mandatory)][string] $RepositoryRoot,
	[string] $ClientExecutable,
	[string[]] $ClientArguments = @(),
	[string] $ReattachWorktree,
	[int] $WaitSeconds = 660
)

$ErrorActionPreference = 'Stop'
if ($Client -cne 'claude' -and $Client -cne 'codex') { throw "Client must be lowercase 'claude' or 'codex'." }

# Claude carries client arguments out of band because -File treats a leading dash as a
# PowerShell parameter. Do not clear the transport variable until reattach validation succeeds:
# rejected reattach attempts must leave the caller environment unchanged.
$clearClaudeArgumentTransport = $false
if ($Client -ceq 'claude' -and -not [string]::IsNullOrEmpty($env:BROKEN_ENGINE_CLIENT_ARGUMENTS))
{
	$decoded = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($env:BROKEN_ENGINE_CLIENT_ARGUMENTS))
	$parts = @($decoded -split "`0")
	if ($parts.Count -ge 2) { $ClientArguments = @($parts[0..($parts.Count - 2)]) }
	$clearClaudeArgumentTransport = $true
}

Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot 'AgentWorktreeSession.psm1') -Force -DisableNameChecking

$environmentNames = @(
	'BROKEN_ENGINE_CLIENT_ARGUMENTS',
	'BROKEN_ENGINE_SESSION_OWNER',
	'BROKEN_ENGINE_WORKTREE_PATH',
	'BROKEN_ENGINE_SESSION_BRANCH',
	'BROKEN_ENGINE_PRIMARY_CHECKOUT',
	'BROKEN_ENGINE_TARGET_BRANCH',
	'BROKEN_ENGINE_BASELINE',
	'BROKEN_ENGINE_AGENT_CLIENT'
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) { $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }

function Restore-AgentWorktreeEnvironment {
	foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process') }
}

function Set-AgentWorktreeEnvironment([object] $Identity, [string] $Owner) {
	$env:BROKEN_ENGINE_SESSION_OWNER = $Owner
	$env:BROKEN_ENGINE_WORKTREE_PATH = $Identity.Worktree
	$env:BROKEN_ENGINE_SESSION_BRANCH = $Identity.Branch
	$env:BROKEN_ENGINE_PRIMARY_CHECKOUT = $Identity.Primary.Root
	$env:BROKEN_ENGINE_TARGET_BRANCH = $Identity.TargetBranch
	$env:BROKEN_ENGINE_BASELINE = $Identity.Baseline
	$env:BROKEN_ENGINE_AGENT_CLIENT = $Client
}

function Assert-AgentWorktreeSkillsLink([string] $Worktree) {
	$skillsLink = Join-Path $Worktree '.claude\skills'
	$skillsItem = Get-Item -LiteralPath $skillsLink -Force -ErrorAction SilentlyContinue
	if ($null -eq $skillsItem -or -not $skillsItem.PSIsContainer -or -not ($skillsItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
		$null -eq (Get-ChildItem -LiteralPath $skillsLink -ErrorAction SilentlyContinue | Select-Object -First 1)) {
		throw "'.claude\skills' in worktree '$Worktree' did not check out as a working directory link, so agent skills are unavailable. Enable Windows Developer Mode (Settings -> System -> For developers -> Developer Mode -> On), open a new terminal, then restore the link: git -C '$Worktree' config core.symlinks true; git -C '$Worktree' checkout -- .claude/skills"
	}
}

$worktreeCreated = $false
$exitCode = 1
try {
	$primary = Get-AgentWorktreePrimaryIdentity $RepositoryRoot
	$root = $primary.Root
	$worktreeCli = Join-Path $root 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	$identity = $null
	$rebaseInProgress = $false

	if (-not [string]::IsNullOrWhiteSpace($ReattachWorktree)) {
		# Reattach identity is Git-derived: the worktree must be uniquely registered against this primary
		# checkout and sit on a session branch whose uuid is the session owner. An in-progress rebase is
		# reported, not repaired - the session resolves it itself.
		$reattachTarget = Get-AgentCanonicalPath $ReattachWorktree
		$records = @(Get-AgentWorktreeRecords $root | Where-Object { -not $_.Bare -and $_.Path.Equals($reattachTarget, [StringComparison]::OrdinalIgnoreCase) })
		if ($records.Count -ne 1 -or $records[0].Prunable) { throw "Worktree '$reattachTarget' is not uniquely registered against '$root', or is prunable." }
		$branch = $records[0].Branch
		foreach ($marker in @('rebase-merge', 'rebase-apply')) {
			$markerPath = @(Invoke-AgentGit @('-C', $reattachTarget, 'rev-parse', '--path-format=absolute', '--git-path', $marker))[0].Trim()
			if (-not (Test-Path -LiteralPath $markerPath)) { continue }
			$rebaseInProgress = $true
			# A rebase detaches HEAD, so the worktree registration reports no branch: recover the branch
			# identity from the rebase's own record of where it will land.
			$headName = Join-Path $markerPath 'head-name'
			if ([string]::IsNullOrWhiteSpace($branch) -and (Test-Path -LiteralPath $headName)) { $branch = (Get-Content -LiteralPath $headName -Raw).Trim() -replace '^refs/heads/', '' }
		}
		if ($branch -cnotmatch "^$Client/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$") { throw "Worktree '$reattachTarget' is not checked out on a $Client session branch: '$branch'." }
		$owner = $branch.Substring($Client.Length + 1)
		$baseline = @(Invoke-AgentGit @('-C', $reattachTarget, 'merge-base', 'HEAD', $primary.Head))[0].Trim()
		$identity = [pscustomobject]@{ Primary = $primary; Worktree = $reattachTarget; Branch = $branch; TargetBranch = $primary.Branch; Baseline = $baseline }
	}
	else {
		$repositoryName = Split-Path -Leaf $root
		$uuid = [guid]::NewGuid().ToString()
		$branch = "$Client/$uuid"
		$clientHome = if ($Client -ceq 'claude') { '.claude' } else { '.codex' }
		$worktreeRoot = Join-Path $HOME "$clientHome\worktrees\$repositoryName"
		$worktree = Join-Path $worktreeRoot $uuid
		if (Test-Path -LiteralPath $worktree) { throw "Generated worktree path already exists: '$worktree'." }
		if (@(Invoke-AgentGit @('-C', $root, 'branch', '--list', $branch)).Count -ne 0) { throw "Generated branch already exists: '$branch'." }
		# The branch uuid is the session owner, so reattach recovers the same identity from Git alone.
		$owner = $uuid
		$identity = [pscustomobject]@{ Primary = $primary; Worktree = $worktree; Branch = $branch; TargetBranch = $primary.Branch; Baseline = $primary.Head }
		New-Item -ItemType Directory -Path $worktreeRoot -Force | Out-Null
		& git -C $root worktree add -b $branch $worktree $primary.Head
		if ($LASTEXITCODE -ne 0) { throw "Failed to create worktree '$worktree'." }
		$worktreeCreated = $true
	}

	# Both paths reach here with a validated identity and its durable session owner.
	Set-AgentWorktreeEnvironment $identity $owner
	# Record the branch this session lands onto; Get-AgentWorktreeSessionContext refuses to resolve a
	# session worktree without it. Rewritten on every create and reattach, so it reflects the primary
	# branch observed at the most recent wrapper start.
	$sidecarDirectory = Join-Path $identity.Worktree 'Temp'
	New-Item -ItemType Directory -Path $sidecarDirectory -Force | Out-Null
	$sidecar = [ordered]@{ schemaVersion = 'broken-engine-session-sidecar/v1'; targetBranch = $identity.TargetBranch } | ConvertTo-Json
	[IO.File]::WriteAllText((Join-Path $sidecarDirectory 'session-sidecar.json'), $sidecar, [Text.UTF8Encoding]::new($false))
	& (Join-Path $root '.agents\scripts\Bootstrap-AgentTools.ps1') -RepositoryRoot $root -WaitSeconds $WaitSeconds
	& (Join-Path $root '.agents\scripts\Provision-WorktreeThirdParty.ps1') -RepositoryRoot $identity.Worktree -WaitSeconds $WaitSeconds
	Assert-AgentWorktreeSkillsLink $identity.Worktree
	if ($clearClaudeArgumentTransport) { $env:BROKEN_ENGINE_CLIENT_ARGUMENTS = $null }
	if ([string]::IsNullOrWhiteSpace($ClientExecutable)) {
		$ClientExecutable = (Get-Command $Client -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
	}
	# Skip the DataPacker build while a rebase is in progress: a tree with conflict markers cannot build,
	# and a wrapper exit here would block the very session that must resolve the conflict from launching.
	# The session runs the build itself after `git rebase --continue`. Provisioning above is conflict-insensitive.
	if (-not $rebaseInProgress) {
		& (Join-Path $root '.agents\scripts\Build-WorktreeDataPacker.ps1') -Worktree $identity.Worktree -WorktreeCliExecutable $worktreeCli -PrimaryCheckout $root
	}
	$banner = "$(if ($worktreeCreated) { 'Created' } else { 'Reattached' }) worktree $($identity.Worktree) on branch $($identity.Branch) at baseline $($identity.Baseline)."
	if ($rebaseInProgress) {
		$buildCommand = "'.agents\scripts\Build-WorktreeDataPacker.ps1' -Worktree '$($identity.Worktree)' -WorktreeCliExecutable '$worktreeCli' -PrimaryCheckout '$root'"
		$banner = "FIRST TASK: this worktree has a rebase in progress - resolve your rebase first. Fix the conflicted files, run 'git rebase --continue', then run $buildCommand to build the deferred DataPacker.`n" + $banner
	}
	Write-Host $banner
	$exitCode = Invoke-WorktreeCliTrackedProcess -Executable $ClientExecutable -ArgumentList $ClientArguments -WorkingDirectory $identity.Worktree
}
catch {
	[Console]::Error.WriteLine($_.Exception.Message)
	if ($worktreeCreated) { [Console]::Error.WriteLine("Preserved partial worktree '$($identity.Worktree)' and branch '$($identity.Branch)' for recovery.") }
	$exitCode = 1
}
finally {
	Restore-AgentWorktreeEnvironment
}
exit $exitCode
