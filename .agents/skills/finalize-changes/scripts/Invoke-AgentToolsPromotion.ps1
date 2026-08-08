# Promotes a freshly built AgentTools candidate pair (WorktreeCli.exe +
# AgentHarness.exe) to canonical primary Output after a landing. The candidate
# executables are supplied by the caller that built them, and the landed commit
# must already be contained in the primary branch. Promotion runs
# inside the WorktreeCli exclusion ledger's
# exclusive-operation window (any other in-flight transient operation claim
# blocks; the invoking landing passes its own session owner as
# -CooperatingSessionOwner so a same-session in-flight operation claim does not
# self-block).
#
# Transaction: snapshot the complete previous canonical pair (both-present or
# both-absent; a partial pair blocks), replace both executables as one logical
# promotion, re-run the capability contract from the canonical paths, verify the
# promoted hashes, and write AgentToolsSourceStamp.txt from the landed commit. On
# any replacement or post-promotion failure, restore the complete previous pair,
# re-verify the rollback, and report it before releasing coordination. A
# schema-versioned broken-engine-agenttools-promotion/v1 receipt records the
# previous, candidate, and promoted identities.
#
# Result contract: one broken-engine-agenttools-promotion-result/v1 JSON object on
# stdout. Exit 0 = promoted, 2 = deterministic blocker (mismatch, blocked
# coordination, verified rollback), 1 = malformed input, internal failure, or a
# failed rollback leaving partial canonical state (named in the message).
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $PrimaryRoot,
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliCandidate,
	[Parameter(Mandatory = $true)]
	[string] $AgentHarnessCandidate,
	[Parameter(Mandatory = $true)]
	[string] $LandedCommit,
	[string] $CooperatingSessionOwner,
	[ValidateRange(1, 55)][int] $WaitSeconds = 55
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'AgentScriptCommon.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
Import-Module (Join-Path $sharedScripts 'AgentScriptCommon.psm1') -Force
Import-Module (Join-Path $sharedScripts 'WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking

$result = [ordered]@{
	schemaVersion = 'broken-engine-agenttools-promotion-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Promotion did not run.'
	promoted = $false
	rollback = 'not-required'
	receipt = $null
	disposition = 'terminal'
	requiresUserAuthority = $false
	retryAfterMilliseconds = 0
	blocker = $null
}

function Complete-Promotion([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message, [string] $Disposition = 'terminal', [bool] $RequiresUserAuthority = $false, [int] $RetryAfterMilliseconds = 0) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	$result.disposition = $Disposition
	$result.requiresUserAuthority = $RequiresUserAuthority
	$result.retryAfterMilliseconds = $RetryAfterMilliseconds
	if ($Status -cne 'pass') {
		$result.blocker = [ordered]@{
			disposition = $Disposition
			requiresUserAuthority = $RequiresUserAuthority
			retryAfterMilliseconds = $RetryAfterMilliseconds
		}
	}
	Write-Output ($result | ConvertTo-Json -Depth 100 -Compress)
	exit $ExitCode
}

function Get-ExecutableIdentity([string] $Path) {
	$item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
	if ($null -eq $item -or $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		return [ordered]@{ path = $Path; present = $false; sha256 = $null; bytes = $null }
	}
	return [ordered]@{
		path = $Path
		present = $true
		sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
		bytes = $item.Length
	}
}

try {
	if (-not [IO.Path]::IsPathRooted($PrimaryRoot)) { throw 'PrimaryRoot must be absolute.' }
	$primaryRoot = Get-AgentCanonicalPath $PrimaryRoot
	$topLevel = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $primaryRoot, 'rev-parse', '--show-toplevel'))[0].Trim())
	if (-not $topLevel.Equals($primaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
		throw "PrimaryRoot is not the repository root: '$primaryRoot'."
	}
	$gitDirectory = Get-Item -LiteralPath (Join-Path $primaryRoot '.git') -Force -ErrorAction Stop
	if (-not $gitDirectory.PSIsContainer -or ($gitDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		throw "PrimaryRoot must be the primary checkout with an ordinary .git directory: '$primaryRoot'."
	}

	if ($LandedCommit -cnotmatch '^[0-9a-f]{40}$') { throw 'LandedCommit must be a full lowercase commit hash.' }
	& git -C $primaryRoot merge-base --is-ancestor $LandedCommit HEAD
	if ($LASTEXITCODE -ne 0) {
		Complete-Promotion 2 'blocked' 'promotion.not-landed' "Landed commit '$LandedCommit' is not contained in the primary branch; promotion is impossible from an unlanded tree."
	}
	$candidates = [ordered]@{}
	foreach ($entry in @(@('WorktreeCli', $WorktreeCliCandidate), @('AgentHarness', $AgentHarnessCandidate))) {
		$candidateItem = Get-Item -LiteralPath $entry[1] -Force -ErrorAction SilentlyContinue
		if ($null -eq $candidateItem -or $candidateItem.PSIsContainer -or ($candidateItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $candidateItem.Length -eq 0) {
			throw "Candidate '$($entry[0])' must be a nonempty ordinary file: '$($entry[1])'."
		}
		$candidates[$entry[0]] = [ordered]@{ path = $candidateItem.FullName; sha256 = (Get-FileHash -LiteralPath $candidateItem.FullName -Algorithm SHA256).Hash.ToLowerInvariant(); bytes = $candidateItem.Length }
	}

	$worktreeCliOutput = Join-Path $primaryRoot 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	$agentHarnessOutput = Join-Path $primaryRoot 'Tools\AgentHarness\Platforms\VisualStudio2026\Output'
	foreach ($output in @($worktreeCliOutput, $agentHarnessOutput)) {
		$outputItem = Get-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
		if ($null -ne $outputItem -and (-not $outputItem.PSIsContainer -or ($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint))) {
			throw "Primary AgentTools Output must be an ordinary directory: '$output'."
		}
		if ($null -eq $outputItem) { New-Item -ItemType Directory -Force $output | Out-Null }
	}
	$canonical = [ordered]@{
		WorktreeCli = Join-Path $worktreeCliOutput 'WorktreeCli.exe'
		AgentHarness = Join-Path $agentHarnessOutput 'AgentHarness.exe'
	}
	$capabilityScript = Join-Path $primaryRoot '.agents\scripts\Test-AgentToolsCapabilities.ps1'
	if (-not (Test-Path -LiteralPath $capabilityScript -PathType Leaf)) { throw "AgentTools capability checker is missing: '$capabilityScript'." }

	$promotionAction = {
		$previous = [ordered]@{
			WorktreeCli = Get-ExecutableIdentity $canonical.WorktreeCli
			AgentHarness = Get-ExecutableIdentity $canonical.AgentHarness
		}
		if ($previous.WorktreeCli.present -ne $previous.AgentHarness.present) {
			Complete-Promotion 2 'blocked' 'promotion.partial-previous' 'Canonical AgentTools output is a partial pair; repair it before promotion.' 'authority-required' $true
		}
		$firstRollout = -not $previous.WorktreeCli.present

		$backupDirectory = Join-Path $env:LOCALAPPDATA "BrokenEngine\AgentToolsPromotions\backup-$([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))-$PID"
		if (-not $firstRollout) {
			New-Item -ItemType Directory -Force $backupDirectory | Out-Null
			Copy-Item -LiteralPath $canonical.WorktreeCli -Destination (Join-Path $backupDirectory 'WorktreeCli.exe') -Force
			Copy-Item -LiteralPath $canonical.AgentHarness -Destination (Join-Path $backupDirectory 'AgentHarness.exe') -Force
		}
		# The stamp belongs to the promoted state; snapshot it so rollback can restore it
		# even after a failed WriteAllText truncated the file.
		$stampPath = Join-Path $worktreeCliOutput 'AgentToolsSourceStamp.txt'
		$previousStamp = if (Test-Path -LiteralPath $stampPath -PathType Leaf) { [IO.File]::ReadAllBytes($stampPath) } else { $null }

		$failure = $null
		$replaced = @()
		try {
			foreach ($name in @('WorktreeCli', 'AgentHarness')) {
				$staging = "$($canonical[$name]).promoting"
				Copy-Item -LiteralPath $candidates[$name].path -Destination $staging -Force
				[IO.File]::Move($staging, $canonical[$name], $true)
				$replaced += $name
			}
			& $capabilityScript -WorktreeCliExecutable $canonical.WorktreeCli -AgentHarnessExecutable $canonical.AgentHarness | Out-Null
			foreach ($name in @('WorktreeCli', 'AgentHarness')) {
				$promotedIdentity = Get-ExecutableIdentity $canonical[$name]
				if (-not $promotedIdentity.present -or $promotedIdentity.sha256 -cne $candidates[$name].sha256) {
					throw "Promoted '$name' does not match the candidate hash."
				}
			}
			# The stamp is part of the promoted state (a built-source provenance record, no longer
			# read by bootstrap); a stamp failure rolls the pair back rather than leaving an
			# inconsistent provenance record.
			[IO.File]::WriteAllText($stampPath, $LandedCommit + "`n")
		}
		catch {
			$failure = $_.Exception.Message
		}

		if ($null -ne $failure) {
			try {
				foreach ($name in @('WorktreeCli', 'AgentHarness')) {
					$staging = "$($canonical[$name]).promoting"
					if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Force -Confirm:$false -ErrorAction SilentlyContinue }
				}
				if ($firstRollout) {
					foreach ($name in $replaced) {
						if (Test-Path -LiteralPath $canonical[$name]) { Remove-Item -LiteralPath $canonical[$name] -Force -Confirm:$false }
					}
				}
				else {
					foreach ($name in $replaced) {
						Copy-Item -LiteralPath (Join-Path $backupDirectory "$name.exe") -Destination $canonical[$name] -Force
					}
					foreach ($name in @('WorktreeCli', 'AgentHarness')) {
						$restoredIdentity = Get-ExecutableIdentity $canonical[$name]
						if (-not $restoredIdentity.present -or $restoredIdentity.sha256 -cne $previous[$name].sha256) {
							throw "Restored '$name' does not match the previous hash."
						}
					}
					& $capabilityScript -WorktreeCliExecutable $canonical.WorktreeCli -AgentHarnessExecutable $canonical.AgentHarness | Out-Null
				}
				if ($null -ne $previousStamp) {
					[IO.File]::WriteAllBytes($stampPath, $previousStamp)
					$restoredStamp = [IO.File]::ReadAllBytes($stampPath)
					if ([Convert]::ToBase64String($restoredStamp) -cne [Convert]::ToBase64String($previousStamp)) { throw 'Restored source stamp does not match the previous content.' }
				}
				elseif (Test-Path -LiteralPath $stampPath) {
					Remove-Item -LiteralPath $stampPath -Force -Confirm:$false
				}
				$result.rollback = 'verified'
				Complete-Promotion 2 'blocked' 'promotion.rolled-back' "Promotion failed and the previous canonical pair was restored and re-verified. Failure: $failure"
			}
			catch {
				$result.rollback = 'failed'
				Complete-Promotion 1 'error' 'promotion.rollback-failed' "Promotion failed AND rollback could not restore the complete previous state (canonical pair or stamp may be partial; backup retained at '$backupDirectory'). Promotion failure: $failure. Rollback failure: $($_.Exception.Message)" 'authority-required' $true
			}
		}

		$receiptValue = [ordered]@{
			schemaVersion = 'broken-engine-agenttools-promotion/v1'
			promotedAt = [DateTime]::UtcNow.ToString('O')
			primaryRoot = $primaryRoot
			landedCommit = $LandedCommit
			previous = $previous
			candidate = [ordered]@{
				WorktreeCli = [ordered]@{ path = $candidates.WorktreeCli.path; sha256 = $candidates.WorktreeCli.sha256 }
				AgentHarness = [ordered]@{ path = $candidates.AgentHarness.path; sha256 = $candidates.AgentHarness.sha256 }
			}
			promoted = [ordered]@{
				WorktreeCli = Get-ExecutableIdentity $canonical.WorktreeCli
				AgentHarness = Get-ExecutableIdentity $canonical.AgentHarness
			}
			backupDirectory = $(if ($firstRollout) { $null } else { $backupDirectory })
			capabilityCheck = 'pass'
		}
		$result.promoted = $true
		try {
			$receiptDirectory = Join-Path $env:LOCALAPPDATA 'BrokenEngine\AgentToolsPromotions'
			New-Item -ItemType Directory -Force $receiptDirectory | Out-Null
			$receiptPath = Join-Path $receiptDirectory "promotion-$([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))-$($LandedCommit.Substring(0, 8)).json"
			[IO.File]::WriteAllText($receiptPath, ($receiptValue | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
			$result.receipt = [ordered]@{
				path = $receiptPath
				sha256 = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
				bytes = (Get-Item -LiteralPath $receiptPath -Force).Length
			}
		}
		catch {
			# The pair is promoted, verified, and stamped; only the receipt is outstanding.
			Complete-Promotion 2 'blocked' 'promotion.receipt-failed' "The candidate pair was promoted, re-verified, and stamped, but the promotion receipt could not be written: $($_.Exception.Message)"
		}
		Complete-Promotion 0 'pass' 'ok' 'AgentTools candidate pair was promoted to canonical primary output and re-verified.'
	}

	try {
		Invoke-WorktreeCliExclusiveOperation -RepositoryRoot $primaryRoot -Label 'AgentTools promotion' -CooperatingSessionOwner $CooperatingSessionOwner -WaitSeconds $WaitSeconds -Action $promotionAction 6> $null
	}
	catch {
		$ledgerFailure = $_.Exception.Message -match '^WorktreeCli session ledger (is unreadable or malformed|failed validation|contains an invalid)'
		if ($_.Exception.Message -match 'Timed out') {
			Complete-Promotion 2 'blocked' 'promotion.shared-quiescence' "Canonical AgentTools remains in use by an in-flight operation claim owner: $($_.Exception.Message)" 'shared-quiescence' $false 500
		}
		if ($ledgerFailure -or $_.Exception.Message -match 'not initialized|ledger mutex') {
			Complete-Promotion 2 'blocked' 'promotion.coordination-unverifiable' "Canonical AgentTools coordination state cannot be verified: $($_.Exception.Message)" 'authority-required' $true
		}
		throw
	}
}
catch {
	Complete-Promotion 1 'error' 'promotion.failed' $_.Exception.Message
}
