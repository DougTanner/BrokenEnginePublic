[CmdletBinding()]
param(
	[Parameter(Mandatory)][ValidateSet('claude', 'codex')][string] $Client,
	[Parameter(Mandatory)][string] $RepositoryRoot,
	[string] $ClientExecutable,
	[string[]] $ClientArguments = @(),
	[switch] $LegacySessionsClosed,
	[int] $WaitSeconds = 660
)

$ErrorActionPreference = 'Stop'

# A -File caller cannot pass client arguments as parameter values: PowerShell binds a -prefixed token
# to any parameter it names, so '--verbose' silently binds -Verbose and never reaches the client, and
# '--Wait 5' silently rebinds $WaitSeconds. claude-worktree.sh therefore carries them out of band,
# NUL-delimited and base64-encoded, which no quoting or parameter name can collide with. Clear the
# variable so the launched client does not inherit it. Codex calls this script in-process and binds
# -ClientArguments by name, so this path is claude-only and never overrides that binding.
if ($Client -ceq 'claude' -and -not [string]::IsNullOrEmpty($env:BROKEN_ENGINE_CLIENT_ARGUMENTS))
{
	$decoded = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($env:BROKEN_ENGINE_CLIENT_ARGUMENTS))
	$env:BROKEN_ENGINE_CLIENT_ARGUMENTS = $null
	# printf terminates every argument with NUL, so the split always yields a trailing empty element.
	$parts = @($decoded -split "`0")
	if ($parts.Count -ge 2) { $ClientArguments = @($parts[0..($parts.Count - 2)]) }
}
Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1') -Force
$module = Join-Path $PSScriptRoot 'WorktreeCliSessionExclusion.psm1'
Import-Module $module -Force

$root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
$top = [IO.Path]::GetFullPath(@(Invoke-AgentGit @('-C', $root, 'rev-parse', '--show-toplevel'))[0].Trim()).TrimEnd('\', '/')
if (-not $root.Equals($top, [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot is not repository root: '$root'." }
$git = Get-Item -LiteralPath (Join-Path $root '.git') -Force
if (-not $git.PSIsContainer -or ($git.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "RepositoryRoot is not primary checkout: '$root'." }
$status = @(Invoke-AgentGit @('-C', $root, 'status', '--porcelain', '--untracked-files=all'))
if ($status.Count -ne 0) { throw "Primary checkout must be clean before session creation: $($status -join '; ')." }
$targetBranch = @(Invoke-AgentGit @('-C', $root, 'branch', '--show-current'))[0].Trim()
if ([string]::IsNullOrWhiteSpace($targetBranch)) { throw 'Primary checkout must have an attached branch.' }
foreach ($marker in @('MERGE_HEAD','CHERRY_PICK_HEAD','REVERT_HEAD','BISECT_LOG','rebase-merge','rebase-apply','sequencer')) {
	$markerPath = @(Invoke-AgentGit @('-C', $root, 'rev-parse', '--git-path', $marker))[0].Trim()
	if (Test-Path -LiteralPath $markerPath) { throw "Primary checkout has active Git operation marker '$marker'." }
}
$baseline = @(Invoke-AgentGit @('-C', $root, 'rev-parse', 'HEAD'))[0].Trim()
$repositoryName = Split-Path -Leaf $root
$uuid = [guid]::NewGuid().ToString()
$branch = "$Client/$uuid"
$clientHome = if ($Client -eq 'claude') { '.claude' } else { '.codex' }
$worktreeRoot = Join-Path $HOME "$clientHome\worktrees\$repositoryName"
$worktree = Join-Path $worktreeRoot $uuid
if (Test-Path -LiteralPath $worktree) { throw "Generated worktree path already exists: '$worktree'." }
if (@(Invoke-AgentGit @('-C', $root, 'branch', '--list', $branch)).Count -ne 0) { throw "Generated branch already exists: '$branch'." }

$owner = [guid]::NewGuid().ToString()
$claim = $null
$worktreeCreated = $false
$exitCode = 1
try {
	$worktreeCli = Join-Path $root 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	$claim = Register-WorktreeCliSession -RepositoryRoot $root -Owner $owner -Label "$Client wrapper" -Worktree $worktree -WaitSeconds $WaitSeconds -LegacySessionsClosed:$LegacySessionsClosed -BootstrapExecutable $worktreeCli
	$env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER = $owner
	$env:BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE = $worktree
	$env:BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE = $claim.Mode
	& (Join-Path $root '.agents\scripts\Bootstrap-AgentTools.ps1') -RepositoryRoot $root -WaitSeconds $WaitSeconds
	New-Item -ItemType Directory -Path $worktreeRoot -Force | Out-Null
	& git -C $root worktree add -b $branch $worktree $baseline
	if ($LASTEXITCODE -ne 0) { throw "Failed to create worktree '$worktree'." }
	$worktreeCreated = $true
	# Lock the live session worktree so no other session or cleanup can 'git worktree remove' it with a
	# single --force; released in finally when this wrapper (and its client) exits.
	& git -C $root worktree lock --reason "Live $Client session $uuid - do not remove" $worktree
	if ($LASTEXITCODE -ne 0) { throw "Failed to lock worktree '$worktree'." }
	try {
		& (Join-Path $root '.agents\scripts\Provision-WorktreeThirdParty.ps1') -RepositoryRoot $worktree -WaitSeconds $WaitSeconds
	}
	catch { throw "Provisioning failed; preserved worktree '$worktree' and branch '$branch' for recovery. $($_.Exception.Message)" }
	$skillsLink = Join-Path $worktree '.claude\skills'
	$skillsItem = Get-Item -LiteralPath $skillsLink -Force -ErrorAction SilentlyContinue
	if ($null -eq $skillsItem -or -not $skillsItem.PSIsContainer -or -not ($skillsItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
		$null -eq (Get-ChildItem -LiteralPath $skillsLink -ErrorAction SilentlyContinue | Select-Object -First 1)) {
		throw "'.claude\skills' in worktree '$worktree' did not check out as a working directory link, so agent skills are unavailable. Enable Windows Developer Mode (Settings -> System -> For developers -> Developer Mode -> On), open a new terminal, then restore the link: git -C '$worktree' config core.symlinks true; git -C '$worktree' checkout -- .claude/skills"
	}
	$env:BROKEN_ENGINE_WORKTREE_PATH = $worktree
	$env:BROKEN_ENGINE_SESSION_BRANCH = $branch
	$env:BROKEN_ENGINE_PRIMARY_CHECKOUT = $root
	$env:BROKEN_ENGINE_TARGET_BRANCH = $targetBranch
	$env:BROKEN_ENGINE_BASELINE = $baseline
	$env:BROKEN_ENGINE_AGENT_CLIENT = $Client
	if ([string]::IsNullOrWhiteSpace($ClientExecutable)) {
		$ClientExecutable = (Get-Command $Client -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
	}
	Write-Host "Created worktree $worktree on branch $branch at baseline $baseline."
	$exitCode = Invoke-WorktreeCliTrackedProcess -Executable $ClientExecutable -ArgumentList $ClientArguments -WorkingDirectory $worktree
}
catch {
	[Console]::Error.WriteLine($_.Exception.Message)
	if ($worktreeCreated) { [Console]::Error.WriteLine("Preserved partial worktree '$worktree' and branch '$branch' for recovery.") }
	$exitCode = 1
}
finally {
	if ($null -ne $claim) {
		try {
			$exclusion = Get-WorktreeCliExclusionStatus -RepositoryRoot $root -WaitSeconds $WaitSeconds
			if ($null -ne $exclusion.Maintenance -and $exclusion.Maintenance.owner -eq $owner) { Exit-WorktreeCliMaintenance -RepositoryRoot $root -Owner $owner }
			elseif (@($exclusion.Sessions | Where-Object { $_.owner -eq $owner }).Count -eq 1) { Unregister-WorktreeCliSession -RepositoryRoot $root -Owner $owner }
		}
		catch { [Console]::Error.WriteLine("Failed to release WorktreeCli session '$owner': $($_.Exception.Message)"); $exitCode = 1 }
	}
	if ($worktreeCreated) {
		# Session over: release the live-session lock so ordinary retained-worktree cleanup rules apply again.
		& git -C $root worktree unlock $worktree 2>$null
	}
	Remove-Item Env:BROKEN_ENGINE_* -ErrorAction SilentlyContinue
}
exit $exitCode
