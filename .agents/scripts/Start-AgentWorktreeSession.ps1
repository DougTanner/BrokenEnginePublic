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
$module = Join-Path $PSScriptRoot 'AgentCliSessionExclusion.psm1'
Import-Module $module -Force

function Invoke-Git([string[]] $Arguments) {
	$output = @(& git @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
}

$root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
$top = [IO.Path]::GetFullPath(@(Invoke-Git @('-C', $root, 'rev-parse', '--show-toplevel'))[0].Trim()).TrimEnd('\', '/')
if (-not $root.Equals($top, [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot is not repository root: '$root'." }
$git = Get-Item -LiteralPath (Join-Path $root '.git') -Force
if (-not $git.PSIsContainer -or ($git.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "RepositoryRoot is not primary checkout: '$root'." }
$status = @(Invoke-Git @('-C', $root, 'status', '--porcelain', '--untracked-files=all'))
if ($status.Count -ne 0) { throw "Primary checkout must be clean before session creation: $($status -join '; ')." }
$targetBranch = @(Invoke-Git @('-C', $root, 'branch', '--show-current'))[0].Trim()
if ([string]::IsNullOrWhiteSpace($targetBranch)) { throw 'Primary checkout must have an attached branch.' }
foreach ($marker in @('MERGE_HEAD','CHERRY_PICK_HEAD','REVERT_HEAD','BISECT_LOG','rebase-merge','rebase-apply','sequencer')) {
	$markerPath = @(Invoke-Git @('-C', $root, 'rev-parse', '--git-path', $marker))[0].Trim()
	if (Test-Path -LiteralPath $markerPath) { throw "Primary checkout has active Git operation marker '$marker'." }
}
$baseline = @(Invoke-Git @('-C', $root, 'rev-parse', 'HEAD'))[0].Trim()
$repositoryName = Split-Path -Leaf $root
$uuid = [guid]::NewGuid().ToString()
$branch = "$Client/$uuid"
$clientHome = if ($Client -eq 'claude') { '.claude' } else { '.codex' }
$worktreeRoot = Join-Path $HOME "$clientHome\worktrees\$repositoryName"
$worktree = Join-Path $worktreeRoot $uuid
if (Test-Path -LiteralPath $worktree) { throw "Generated worktree path already exists: '$worktree'." }
if (@(Invoke-Git @('-C', $root, 'branch', '--list', $branch)).Count -ne 0) { throw "Generated branch already exists: '$branch'." }

$owner = [guid]::NewGuid().ToString()
$claim = $null
$worktreeCreated = $false
$exitCode = 1
try {
	$agentCli = Join-Path $root 'Tools\AgentCli\Platforms\VisualStudio2026\Output\AgentCli.exe'
	$claim = Acquire-AgentCliSession -RepositoryRoot $root -Owner $owner -Label "$Client wrapper" -Worktree $worktree -WaitSeconds $WaitSeconds -LegacySessionsClosed:$LegacySessionsClosed -BootstrapExecutable $agentCli
	$env:BROKEN_ENGINE_AGENTCLI_SESSION_OWNER = $owner
	$env:BROKEN_ENGINE_AGENTCLI_SESSION_WORKTREE = $worktree
	$env:BROKEN_ENGINE_AGENTCLI_ADMISSION_MODE = $claim.Mode
	& (Join-Path $root '.agents\scripts\Bootstrap-AgentCli.ps1') -RepositoryRoot $root -WaitSeconds $WaitSeconds
	if ($LASTEXITCODE -ne 0) { throw "AgentCli bootstrap exited with code $LASTEXITCODE." }
	New-Item -ItemType Directory -Path $worktreeRoot -Force | Out-Null
	& git -C $root worktree add -b $branch $worktree $baseline
	if ($LASTEXITCODE -ne 0) { throw "Failed to create worktree '$worktree'." }
	$worktreeCreated = $true
	try {
		& (Join-Path $root '.agents\scripts\Provision-WorktreeThirdParty.ps1') -RepositoryRoot $worktree -WaitSeconds $WaitSeconds
		if ($LASTEXITCODE -ne 0) { throw "Provisioner exited with code $LASTEXITCODE." }
	}
	catch { throw "Provisioning failed; preserved worktree '$worktree' and branch '$branch' for recovery. $($_.Exception.Message)" }
	$reports = Join-Path $worktree 'Temp\AgentReports'
	New-Item -ItemType Directory -Path $reports -Force | Out-Null
	& git -C $worktree check-ignore -q -- 'Temp/AgentReports/'
	if ($LASTEXITCODE -ne 0) { throw "Report directory is not ignored: '$reports'." }

	$env:BROKEN_ENGINE_WORKTREE_PATH = $worktree
	$env:BROKEN_ENGINE_SESSION_BRANCH = $branch
	$env:BROKEN_ENGINE_PRIMARY_CHECKOUT = $root
	$env:BROKEN_ENGINE_TARGET_BRANCH = $targetBranch
	$env:BROKEN_ENGINE_BASELINE = $baseline
	if ([string]::IsNullOrWhiteSpace($ClientExecutable)) {
		$ClientExecutable = (Get-Command $Client -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
	}
	Write-Host "Created worktree $worktree on branch $branch at baseline $baseline."
	$exitCode = Invoke-AgentCliTrackedProcess -Executable $ClientExecutable -ArgumentList $ClientArguments -WorkingDirectory $worktree
}
catch {
	[Console]::Error.WriteLine($_.Exception.Message)
	if ($worktreeCreated) { [Console]::Error.WriteLine("Preserved partial worktree '$worktree' and branch '$branch' for recovery.") }
	$exitCode = 1
}
finally {
	if ($null -ne $claim) {
		try {
			$exclusion = Get-AgentCliExclusionStatus -RepositoryRoot $root -WaitSeconds $WaitSeconds
			if ($null -ne $exclusion.Maintenance -and $exclusion.Maintenance.owner -eq $owner) { Exit-AgentCliMaintenance -RepositoryRoot $root -Owner $owner }
			elseif (@($exclusion.Sessions | Where-Object { $_.owner -eq $owner }).Count -eq 1) { Release-AgentCliSession -RepositoryRoot $root -Owner $owner }
		}
		catch { [Console]::Error.WriteLine("Failed to release AgentCli session '$owner': $($_.Exception.Message)"); $exitCode = 1 }
	}
	Remove-Item Env:BROKEN_ENGINE_AGENTCLI_SESSION_OWNER -ErrorAction SilentlyContinue
	Remove-Item Env:BROKEN_ENGINE_AGENTCLI_SESSION_WORKTREE -ErrorAction SilentlyContinue
	Remove-Item Env:BROKEN_ENGINE_AGENTCLI_ADMISSION_MODE -ErrorAction SilentlyContinue
}
exit $exitCode
