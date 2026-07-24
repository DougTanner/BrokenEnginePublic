[CmdletBinding()]
param(
	[Parameter(Mandatory)][ValidateSet('claude', 'codex')][string] $Client,
	[Parameter(Mandatory)][string] $RepositoryRoot,
	[string] $ClientExecutable,
	[string[]] $ClientArguments = @(),
	[string] $ReattachWorktree,
	[switch] $LegacySessionsClosed,
	[int] $WaitSeconds = 660
)

$ErrorActionPreference = 'Stop'
if ($Client -cne 'claude' -and $Client -cne 'codex') { throw "Client must be lowercase 'claude' or 'codex'." }

# Claude carries client arguments out of band because -File treats a leading dash as a
# PowerShell parameter. Do not clear the transport variable until admission succeeds:
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
	'BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER',
	'BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE',
	'BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE',
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

function Set-AgentWorktreeEnvironment([object] $Identity, [string] $Owner, [string] $AdmissionMode) {
	$env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER = $Owner
	$env:BROKEN_ENGINE_WORKTREECLI_SESSION_WORKTREE = $Identity.Worktree
	$env:BROKEN_ENGINE_WORKTREECLI_ADMISSION_MODE = $AdmissionMode
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

$claim = $null
$worktreeCreated = $false
$exitCode = 1
try {
	$primary = Get-AgentWorktreePrimaryIdentity $RepositoryRoot
	$root = $primary.Root
	$status = @(Invoke-AgentGit @('-C', $root, 'status', '--porcelain', '--untracked-files=all'))
	if ([string]::IsNullOrWhiteSpace($ReattachWorktree) -and $status.Count -ne 0) { throw "Primary checkout must be clean before session creation: $($status -join '; ')." }
	$worktreeCli = Join-Path $root 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	$identity = $null

	if (-not [string]::IsNullOrWhiteSpace($ReattachWorktree)) {
		# All provenance is receipt-derived. This first proof is intentionally read-only;
		# the second proof closes the worktree/receipt race before ledger admission.
		$proof = Get-AgentWorktreeReattachProof -Client $Client -RepositoryRoot $root -Worktree $ReattachWorktree
		$receipt = $proof.Receipt.Value
		$firstReceiptBytes = $proof.Receipt.Bytes
		$firstReceiptIntegrityBytes = $proof.Receipt.IntegrityBytes
		$available = Test-WorktreeCliReattachAvailability -RepositoryRoot $root -Owner $receipt.sessionOwner -Worktree $proof.Worktree -WaitSeconds $WaitSeconds -LegacySessionsClosed:$LegacySessionsClosed
		if (-not $available.Available) { throw $available.Message }
		# The second proof executes inside the admission mutex under a receipt read lease, so
		# durable provenance cannot change between its validation and installation of the
		# restored claim.
		$claim = Restore-WorktreeCliSession -RepositoryRoot $root -Owner $receipt.sessionOwner -Label "$Client wrapper" -Worktree $proof.Worktree `
			-WaitSeconds $WaitSeconds -LegacySessionsClosed:$LegacySessionsClosed -BeforeAdmission {
				$lease = Open-AgentWorktreeReceiptReadLease $proof.Worktree
				try {
					$secondProof = Get-AgentWorktreeReattachProof -Client $Client -RepositoryRoot $root -Worktree $proof.Worktree -ExpectedReceiptBytes $firstReceiptBytes -ExpectedReceiptIntegrityBytes $firstReceiptIntegrityBytes -ReadLease $lease
					return [pscustomobject]@{ Proof = $secondProof; Lease = $lease }
				}
				catch { $lease.ReceiptStream.Dispose(); $lease.IntegrityStream.Dispose(); throw }
			}
		$proof = $claim.AdmissionProof
		$receipt = $proof.Receipt.Value
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
		$claim = Register-WorktreeCliSession -RepositoryRoot $root -Owner $owner -Label "$Client wrapper" -Worktree $worktree -WaitSeconds $WaitSeconds -LegacySessionsClosed:$LegacySessionsClosed
		$identity = [pscustomobject]@{ Primary = $primary; Worktree = $worktree; Branch = $branch; TargetBranch = $primary.Branch; Baseline = $primary.Head }
		New-Item -ItemType Directory -Path $worktreeRoot -Force | Out-Null
		& git -C $root worktree add -b $branch $worktree $primary.Head
		if ($LASTEXITCODE -ne 0) { throw "Failed to create worktree '$worktree'." }
		$worktreeCreated = $true
		$receipt = New-AgentWorktreeSessionReceipt -Client $Client -PrimaryCheckout $root -GitCommonDirectory $primary.CommonDirectory -Worktree $worktree `
			-WorktreeId $uuid -Branch $branch -TargetBranch $primary.Branch -Baseline $primary.Head -SessionOwner $owner
		Write-AgentWorktreeSessionReceipt -Worktree $worktree -Receipt $receipt | Out-Null
	}

	# Reattach only reaches this point after the second proof and atomic admission.
	Set-AgentWorktreeEnvironment $identity $claim.Owner $claim.Mode
	& (Join-Path $root '.agents\scripts\Bootstrap-AgentTools.ps1') -RepositoryRoot $root -WaitSeconds $WaitSeconds
	& (Join-Path $root '.agents\scripts\Provision-WorktreeThirdParty.ps1') -RepositoryRoot $identity.Worktree -WaitSeconds $WaitSeconds
	Assert-AgentWorktreeSkillsLink $identity.Worktree
	if ($clearClaudeArgumentTransport) { $env:BROKEN_ENGINE_CLIENT_ARGUMENTS = $null }
	if ([string]::IsNullOrWhiteSpace($ClientExecutable)) {
		$ClientExecutable = (Get-Command $Client -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
	}
	& (Join-Path $root '.agents\scripts\Build-WorktreeDataPacker.ps1') -Worktree $identity.Worktree -WorktreeCliExecutable $worktreeCli -PrimaryCheckout $root
	Write-Host "$(if ($worktreeCreated) { 'Created' } else { 'Reattached' }) worktree $($identity.Worktree) on branch $($identity.Branch) at baseline $($identity.Baseline)."
	$exitCode = Invoke-WorktreeCliTrackedProcess -Executable $ClientExecutable -ArgumentList $ClientArguments -WorkingDirectory $identity.Worktree
}
catch {
	[Console]::Error.WriteLine($_.Exception.Message)
	if ($worktreeCreated) { [Console]::Error.WriteLine("Preserved partial worktree '$($identity.Worktree)' and branch '$($identity.Branch)' for recovery.") }
	$exitCode = 1
}
finally {
	if ($null -ne $claim) {
		# Wrapper claims are always plain session claims (bootstrap serializes on its own mutex,
		# never a maintenance upgrade), so releasing is an unconditional Unregister.
		try { Unregister-WorktreeCliSession -RepositoryRoot $root -Owner $claim.Owner }
		catch { [Console]::Error.WriteLine("Failed to release WorktreeCli session '$($claim.Owner)': $($_.Exception.Message)"); $exitCode = 1 }
	}
	Restore-AgentWorktreeEnvironment
}
exit $exitCode
