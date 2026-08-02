# Landing-lock claim script for the pre-confirmation reconciliation
# lease. Post-confirmation landing imports the same common helper directly.
# The script intentionally does not turn live contention into an override
# request: callers can reinvoke a retryable result after its short delay.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $WorktreeCliExecutable,
	[Parameter(Mandatory)][string] $GitCommonDirectory,
	[Parameter(Mandatory)][string] $SessionLabel,
	[Parameter(Mandatory)][string] $Worktree,
	[string] $LandingOwner,
	[switch] $Release,
	[ValidateRange(60, 86400)][int] $LeaseSeconds = 3600,
	[ValidateRange(1, 3600)][int] $WaitSeconds = 300,
	[ValidateRange(50, 5000)][int] $PollMilliseconds = 500
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workflowModule = Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1'
if (-not (Test-Path -LiteralPath $workflowModule -PathType Leaf)) {
	$workflowModule = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts\FinalizeWorkflowCommon.psm1'
}
Import-Module $workflowModule -Force

$result = [ordered]@{
	schemaVersion = 'broken-engine-finalize-lock-claim/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Landing lock claim did not run.'
	owner = $LandingOwner
	lock = $null
	attempts = 0
	disposition = 'terminal'
	requiresUserAuthority = $false
	retryAfterMilliseconds = 0
	blocker = $null
}

function Complete-FinalizeLockClaim([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	Write-Output ($result | ConvertTo-Json -Depth 32 -Compress)
	exit $ExitCode
}

try {
	$item = Get-Item -LiteralPath $WorktreeCliExecutable -Force -ErrorAction Stop
	if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.Length -eq 0) {
		throw "WorktreeCli executable must be a nonempty ordinary file: '$WorktreeCliExecutable'."
	}
	if ($Release) {
		if ([string]::IsNullOrWhiteSpace($LandingOwner)) {
			$result.blocker = [ordered]@{ disposition = 'terminal'; requiresUserAuthority = $false; retryAfterMilliseconds = 0 }
			Complete-FinalizeLockClaim 1 'error' 'landing-lock.release-owner-required' 'Releasing a landing lease requires the owner token of the held lease.'
		}
		$result.attempts = 1
		$releaseResult = Invoke-FinalizeNativeText $item.FullName @('lock', 'release', '--repo', $GitCommonDirectory, '--owner', $LandingOwner) $Worktree
		if ($releaseResult.ExitCode -eq 0) {
			Complete-FinalizeLockClaim 0 'pass' 'ok' 'Landing lease released.'
		}
		$status = $null
		try { if (-not [string]::IsNullOrWhiteSpace($releaseResult.Stdout)) { $status = $releaseResult.Stdout | ConvertFrom-Json -Depth 32 -ErrorAction Stop } } catch { }
		$output = (($releaseResult.Stdout, $releaseResult.Stderr) -join ' ').Trim()
		if ($releaseResult.ExitCode -eq 2) {
			# WorktreeCli reports an absent lock as a state conflict; a second release must stay idempotent.
			if ($null -ne $status -and ($status.PSObject.Properties.Name -ccontains 'held') -and -not $status.held) {
				Complete-FinalizeLockClaim 0 'pass' 'ok' 'Landing lease was already released.'
			}
			$result.lock = $status
			$result.blocker = [ordered]@{ disposition = 'terminal'; requiresUserAuthority = $false; retryAfterMilliseconds = 0 }
			Complete-FinalizeLockClaim 1 'error' 'landing-lock.release-denied' "WorktreeCli did not release the landing lease for this owner: $output"
		}
		$result.blocker = [ordered]@{ disposition = 'terminal'; requiresUserAuthority = $false; retryAfterMilliseconds = 0 }
		Complete-FinalizeLockClaim 1 'error' 'landing-lock.release-failed' "WorktreeCli lock release failed with exit $($releaseResult.ExitCode): $output"
	}
	if ([string]::IsNullOrWhiteSpace($LandingOwner)) {
		$token = Invoke-FinalizeNativeText $item.FullName @('lock', 'token') $Worktree
		if ($token.ExitCode -ne 0 -or $token.Stdout.Trim() -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') {
			Complete-FinalizeLockClaim 1 'error' 'landing-lock.token-failed' 'WorktreeCli could not generate a canonical landing owner token.'
		}
		$LandingOwner = $token.Stdout.Trim()
		$result.owner = $LandingOwner
	}
	$outcome = Invoke-FinalizeLandingLockClaim -WorktreeCliExecutable $item.FullName -GitCommonDirectory $GitCommonDirectory -Owner $LandingOwner -Session $SessionLabel -Worktree $Worktree -LeaseSeconds $LeaseSeconds -WaitSeconds $WaitSeconds -PollMilliseconds $PollMilliseconds
	$result.owner = $outcome.Owner
	$result.lock = $outcome.Lock
	$result.attempts = $outcome.Attempts
	$result.disposition = $outcome.Disposition
	$result.requiresUserAuthority = $outcome.RequiresUserAuthority
	$result.retryAfterMilliseconds = $outcome.RetryAfterMilliseconds
	if ($outcome.Claimed) {
		Complete-FinalizeLockClaim 0 'pass' 'ok' $outcome.Message
	}
	$result.blocker = [ordered]@{
		disposition = $outcome.Disposition
		requiresUserAuthority = $outcome.RequiresUserAuthority
		retryAfterMilliseconds = $outcome.RetryAfterMilliseconds
	}
	if ($outcome.Disposition -ceq 'terminal') {
		Complete-FinalizeLockClaim 1 'error' $outcome.Code $outcome.Message
	}
	Complete-FinalizeLockClaim 2 'blocked' $outcome.Code $outcome.Message
}
catch {
	$result.disposition = 'terminal'
	$result.requiresUserAuthority = $false
	$result.retryAfterMilliseconds = 0
	$result.blocker = [ordered]@{ disposition = 'terminal'; requiresUserAuthority = $false; retryAfterMilliseconds = 0 }
	Complete-FinalizeLockClaim 1 'error' 'internal.error' $_.Exception.Message
}
