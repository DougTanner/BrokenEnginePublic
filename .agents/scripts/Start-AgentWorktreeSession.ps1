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
	$status = @(Invoke-AgentGit @('-C', $root, 'status', '--porcelain', '--untracked-files=all'))
	if ([string]::IsNullOrWhiteSpace($ReattachWorktree) -and $status.Count -ne 0) { throw "Primary checkout must be clean before session creation: $($status -join '; ')." }
	$worktreeCli = Join-Path $root 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	$identity = $null
	$repair = $null

	if (-not [string]::IsNullOrWhiteSpace($ReattachWorktree)) {
		# Always run the squash-repair sidecar on reattach; never gate it on a merge-base probe. After a
		# conflicted re-parent the receipt baseline already equals the new tip, so a probe would read
		# "baseline is an ancestor of primary" and skip repair even though the worktree still carries an
		# unresolved rebase or autostash pop-conflict - which would then build DataPacker on a conflict tree
		# and launch with no FIRST-TASK banner. Let the sidecar decide: it returns 'not-needed' for a healthy
		# session (cheap, no mutation, and it fails a tampered receipt closed before touching anything), and
		# 'reparented'/'reparented-conflict' otherwise, and that status alone drives the banner and DataPacker
		# skip below. Keep the fail-closed missing-CLI check - the sidecar needs WorktreeCli to move the
		# claim, and a reattach always has it from the prior session's build.
		$reattachTarget = Get-AgentCanonicalPath $ReattachWorktree
		if (-not (Test-Path -LiteralPath $worktreeCli)) { throw "WorktreeCli is unavailable to validate or re-parent the retained session; run a fresh session first to rebuild AgentTools." }
		$repairScript = Join-Path $root '.agents\scripts\Repair-AgentWorktreeSquashedBaseline.ps1'
		# A child process (not in-process `&`) so the sidecar's terminal exit cannot end this wrapper.
		$repairJson = & "$PSHOME\pwsh.exe" -NoProfile -File $repairScript -RepositoryRoot $root -Worktree $reattachTarget -WorktreeCliExecutable $worktreeCli
		if ($LASTEXITCODE -ne 0) { throw "Automatic session re-parent check failed (exit $LASTEXITCODE): $($repairJson -join '; ')." }
		$repair = ($repairJson -join "`n" | ConvertFrom-Json -Depth 100)
		# Echo only actionable re-parent state; receipt identities remain internal to
		# the scheduler sidecars, which rediscover their deterministic local receipt.
		if ($repair.status -cne 'not-needed') { Write-Host ($repairJson -join "`n") }
		# All provenance is receipt-derived: the strictly validated in-worktree receipt is the
		# sole reattach authority now that no session ledger claim exists.
		$proof = Get-AgentWorktreeReattachProof -Client $Client -RepositoryRoot $root -Worktree $ReattachWorktree
		$receipt = $proof.Receipt.Value
		$owner = $receipt.sessionOwner
		$identity = [pscustomobject]@{
			Primary = $proof.Primary; Worktree = $proof.Worktree; Branch = $receipt.branch; TargetBranch = $receipt.targetBranch; Baseline = $receipt.baseline
		}
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
		$owner = [guid]::NewGuid().ToString()
		$identity = [pscustomobject]@{ Primary = $primary; Worktree = $worktree; Branch = $branch; TargetBranch = $primary.Branch; Baseline = $primary.Head }
		New-Item -ItemType Directory -Path $worktreeRoot -Force | Out-Null
		& git -C $root worktree add -b $branch $worktree $primary.Head
		if ($LASTEXITCODE -ne 0) { throw "Failed to create worktree '$worktree'." }
		$worktreeCreated = $true
		$receipt = New-AgentWorktreeSessionReceipt -Client $Client -PrimaryCheckout $root -GitCommonDirectory $primary.CommonDirectory -Worktree $worktree `
			-WorktreeId $uuid -Branch $branch -TargetBranch $primary.Branch -Baseline $primary.Head -SessionOwner $owner
		Write-AgentWorktreeSessionReceipt -Worktree $worktree -Receipt $receipt | Out-Null
	}

	# Both paths reach here with a validated identity and its durable session owner.
	Set-AgentWorktreeEnvironment $identity $owner
	& (Join-Path $root '.agents\scripts\Bootstrap-AgentTools.ps1') -RepositoryRoot $root -WaitSeconds $WaitSeconds
	& (Join-Path $root '.agents\scripts\Provision-WorktreeThirdParty.ps1') -RepositoryRoot $identity.Worktree -WaitSeconds $WaitSeconds
	Assert-AgentWorktreeSkillsLink $identity.Worktree
	if ($clearClaudeArgumentTransport) { $env:BROKEN_ENGINE_CLIENT_ARGUMENTS = $null }
	if ([string]::IsNullOrWhiteSpace($ClientExecutable)) {
		$ClientExecutable = (Get-Command $Client -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
	}
	# Skip the DataPacker build on a re-parent conflict: a tree with conflict markers cannot build, and a
	# wrapper exit here would block the very session that must resolve the conflict from launching. The
	# session runs the build itself after `git rebase --continue`. Provisioning above is conflict-insensitive.
	if ($null -eq $repair -or $repair.status -cne 'reparented-conflict') {
		& (Join-Path $root '.agents\scripts\Build-WorktreeDataPacker.ps1') -Worktree $identity.Worktree -WorktreeCliExecutable $worktreeCli -PrimaryCheckout $root
	}
	$banner = "$(if ($worktreeCreated) { 'Created' } else { 'Reattached' }) worktree $($identity.Worktree) on branch $($identity.Branch) at baseline $($identity.Baseline)."
	if ($null -ne $repair -and $repair.status -cin @('reparented', 'reparented-conflict')) {
		$banner += " (re-parented from squashed $($repair.oldBaseline) to $($repair.newBaseline))"
		if ($repair.status -ceq 'reparented-conflict') {
			$conflictList = @($repair.conflictFiles) -join ', '
			$buildCommand = "'.agents\scripts\Build-WorktreeDataPacker.ps1' -Worktree '$($identity.Worktree)' -WorktreeCliExecutable '$worktreeCli' -PrimaryCheckout '$root'"
			if ($repair.rebaseInProgress) {
				# Mid-rebase conflict: HEAD is detached, so a scheduler op would heal-delete the reparented claim.
				$banner = "FIRST TASK: the automatic re-parent hit rebase conflicts. Resolve the conflicted files ($conflictList), run 'git rebase --continue', and run NO scheduler operation (plan validate, claim-next, or any sidecar that invokes them) until the rebase completes - a mid-conflict detached HEAD heal-deletes the reparented claim. Then run $buildCommand to build the deferred DataPacker.`n" + $banner
			}
			else {
				# Autostash pop-conflict after a completed rebase: HEAD is attached, so scheduler ops are safe.
				$banner = "FIRST TASK: the automatic re-parent completed but its autostashed local changes conflicted on pop. Resolve the conflicted files ($conflictList), then run 'git stash drop' to discard the kept autostash. HEAD is already attached so scheduler operations are safe. Then run $buildCommand to build the deferred DataPacker.`n" + $banner
			}
		}
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
