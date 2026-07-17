# Promotes a validated AgentTools candidate pair (WorktreeCli.exe + AgentHarness.exe)
# to canonical primary Output during an approved session landing. Requires the
# candidate receipt written by New-AgentToolsCandidate.ps1 with its SHA-256, and a
# landed commit already contained in the primary branch whose Tools/WorktreeCli,
# Tools/AgentHarness, and Tools/ToolCommon tree hashes equal the receipt's — that
# binding makes promotion impossible from an ordinary routine build or an unlanded
# session tree. Promotion runs inside the WorktreeCli exclusion ledger's
# exclusive-operation window (other registered sessions and held maintenance
# block; the invoking landing session passes itself as -CooperatingSessionOwner
# so its own live claim does not self-block).
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
	[string] $CandidateReceiptPath,
	[Parameter(Mandatory = $true)]
	[string] $CandidateReceiptSha256,
	[Parameter(Mandatory = $true)]
	[string] $LandedCommit,
	[string] $CooperatingSessionOwner,
	[int] $WaitSeconds = 660
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\AgentScriptCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking

$result = [ordered]@{
	schemaVersion = 'broken-engine-agenttools-promotion-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Promotion did not run.'
	promoted = $false
	rollback = 'not-required'
	receipt = $null
}

function Complete-Promotion([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 100 -Compress))
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

	if ($CandidateReceiptSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'CandidateReceiptSha256 must be 64 lowercase hexadecimal characters.' }
	if (-not (Test-Path -LiteralPath $CandidateReceiptPath -PathType Leaf)) { throw "Candidate receipt is missing: '$CandidateReceiptPath'." }
	$actualReceiptHash = (Get-FileHash -LiteralPath $CandidateReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
	if ($actualReceiptHash -cne $CandidateReceiptSha256) {
		Complete-Promotion 2 'blocked' 'promotion.receipt-identity' "Candidate receipt hash mismatch: expected $CandidateReceiptSha256, found $actualReceiptHash."
	}
	$receipt = Get-Content -LiteralPath $CandidateReceiptPath -Raw | ConvertFrom-Json -Depth 32
	if ($receipt.schemaVersion -cne 'broken-engine-agenttools-candidate/v1') {
		throw "Candidate receipt has unexpected schema '$($receipt.schemaVersion)'."
	}
	if ($receipt.dirtyToolPaths) {
		Complete-Promotion 2 'blocked' 'promotion.dirty-candidate' 'Candidate was built from a dirty AgentTools source tree; rebuild the candidate from the reconciled commit.'
	}

	$candidates = [ordered]@{
		WorktreeCli = $receipt.executables.WorktreeCli
		AgentHarness = $receipt.executables.AgentHarness
	}
	foreach ($name in @('WorktreeCli', 'AgentHarness')) {
		$candidate = $candidates[$name]
		$identity = Get-ExecutableIdentity $candidate.path
		if (-not $identity.present -or $identity.bytes -eq 0) {
			Complete-Promotion 2 'blocked' 'promotion.candidate-missing' "Candidate executable is missing or invalid: '$($candidate.path)'."
		}
		if ($identity.sha256 -cne $candidate.sha256.ToLowerInvariant()) {
			Complete-Promotion 2 'blocked' 'promotion.candidate-identity' "Candidate executable no longer matches its receipt hash: '$($candidate.path)'."
		}
	}

	if ($LandedCommit -notmatch '^[0-9a-fA-F]{40}$') { throw 'LandedCommit must be a full commit hash.' }
	& git -C $primaryRoot merge-base --is-ancestor $LandedCommit HEAD
	if ($LASTEXITCODE -ne 0) {
		Complete-Promotion 2 'blocked' 'promotion.not-landed' "Landed commit '$LandedCommit' is not contained in the primary branch; promotion is impossible from an unlanded tree."
	}
	$landedTrees = @(Invoke-AgentGit @('-C', $primaryRoot, 'rev-parse', "${LandedCommit}:Tools/WorktreeCli", "${LandedCommit}:Tools/AgentHarness", "${LandedCommit}:Tools/ToolCommon")) | ForEach-Object { $_.Trim() }
	if ($landedTrees.Count -ne 3) { throw 'Unable to resolve landed AgentTools tree hashes.' }
	if ($landedTrees[0] -cne $receipt.toolTreeHashes.worktreeCli -or
		$landedTrees[1] -cne $receipt.toolTreeHashes.agentHarness -or
		$landedTrees[2] -cne $receipt.toolTreeHashes.toolCommon) {
		Complete-Promotion 2 'blocked' 'promotion.source-mismatch' 'Candidate tool source trees do not match the landed commit; rebuild the candidate from the landed source.'
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
			Complete-Promotion 2 'blocked' 'promotion.partial-previous' 'Canonical AgentTools output is a partial pair; repair it before promotion.'
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
				if (-not $promotedIdentity.present -or $promotedIdentity.sha256 -cne $candidates[$name].sha256.ToLowerInvariant()) {
					throw "Promoted '$name' does not match the candidate hash."
				}
			}
			# The stamp is part of the promoted state (bootstrap drift detection reads it);
			# a stamp failure rolls the pair back rather than leaving stale drift evidence.
			[IO.File]::WriteAllText($stampPath, ($landedTrees -join "`n") + "`n")
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
				Complete-Promotion 1 'error' 'promotion.rollback-failed' "Promotion failed AND rollback could not restore the complete previous state (canonical pair or stamp may be partial; backup retained at '$backupDirectory'). Promotion failure: $failure. Rollback failure: $($_.Exception.Message)"
			}
		}

		$receiptValue = [ordered]@{
			schemaVersion = 'broken-engine-agenttools-promotion/v1'
			promotedAt = [DateTime]::UtcNow.ToString('O')
			primaryRoot = $primaryRoot
			landedCommit = $LandedCommit
			toolTreeHashes = $receipt.toolTreeHashes
			candidateReceipt = [ordered]@{ path = $CandidateReceiptPath; sha256 = $CandidateReceiptSha256 }
			previous = $previous
			candidate = [ordered]@{
				WorktreeCli = [ordered]@{ path = $candidates.WorktreeCli.path; sha256 = $candidates.WorktreeCli.sha256.ToLowerInvariant() }
				AgentHarness = [ordered]@{ path = $candidates.AgentHarness.path; sha256 = $candidates.AgentHarness.sha256.ToLowerInvariant() }
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
		if ($_.Exception.Message -match 'Timed out|not initialized|ledger mutex') {
			Complete-Promotion 2 'blocked' 'promotion.coordination-blocked' "Could not acquire exclusive AgentTools coordination: $($_.Exception.Message)"
		}
		throw
	}
}
catch {
	Complete-Promotion 1 'error' 'promotion.failed' $_.Exception.Message
}
