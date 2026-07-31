# Scratch-repository coverage for candidate creation, approval preparation, the
# landing lock lease, one session landing, and idempotent post-advance recovery.
# Uses the supplied WorktreeCli only inside disposable Output and never touches a
# real primary checkout, queue, or canonical executable.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1') -Force -DisableNameChecking

$script:Failures = [Collections.Generic.List[string]]::new()
$landingScript = Join-Path $PSScriptRoot 'Invoke-FinalizeLanding.ps1'
$approvalPreparationScript = Join-Path $PSScriptRoot 'Invoke-FinalizeApprovalPreparation.ps1'
$candidateScript = Join-Path $PSScriptRoot 'Invoke-FinalizeCandidateCommit.ps1'
$lockClaimScript = Join-Path $PSScriptRoot 'Invoke-FinalizeLockClaim.ps1'
$approvalReviewScript = Join-Path $PSScriptRoot 'Show-FinalizeApprovalReview.ps1'
$moduleSource = Join-Path $PSScriptRoot '..\..\..\scripts'
$WorktreeCliExecutable = (Get-Item -LiteralPath $WorktreeCliExecutable -Force -ErrorAction Stop).FullName

function Assert-True([bool] $Condition, [string] $Name) {
	if ($Condition) { Write-Host "pass $Name" } else { $script:Failures.Add($Name); Write-Host "FAIL $Name" }
}

function Invoke-JsonScript([string] $Script, [string[]] $Arguments) {
	$stdout = @(& "$PSHOME\pwsh.exe" -NoProfile -File $Script @Arguments 2>$null)
	$exitCode = $LASTEXITCODE
	$text = ($stdout -join "`n").Trim()
	$json = $null
	try { if (-not [string]::IsNullOrWhiteSpace($text)) { $json = $text | ConvertFrom-Json -Depth 100 -ErrorAction Stop } } catch { }
	return [pscustomobject]@{ ExitCode = $exitCode; Json = $json; Text = $text }
}

function Invoke-JsonScriptWithSplat([string] $Script, [Collections.IDictionary] $Parameters, [string] $ScratchRoot) {
	$invocationRoot = Join-Path $ScratchRoot 'splat-invocation'
	New-Item -ItemType Directory -Force $invocationRoot | Out-Null
	$payloadPath = Join-Path $invocationRoot ([guid]::NewGuid().ToString('N') + '.json')
	$wrapperPath = Join-Path $invocationRoot 'Invoke-WithSplat.ps1'
	$wrapper = @'
param([string] $TargetScript, [string] $PayloadPath)
$payload = Get-Content -LiteralPath $PayloadPath -Raw | ConvertFrom-Json -Depth 32 -ErrorAction Stop
$parameters = @{}
foreach ($property in $payload.PSObject.Properties) { $parameters[$property.Name] = $property.Value }
& $TargetScript @parameters
exit $LASTEXITCODE
'@
	[IO.File]::WriteAllText($wrapperPath, $wrapper, [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText($payloadPath, (ConvertTo-Json -InputObject $Parameters -Depth 8 -Compress), [Text.UTF8Encoding]::new($false))
	$stderrPath = Join-Path $invocationRoot ([guid]::NewGuid().ToString('N') + '.stderr')
	$stdout = @(& "$PSHOME\pwsh.exe" -NoProfile -File $wrapperPath -TargetScript $Script -PayloadPath $payloadPath 2>$stderrPath)
	$exitCode = $LASTEXITCODE
	$stderr = ''
	if (Test-Path -LiteralPath $stderrPath) {
		$stderr = [IO.File]::ReadAllText($stderrPath)
		Remove-Item -LiteralPath $stderrPath -Force
	}
	$text = ($stdout -join "`n").Trim()
	$json = $null
	try { if (-not [string]::IsNullOrWhiteSpace($text)) { $json = $text | ConvertFrom-Json -Depth 100 -ErrorAction Stop } } catch { }
	return [pscustomobject]@{ ExitCode = $exitCode; Json = $json; Text = $text; Stderr = $stderr }
}

function Invoke-WorktreeCli([string[]] $Arguments, [int] $ExpectedExitCode = 0) {
	$stdout = @(& $WorktreeCliExecutable @Arguments 2>&1)
	if ($LASTEXITCODE -ne $ExpectedExitCode) {
		throw "WorktreeCli $($Arguments -join ' ') exited $LASTEXITCODE, expected $ExpectedExitCode`: $($stdout -join '; ')"
	}
	return $stdout
}

function Assert-Outcome($Run, [string] $Case, [int] $ExpectedExit, [string] $ExpectedStatus, [string] $ExpectedCode) {
	Assert-True ($null -ne $Run.Json) "$Case emitted JSON"
	if ($null -eq $Run.Json) {
		Write-Host "  stdout: $($Run.Text)"
		if (($Run.PSObject.Properties.Name -ccontains 'Stderr') -and -not [string]::IsNullOrWhiteSpace($Run.Stderr)) { Write-Host "  stderr: $($Run.Stderr.Trim())" }
		return
	}
	Assert-True ($Run.ExitCode -eq $ExpectedExit) "$Case exit=$ExpectedExit (was $($Run.ExitCode))"
	Assert-True ($Run.Json.status -ceq $ExpectedStatus) "$Case status=$ExpectedStatus (was $($Run.Json.status))"
	Assert-True ($Run.Json.code -ceq $ExpectedCode) "$Case code=$ExpectedCode (was $($Run.Json.code))"
	if ($Run.ExitCode -ne $ExpectedExit -or $Run.Json.code -cne $ExpectedCode) { Write-Host "  message: $($Run.Json.message)" }
}

function Assert-ExactProperties($Value, [string[]] $Expected, [string] $Case) {
	$actual = @($Value.PSObject.Properties.Name | Sort-Object)
	$wanted = @($Expected | Sort-Object)
	Assert-True (($actual -join '|') -ceq ($wanted -join '|')) "$Case exact properties"
}

function Set-ExpiredLandingLease([string] $LocalAppData, [string] $Owner) {
	$lease = @(Get-ChildItem -LiteralPath $LocalAppData -Recurse -Filter '*.lock' -File -Force | Where-Object {
		try {
			$metadata = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
			return $metadata.domain -ceq 'landing' -and $metadata.owner -ceq $Owner
		}
		catch { return $false }
	})
	if ($lease.Count -ne 1) { throw "Could not locate one scratch landing lease for '$Owner'." }
	$metadata = Get-Content -LiteralPath $lease[0].FullName -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
	[IO.File]::SetAttributes($lease[0].FullName, [IO.FileAttributes]::Normal)
	$now = [DateTime]::UtcNow
	$heartbeat = $now.AddSeconds(-120).ToString('yyyy-MM-ddTHH:mm:ss.fffZ', [Globalization.CultureInfo]::InvariantCulture)
	$expires = $now.AddSeconds(-60).ToString('yyyy-MM-ddTHH:mm:ss.fffZ', [Globalization.CultureInfo]::InvariantCulture)
	$metadata.claimedAt = $heartbeat
	$metadata.heartbeatAt = $heartbeat
	$metadata.expiresAt = $expires
	[IO.File]::WriteAllText($lease[0].FullName, ($metadata | ConvertTo-Json -Depth 16 -Compress), [Text.UTF8Encoding]::new($false))
}

function Set-UnverifiableLandingLease([string] $LocalAppData, [string] $Owner) {
	$lease = @(Get-ChildItem -LiteralPath $LocalAppData -Recurse -Filter '*.lock' -File -Force | Where-Object {
		try {
			$metadata = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
			return $metadata.domain -ceq 'landing' -and $metadata.owner -ceq $Owner
		}
		catch { return $false }
	})
	if ($lease.Count -ne 1) { throw "Could not locate one scratch landing lease for '$Owner'." }
	$metadata = Get-Content -LiteralPath $lease[0].FullName -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
	[IO.File]::SetAttributes($lease[0].FullName, [IO.FileAttributes]::Normal)
	$metadata.schemaVersion = 0
	[IO.File]::WriteAllText($lease[0].FullName, ($metadata | ConvertTo-Json -Depth 16 -Compress), [Text.UTF8Encoding]::new($false))
}

$scratchParent = Join-Path ([IO.Path]::GetTempPath()) 'BrokenEngineFinalizeWorkflowFixtures'
$scratchLeaf = [guid]::NewGuid().ToString('N')
$scratchBase = Assert-SafeScratchRoot $scratchParent (Join-Path $scratchParent $scratchLeaf) $scratchLeaf
$primary = Join-Path $scratchBase 'primary'
$session = Join-Path $scratchBase 'session'
$localAppData = Join-Path $scratchBase 'local-app-data'
$previousEnvironment = @{}
$fixtureEnvironment = $null
$fixtureExitCode = 0

try {
New-Item -ItemType Directory -Force $primary | Out-Null
New-Item -ItemType Directory -Force $localAppData | Out-Null
Invoke-ScratchGit $primary @('init', '-b', 'main') | Out-Null
Invoke-ScratchGit $primary @('config', 'core.autocrlf', 'false') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $primary '.agents\scripts') | Out-Null
foreach ($module in @('AgentScriptCommon.psm1', 'WorktreeCliSessionExclusion.psm1', 'AgentWorktreeSession.psm1')) {
	Copy-Item -LiteralPath (Join-Path $moduleSource $module) -Destination (Join-Path $primary ".agents\scripts\$module") -Force
}
[IO.File]::WriteAllText((Join-Path $primary '.gitignore'), "Temp/`nTools/WorktreeCli/Platforms/VisualStudio2026/Output/`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'base.txt'), 'base', [Text.UTF8Encoding]::new($false))
$gitlinkSource = Join-Path $scratchBase 'gitlink-source'
New-Item -ItemType Directory -Force $gitlinkSource | Out-Null
Invoke-ScratchGit $gitlinkSource @('init', '-b', 'main') | Out-Null
[IO.File]::WriteAllText((Join-Path $gitlinkSource 'submodule.txt'), 'gitlink fixture', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $gitlinkSource @('add', 'submodule.txt') | Out-Null
Invoke-ScratchGit $gitlinkSource @('commit', '-m', 'gitlink fixture') | Out-Null
Invoke-ScratchGit $primary @('-c', 'protocol.file.allow=always', 'submodule', 'add', $gitlinkSource, 'fixture-gitlink') | Out-Null
Invoke-ScratchGit $primary @('add', '-A') | Out-Null
Invoke-ScratchGit $primary @('commit', '-m', 'fixture base') | Out-Null
$baseline = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()

$primaryOutput = Join-Path $primary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
New-Item -ItemType Directory -Force $primaryOutput | Out-Null
Copy-Item -LiteralPath $WorktreeCliExecutable -Destination (Join-Path $primaryOutput 'WorktreeCli.exe') -Force
$uuid = [guid]::NewGuid().ToString()
$sessionBranch = "codex/$uuid"
Invoke-ScratchGit $primary @('worktree', 'add', '-b', $sessionBranch, $session, $baseline) | Out-Null
New-Item -ItemType Directory -Force (Join-Path $session 'Temp') | Out-Null
$sessionOutputParent = Join-Path $session 'Tools\WorktreeCli\Platforms\VisualStudio2026'
New-Item -ItemType Directory -Force $sessionOutputParent | Out-Null
New-Item -ItemType Junction -Path (Join-Path $sessionOutputParent 'Output') -Target $primaryOutput | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'change.txt'), 'session change', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add', 'change.txt') | Out-Null
Invoke-ScratchGit $session @('commit', '-m', 'fixture change') | Out-Null
# Align primary onto the session tip so every landing scenario below starts from an
# equal session/primary tree.
$sessionTip = (@(Invoke-ScratchGit $session @('rev-parse', 'HEAD')))[0].Trim()
Invoke-ScratchGit $primary @('merge', '--ff-only', $sessionTip) | Out-Null
$baseline = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()
$candidateMessage = Join-Path $scratchBase 'candidate-message.txt'
[IO.File]::WriteAllText($candidateMessage, "fixture candidate`n", [Text.UTF8Encoding]::new($false))

# Candidate construction is deliberately before verification. This isolated coverage
# exercises the Git boundary and its guarded rollbacks.
[IO.File]::WriteAllText((Join-Path $session 'candidate-session.txt'), 'session candidate', [Text.UTF8Encoding]::new($false))
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$sessionTip,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','*.txt','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'candidate-rejects-pathspec-owned-path' 1 'error' 'input.path-invalid'
New-Item -ItemType Directory -Force (Join-Path $session 'owned-directory') | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'owned-directory\first.txt'), 'first', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $session 'owned-directory\second.txt'), 'second', [Text.UTF8Encoding]::new($false))
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$sessionTip,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','owned-directory','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'candidate-rejects-directory-owned-path' 1 'error' 'input.path-directory'
Remove-Item -LiteralPath (Join-Path $session 'owned-directory') -Recurse -Force
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$sessionTip,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','fixture-gitlink','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'candidate-allows-tracked-gitlink-owned-path' 0 'pass' 'candidate.created'
Invoke-ScratchGit $session @('reset','--hard',$sessionTip) | Out-Null
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch','wrong-branch','-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$sessionTip,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','candidate-session.txt','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'candidate-rejects-wrong-checked-out-branch' 2 'blocked' 'identity.branch-mismatch'
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$sessionTip,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','candidate-session.txt','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'session-candidate-before-verification' 0 'pass' 'candidate.created'
if ($null -ne $run.Json) {
	Assert-True $run.Json.candidate.singleParent 'session candidate has one parent'
	Assert-True ($run.Json.candidate.parent -ceq $baseline) 'session candidate parent is reconciled primary'
	Assert-True ($run.Json.candidate.tree -ceq ((@(Invoke-ScratchGit $session @('rev-parse', "$($run.Json.candidate.commit)^{tree}")))[0].Trim())) 'session candidate tree identity is exact'
	Assert-True (@($run.Json.ownedPaths) -ccontains 'candidate-session.txt' -and @($run.Json.ownedPaths).Count -eq 1) 'session candidate owns exactly the caller-declared paths'
}
Invoke-ScratchGit $session @('reset','--hard',$baseline) | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'staged-unrelated.txt'), 'staged unrelated', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add','staged-unrelated.txt') | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'base.txt'), 'unstaged unrelated', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $session 'untracked-unrelated.txt'), 'untracked unrelated', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $session 'owned-state.txt'), 'owned candidate', [Text.UTF8Encoding]::new($false))
$stateBefore = ((@(Invoke-ScratchGit $session @('status','--porcelain=v1','-z','--untracked-files=all')) -join "`n"))
$disjointBefore = (($stateBefore -replace [string][char]0,"`n") -split "`n" | Where-Object { $_ -match 'unrelated' } | Sort-Object) -join "`n"
$indexBefore = ((@(Invoke-ScratchGit $session @('ls-files','-s')) -join "`n"))
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$baseline,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','owned-state.txt','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'session-candidate-preserves-disjoint-state' 0 'pass' 'candidate.created'
Assert-True ($indexBefore -cne ((@(Invoke-ScratchGit $session @('ls-files','-s')) -join "`n")) -and [string]::IsNullOrWhiteSpace((@(Invoke-ScratchGit $session @('status','--porcelain','--','owned-state.txt')) -join "`n"))) 'session candidate reconciles only owned real-index state cleanly'
$stateAfter = ((@(Invoke-ScratchGit $session @('status','--porcelain=v1','-z','--untracked-files=all')) -join "`n"))
$disjointAfter = (($stateAfter -replace [string][char]0,"`n") -split "`n" | Where-Object { $_ -match 'unrelated' } | Sort-Object) -join "`n"
Assert-True ($disjointBefore -ceq $disjointAfter) 'session candidate preserves disjoint staged unstaged and untracked status'
Invoke-ScratchGit $session @('reset','--hard',$baseline) | Out-Null
Remove-Item -LiteralPath (Join-Path $session 'staged-unrelated.txt'),(Join-Path $session 'untracked-unrelated.txt'),(Join-Path $session 'owned-state.txt') -Force -ErrorAction SilentlyContinue
[IO.File]::WriteAllText((Join-Path $session 'rollback-unrelated.txt'), 'staged unrelated', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add','rollback-unrelated.txt') | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'rollback-active-owned.txt'), 'owned candidate', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $session 'rollback-staged-owned.txt'), 'owned staged', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add','rollback-staged-owned.txt') | Out-Null
$rollbackSessionHead = (@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()
$rollbackSessionIndex = (@(Invoke-ScratchGit $session @('ls-files','-s')) -join "`n")
$rollbackSessionStatus = (@(Invoke-ScratchGit $session @('status','--porcelain=v1','-z','--untracked-files=all')) -join "`n")
$rollbackStagedIndex = (@(Invoke-ScratchGit $session @('ls-files','--stage','--','rollback-staged-owned.txt')) -join "`n")
$rollbackActiveWorktree = [IO.File]::ReadAllText((Join-Path $session 'rollback-active-owned.txt'), [Text.UTF8Encoding]::new($false,$true))
$rollbackStagedWorktree = [IO.File]::ReadAllText((Join-Path $session 'rollback-staged-owned.txt'), [Text.UTF8Encoding]::new($false,$true))
$rollbackUnrelatedWorktree = [IO.File]::ReadAllText((Join-Path $session 'rollback-unrelated.txt'), [Text.UTF8Encoding]::new($false,$true))
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$baseline,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','rollback-active-owned.txt','rollback-staged-owned.txt','-CommitMessageFile',$candidateMessage,'-FixtureFailure','post-index-mutation')
Assert-Outcome $run 'session-candidate-post-index-rollback' 2 'blocked' 'candidate.postcondition-failed'
Assert-True ($rollbackSessionHead -ceq ((@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim())) 'session post-index rollback restores the guarded ref'
Assert-True ($rollbackSessionIndex -ceq (@(Invoke-ScratchGit $session @('ls-files','-s')) -join "`n")) 'session post-index rollback restores owned and unrelated index entries'
Assert-True ($rollbackSessionStatus -ceq (@(Invoke-ScratchGit $session @('status','--porcelain=v1','-z','--untracked-files=all')) -join "`n")) 'session post-index rollback preserves owned and unrelated worktree status'
$rollbackStagedIndexAfter = (@(Invoke-ScratchGit $session @('ls-files','--stage','--','rollback-staged-owned.txt')) -join "`n")
Assert-True ($rollbackStagedIndex -ceq $rollbackStagedIndexAfter) 'session post-index rollback restores staged owned mode object and stage exactly'
Assert-True ($rollbackActiveWorktree -ceq [IO.File]::ReadAllText((Join-Path $session 'rollback-active-owned.txt'), [Text.UTF8Encoding]::new($false,$true)) -and $rollbackStagedWorktree -ceq [IO.File]::ReadAllText((Join-Path $session 'rollback-staged-owned.txt'), [Text.UTF8Encoding]::new($false,$true))) 'session post-index rollback preserves owned worktree bytes'
Assert-True ($rollbackUnrelatedWorktree -ceq [IO.File]::ReadAllText((Join-Path $session 'rollback-unrelated.txt'), [Text.UTF8Encoding]::new($false,$true))) 'session post-index rollback preserves unrelated worktree bytes'
Invoke-ScratchGit $session @('reset','--hard',$baseline) | Out-Null
Remove-Item -LiteralPath (Join-Path $session 'rollback-unrelated.txt'),(Join-Path $session 'rollback-active-owned.txt'),(Join-Path $session 'rollback-staged-owned.txt') -Force -ErrorAction SilentlyContinue
[IO.File]::WriteAllText((Join-Path $session 'mixed-owned.txt'), 'staged', [Text.UTF8Encoding]::new($false)); Invoke-ScratchGit $session @('add','mixed-owned.txt') | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'mixed-owned.txt'), 'unstaged after staged', [Text.UTF8Encoding]::new($false))
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$baseline,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','mixed-owned.txt','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'session-candidate-blocks-mixed-owned-state' 2 'blocked' 'git.owned-path-mixed-state'
Invoke-ScratchGit $session @('reset','--hard',$baseline) | Out-Null
Remove-Item -LiteralPath (Join-Path $session 'mixed-owned.txt') -Force -ErrorAction SilentlyContinue
[IO.File]::WriteAllText((Join-Path $primary 'primary-active-owned.txt'), 'primary candidate', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'primary-staged-owned.txt'), 'primary staged', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $primary @('add','primary-staged-owned.txt') | Out-Null
[IO.File]::WriteAllText((Join-Path $primary 'primary-disjoint-staged.txt'), 'staged', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $primary @('add','primary-disjoint-staged.txt') | Out-Null
[IO.File]::WriteAllText((Join-Path $primary 'base.txt'), 'disjoint unstaged', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'primary-disjoint-untracked.txt'), 'untracked', [Text.UTF8Encoding]::new($false))
$primaryDisjointBefore = (@(Invoke-ScratchGit $primary @('status','--porcelain=v1','--untracked-files=all')) -join "`n")
$beforePrimaryIndex = (@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n")
$primaryStagedOwnedIndex = (@(Invoke-ScratchGit $primary @('ls-files','--stage','--','primary-staged-owned.txt')) -join "`n")
$primaryActiveOwnedWorktree = [IO.File]::ReadAllText((Join-Path $primary 'primary-active-owned.txt'), [Text.UTF8Encoding]::new($false,$true))
$primaryStagedOwnedWorktree = [IO.File]::ReadAllText((Join-Path $primary 'primary-staged-owned.txt'), [Text.UTF8Encoding]::new($false,$true))
$primaryUnrelatedWorktree = [IO.File]::ReadAllText((Join-Path $primary 'primary-disjoint-untracked.txt'), [Text.UTF8Encoding]::new($false,$true))
# `pwsh -File` binds only the first token of a multi-value parameter, so this two-owned-path route is invoked
# through the splat wrapper, which passes the array intact.
function New-PrimaryCandidateParameters([Collections.IDictionary] $Extra = @{}) {
	$parameters = [ordered]@{ Route='primary-commit'; CurrentWorktree=$primary; PrimaryWorktree=$primary; CurrentBranch='main'; PrimaryBranch='main'; Baseline=$baseline; ExpectedCurrentTip=$baseline; ExpectedPrimaryTip=$baseline; OwnedPaths=@('primary-active-owned.txt','primary-staged-owned.txt'); CommitMessageFile=$candidateMessage }
	foreach ($entry in $Extra.GetEnumerator()) { $parameters[$entry.Key] = $entry.Value }
	return $parameters
}
$run = Invoke-JsonScriptWithSplat $candidateScript (New-PrimaryCandidateParameters) $scratchBase
Assert-Outcome $run 'primary-candidate-temporary-index' 0 'pass' 'candidate.created'
if ($null -ne $run.Json) {
	$verifiedCandidate = $run.Json.candidate.commit; $verifiedTree = $run.Json.candidate.tree
	Assert-True ($beforePrimaryIndex -ceq ((@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n"))) 'temporary index preserves real index'
	Assert-True ($primaryDisjointBefore -ceq (@(Invoke-ScratchGit $primary @('status','--porcelain=v1','--untracked-files=all')) -join "`n")) 'primary candidate preserves disjoint staged unstaged and untracked state'
	$rollback = Invoke-JsonScriptWithSplat $candidateScript (New-PrimaryCandidateParameters @{ VerifiedCandidateCommit=$verifiedCandidate; VerifiedCandidateTree=$verifiedTree; AdvancePrimary=$true; FixtureFailure='postcondition' }) $scratchBase
	Assert-Outcome $rollback 'primary-candidate-postcondition-rollback' 2 'blocked' 'candidate.postcondition-failed'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'guarded rollback restores expected old primary only from candidate'
	Assert-True ($beforePrimaryIndex -ceq ((@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n"))) 'guarded rollback preserves real index'
	$indexRollback = Invoke-JsonScriptWithSplat $candidateScript (New-PrimaryCandidateParameters @{ VerifiedCandidateCommit=$verifiedCandidate; VerifiedCandidateTree=$verifiedTree; AdvancePrimary=$true; FixtureFailure='post-index-mutation' }) $scratchBase
	Assert-Outcome $indexRollback 'primary-candidate-post-index-rollback' 2 'blocked' 'candidate.postcondition-failed'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'primary post-index rollback restores expected old ref'
	Assert-True ($beforePrimaryIndex -ceq ((@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n"))) 'primary post-index rollback restores owned and unrelated index entries'
	Assert-True ($primaryDisjointBefore -ceq (@(Invoke-ScratchGit $primary @('status','--porcelain=v1','--untracked-files=all')) -join "`n")) 'primary post-index rollback restores owned and unrelated status'
	Assert-True ($primaryStagedOwnedIndex -ceq (@(Invoke-ScratchGit $primary @('ls-files','--stage','--','primary-staged-owned.txt')) -join "`n")) 'primary post-index rollback restores staged owned mode object and stage exactly'
	Assert-True ($primaryActiveOwnedWorktree -ceq [IO.File]::ReadAllText((Join-Path $primary 'primary-active-owned.txt'), [Text.UTF8Encoding]::new($false,$true)) -and $primaryStagedOwnedWorktree -ceq [IO.File]::ReadAllText((Join-Path $primary 'primary-staged-owned.txt'), [Text.UTF8Encoding]::new($false,$true))) 'primary post-index rollback preserves owned worktree bytes'
	Assert-True ($primaryUnrelatedWorktree -ceq [IO.File]::ReadAllText((Join-Path $primary 'primary-disjoint-untracked.txt'), [Text.UTF8Encoding]::new($false,$true))) 'primary post-index rollback preserves unrelated worktree bytes'
	$advance = Invoke-JsonScriptWithSplat $candidateScript (New-PrimaryCandidateParameters @{ VerifiedCandidateCommit=$verifiedCandidate; VerifiedCandidateTree=$verifiedTree; AdvancePrimary=$true }) $scratchBase
	Assert-Outcome $advance 'primary-candidate-atomic-advance' 0 'pass' 'candidate.advanced'
	Assert-True ($verifiedCandidate -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'primary branch equals reviewed candidate'
	Assert-True ($verifiedTree -ceq ((@(Invoke-ScratchGit $primary @('rev-parse',"$verifiedCandidate^{tree}")))[0].Trim())) 'primary tree equals reviewed candidate tree'
}
Invoke-ScratchGit $primary @('reset','--hard',$baseline) | Out-Null
Remove-Item -LiteralPath (Join-Path $primary 'primary-active-owned.txt'),(Join-Path $primary 'primary-staged-owned.txt'),(Join-Path $primary 'primary-disjoint-staged.txt'),(Join-Path $primary 'primary-disjoint-untracked.txt') -Force -ErrorAction SilentlyContinue

$fixtureEnvironment = [ordered]@{
	LOCALAPPDATA = $localAppData
	BROKEN_ENGINE_FINALIZE_WORKFLOW_FIXTURE = '1'
	BROKEN_ENGINE_FINALIZE_APPROVAL_PREPARATION_FIXTURE = '1'
}
foreach ($entry in $fixtureEnvironment.GetEnumerator()) {
	$previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key)
	[Environment]::SetEnvironmentVariable($entry.Key, $entry.Value)
}

# Preview is deterministic even when executable discovery differs. Explicit launch
# uses a bounded command stub, so the actual Start-Process path is exercised safely.
$canonicalManualCommand = "& 'C:\Program Files\SmartGit\bin\smartgit.exe' '--log' '$primary' '--anchor-commit=$baseline'"
$run = Invoke-JsonScript $approvalReviewScript @('-PrimaryWorktree',$primary,'-ApprovedTip',$baseline)
Assert-Outcome $run 'approval-review-preview' 0 'preview' 'review.preview'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.manualCommand -ceq $canonicalManualCommand) 'approval-review-preview returns exact canonical manual command'
	Assert-True ($null -eq $run.Json.processId) 'approval-review-preview starts no process'
}
$reviewExecutable = Join-Path $primary 'fixture-smartgit.cmd'
$reviewArguments = Join-Path $primary 'fixture-smartgit-arguments.txt'
[IO.File]::WriteAllText($reviewExecutable, "@echo off`r`necho %* > `"$reviewArguments`"`r`n", [Text.UTF8Encoding]::new($false))
$run = Invoke-JsonScript $approvalReviewScript @('-PrimaryWorktree',$primary,'-ApprovedTip',$baseline,'-LaunchSmartGit','-FixtureSmartGitExecutable',$reviewExecutable)
Assert-Outcome $run 'approval-review-explicit-launch' 0 'opened' 'ok'
if ($null -ne $run.Json) {
	Assert-True ($null -ne $run.Json.processId) 'approval-review fixture launch reaches Start-Process'
	$deadline = [DateTime]::UtcNow.AddSeconds(5)
	while (-not (Test-Path -LiteralPath $reviewArguments) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 25 }
	Assert-True (Test-Path -LiteralPath $reviewArguments) 'approval-review fixture launch completes boundedly'
	if (Test-Path -LiteralPath $reviewArguments) {
		$capturedReviewArguments = [IO.File]::ReadAllText($reviewArguments).Trim()
		$expectedReviewArguments = ('"--log" "' + $primary + '" "--anchor-commit=' + $baseline + '"')
		Assert-True ($capturedReviewArguments -ceq $expectedReviewArguments) 'approval-review fixture receives exact forwarded arguments'
	}
}
Remove-Item -LiteralPath $reviewExecutable -Force
Remove-Item -LiteralPath $reviewArguments -Force -ErrorAction SilentlyContinue

$commonDirectory = ((@(Invoke-ScratchGit $primary @('rev-parse', '--path-format=absolute', '--git-common-dir')))[0].Trim())

# Reconciliation uses the claim script; landing calls the same common helper.
$reconcileOwner = [guid]::NewGuid().ToString()
$lockClaimArguments = @('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $reconcileOwner, '-LeaseSeconds', '60')
$run = Invoke-JsonScript $lockClaimScript $lockClaimArguments
Assert-Outcome $run 'reconcile-lock-claim' 0 'pass' 'ok'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.owner -ceq $reconcileOwner) 'reconcile lock preserves supplied owner'
	Assert-True ($null -eq $run.Json.blocker) 'reconcile lock success has no blocker disposition'
}
$lockReleaseArguments = @('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session)
$run = Invoke-JsonScript $lockClaimScript ($lockReleaseArguments + @('-LandingOwner', $reconcileOwner, '-Release'))
Assert-Outcome $run 'reconcile-lock-release' 0 'pass' 'ok'
if ($null -ne $run.Json) {
	Assert-ExactProperties $run.Json @('schemaVersion','status','code','message','owner','lock','attempts','disposition','requiresUserAuthority','retryAfterMilliseconds','blocker') 'reconcile-lock-release'
	Assert-True ($null -eq $run.Json.blocker) 'reconcile lock release success has no blocker disposition'
}
$releasedStatus = (@(Invoke-WorktreeCli @('lock', 'status', '--repo', $commonDirectory) 2) -join '')
Assert-True ((($releasedStatus | ConvertFrom-Json -Depth 16).held) -eq $false) 'script release leaves the landing lock not held'
$run = Invoke-JsonScript $lockClaimScript ($lockReleaseArguments + @('-LandingOwner', $reconcileOwner, '-Release'))
Assert-Outcome $run 'reconcile-lock-release-idempotent' 0 'pass' 'ok'

$run = Invoke-JsonScript $lockClaimScript ($lockReleaseArguments + @('-Release'))
Assert-Outcome $run 'reconcile-lock-release-blank-owner' 1 'error' 'landing-lock.release-owner-required'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.attempts -eq 0) 'blank-owner release reports no WorktreeCli attempts'
}
$releasedStatus = (@(Invoke-WorktreeCli @('lock', 'status', '--repo', $commonDirectory) 2) -join '')
Assert-True ((($releasedStatus | ConvertFrom-Json -Depth 16).held) -eq $false) 'blank-owner release mints no token and creates no lease'

$foreignLeaseOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $foreignLeaseOwner, '--session', 'foreign-fixture', '--worktree', $session, '--lease-seconds', '60') | Out-Null
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', ([guid]::NewGuid().ToString()), '-LeaseSeconds', '60', '-WaitSeconds', '1', '-PollMilliseconds', '50'))
Assert-Outcome $run 'reconcile-lock-live-contention' 2 'blocked' 'landing-lock.retryable-wait'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.disposition -ceq 'retryable-wait') 'live foreign lease exposes top-level retryable disposition'
	Assert-True ($run.Json.blocker.disposition -ceq 'retryable-wait') 'live foreign lease is retryable'
	Assert-True (-not $run.Json.blocker.requiresUserAuthority) 'live foreign lease needs no authority'
}
$run = Invoke-JsonScript $lockClaimScript ($lockReleaseArguments + @('-LandingOwner', ([guid]::NewGuid().ToString()), '-Release'))
Assert-Outcome $run 'reconcile-lock-release-denied' 1 'error' 'landing-lock.release-denied'
if ($null -ne $run.Json) {
	Assert-ExactProperties $run.Json @('schemaVersion','status','code','message','owner','lock','attempts','disposition','requiresUserAuthority','retryAfterMilliseconds','blocker') 'reconcile-lock-release-denied'
	Assert-True ($run.Json.lock.owner -ceq $foreignLeaseOwner) 'denied release reports the live foreign lease owner'
}
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $foreignLeaseOwner) | Out-Null

$sameOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $sameOwner, '--session', 'wrong-session', '--worktree', $session, '--lease-seconds', '60') | Out-Null
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $sameOwner, '-LeaseSeconds', '60', '-WaitSeconds', '1', '-PollMilliseconds', '50'))
Assert-Outcome $run 'reconcile-lock-same-owner-wrong-session' 2 'blocked' 'landing-lock.retryable-wait'
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $sameOwner) | Out-Null

$sameOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $sameOwner, '--session', 'finalize-fixture', '--worktree', $primary, '--lease-seconds', '60') | Out-Null
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $sameOwner, '-LeaseSeconds', '60', '-WaitSeconds', '1', '-PollMilliseconds', '50'))
Assert-Outcome $run 'reconcile-lock-same-owner-wrong-worktree' 2 'blocked' 'landing-lock.retryable-wait'
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $sameOwner) | Out-Null

$expiredLeaseOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $expiredLeaseOwner, '--session', 'expired-fixture', '--worktree', $session, '--lease-seconds', '60') | Out-Null
Set-ExpiredLandingLease $localAppData $expiredLeaseOwner
$recoveredOwner = [guid]::NewGuid().ToString()
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $recoveredOwner, '-LeaseSeconds', '60'))
Assert-Outcome $run 'reconcile-lock-expired-recovery' 0 'pass' 'ok'
if ($null -ne $run.Json) { Assert-True ($run.Json.lock.owner -ceq $recoveredOwner) 'expired lease recovers through exact owner compare-and-swap' }
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $recoveredOwner) | Out-Null

$unverifiableLeaseOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $unverifiableLeaseOwner, '--session', 'unverifiable-fixture', '--worktree', $session, '--lease-seconds', '60') | Out-Null
Set-UnverifiableLandingLease $localAppData $unverifiableLeaseOwner
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', ([guid]::NewGuid().ToString()), '-LeaseSeconds', '60'))
Assert-Outcome $run 'reconcile-lock-unverifiable' 2 'blocked' 'landing-lock.unverifiable'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.disposition -ceq 'authority-required') 'unverifiable lease exposes top-level authority disposition'
	Assert-True ($run.Json.blocker.disposition -ceq 'authority-required') 'unverifiable lease needs authority'
	Assert-True $run.Json.blocker.requiresUserAuthority 'unverifiable lease explicitly requires authority'
}
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $unverifiableLeaseOwner) | Out-Null

# One claim-free candidate carried through approval preparation, the guarded landing
# failures, the landing itself, and idempotent post-advance recovery.
Invoke-ScratchGit $session @('reset','--hard',$baseline) | Out-Null
Invoke-ScratchGit $session @('clean','-fd') | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'landing-change.txt'), 'landing change', [Text.UTF8Encoding]::new($false))
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$baseline,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','landing-change.txt','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'landing-candidate-created' 0 'pass' 'candidate.created'
if ($null -ne $run.Json) {
	$approvalParameters = [ordered]@{ CurrentWorktree=$session; PrimaryWorktree=$primary; CurrentBranch=$sessionBranch; PrimaryBranch='main'; ExpectedCurrentTip=$run.Json.candidate.commit; ExpectedPrimaryTip=$baseline; VerifiedCandidateCommit=$run.Json.candidate.commit; VerifiedCandidateTree=$run.Json.candidate.tree }
	$approvalInvalidIdentityParameters = [ordered]@{}
	foreach ($parameter in $approvalParameters.GetEnumerator()) { $approvalInvalidIdentityParameters[$parameter.Key] = $parameter.Value }
	$approvalInvalidIdentityParameters.ExpectedCurrentTip = 'f' * 1500
	$approvalInvalidIdentity = Invoke-JsonScriptWithSplat $approvalPreparationScript $approvalInvalidIdentityParameters $scratchBase
	Assert-Outcome $approvalInvalidIdentity 'approval-preparation-invalid-oversized-identity' 1 'error' 'input.invalid'
	Assert-True ($null -eq $approvalInvalidIdentity.Json.session.originalTip -and $approvalInvalidIdentity.Json.session.primaryTip -ceq $baseline -and $null -eq $approvalInvalidIdentity.Json.candidate.commit -and $null -eq $approvalInvalidIdentity.Json.candidate.parent) 'approval preparation nulls invalid oversized identities and preserves valid exact identities'
	$approvalParameters.FixtureFailure = 'bounded-diagnostic'
	$approvalFailure = Invoke-JsonScriptWithSplat $approvalPreparationScript $approvalParameters $scratchBase
	Assert-True ($approvalFailure.ExitCode -eq 1 -and $approvalFailure.Json.status -ceq 'error') 'approval preparation bounded failure emits error'
	Assert-ExactProperties $approvalFailure.Json @('schemaVersion','status','code','message','messageLength','messageTruncated','session','candidate','squash','sanity','verifiedCandidate','diagnostics') 'approval preparation failure'
	Assert-ExactProperties $approvalFailure.Json.diagnostics @('totalCount','items','truncated','selector','requery') 'approval preparation failure diagnostics'
	Assert-ExactProperties $approvalFailure.Json.diagnostics.items[0] @('source','code','codeLength','codeTruncated','path','pathLength','pathTruncated','message','messageLength','messageTruncated') 'approval preparation failure diagnostic item'
	Assert-True ($approvalFailure.Json.code.Length -eq 128 -and $approvalFailure.Json.message.Length -eq 512 -and $approvalFailure.Json.messageLength -eq 600 -and $approvalFailure.Json.messageTruncated -and $approvalFailure.Json.diagnostics.requery -ceq 'Invoke-FinalizeApprovalPreparation') 'approval preparation failure is bounded with canonical requery'
	$approvalParameters.FixtureFailure = 'none'
	$approval = Invoke-JsonScriptWithSplat $approvalPreparationScript $approvalParameters $scratchBase
	Assert-Outcome $approval 'approval-preparation-normal-success' 0 'pass' 'ok'
	Assert-ExactProperties $approval.Json @('schemaVersion','status','code','message','messageLength','messageTruncated','session','candidate','squash','sanity','verifiedCandidate','diagnostics') 'approval preparation success'
	Assert-ExactProperties $approval.Json.session @('originalTip','currentTip','primaryTip') 'approval preparation session'
	Assert-ExactProperties $approval.Json.candidate @('commit','tree','parent') 'approval preparation candidate'
	Assert-ExactProperties $approval.Json.squash @('disposition','commitCount','refUpdated','rollback') 'approval preparation squash'
	Assert-ExactProperties $approval.Json.sanity @('initial','final') 'approval preparation sanity'
	Assert-ExactProperties $approval.Json.verifiedCandidate @('supplied','matched') 'approval preparation verified candidate'
	Assert-True ($approval.Json.schemaVersion -ceq 'broken-engine-finalize-approval-preparation/v2' -and $approval.Json.candidate.commit -ceq $run.Json.candidate.commit -and $approval.Json.candidate.tree -ceq $run.Json.candidate.tree -and $approval.Json.candidate.parent -ceq $baseline -and $approval.Json.sanity.initial -ceq 'pass' -and $approval.Json.sanity.final -ceq 'pass' -and $approval.Json.verifiedCandidate.supplied -and $approval.Json.verifiedCandidate.matched) 'approval preparation success projects exact identities and pass states'
	Assert-True ($approval.Json.PSObject.Properties.Name -cnotcontains 'tips' -and $approval.Json.PSObject.Properties.Name -cnotcontains 'identities') 'approval preparation hides raw identities'
	$landingParameters = [ordered]@{ CurrentWorktree=$session; PrimaryWorktree=$primary; CurrentBranch=$sessionBranch; PrimaryBranch='main'; ExpectedCurrentTip=$run.Json.candidate.commit; ExpectedPrimaryTip=$baseline; SessionLabel='finalize-fixture'; ApprovedSessionCommit=$run.Json.candidate.commit; ApprovedCandidateTree=$run.Json.candidate.tree }
	$landingInvalidIdentityParameters = [ordered]@{}
	foreach ($parameter in $landingParameters.GetEnumerator()) { $landingInvalidIdentityParameters[$parameter.Key] = $parameter.Value }
	$landingInvalidIdentityParameters.ApprovedSessionCommit = 'f' * 1500
	$landingInvalidIdentity = Invoke-JsonScriptWithSplat $landingScript $landingInvalidIdentityParameters $scratchBase
	Assert-Outcome $landingInvalidIdentity 'landing-invalid-oversized-identity' 1 'error' 'input.commit-invalid'
	Assert-True ($null -eq $landingInvalidIdentity.Json.candidate.commit -and $landingInvalidIdentity.Json.candidate.tree -ceq $run.Json.candidate.tree) 'landing nulls invalid oversized identities and preserves valid exact identities'
	$landingParameters.FixtureFailure = 'bounded-diagnostic'
	$landingBoundedFailure = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-True ($landingBoundedFailure.ExitCode -eq 1 -and $landingBoundedFailure.Json.status -ceq 'error') 'landing bounded failure emits error'
	Assert-ExactProperties $landingBoundedFailure.Json @('schemaVersion','status','code','message','messageLength','messageTruncated','primaryAdvanced','candidate','planClaim','lock','cleanup','disposition','requiresUserAuthority','retryAfterMilliseconds','diagnostics','residuals') 'landing failure'
	Assert-ExactProperties $landingBoundedFailure.Json.diagnostics @('totalCount','items','truncated','selector','requery') 'landing failure diagnostics'
	Assert-ExactProperties $landingBoundedFailure.Json.diagnostics.items[0] @('source','code','codeLength','codeTruncated','path','pathLength','pathTruncated','message','messageLength','messageTruncated') 'landing failure diagnostic item'
	Assert-True ($landingBoundedFailure.Json.code.Length -eq 128 -and $landingBoundedFailure.Json.message.Length -eq 512 -and $landingBoundedFailure.Json.messageLength -eq 600 -and $landingBoundedFailure.Json.messageTruncated) 'landing failure top-level text is bounded'
	$landingParameters.FixtureFailure = 'compare-and-swap'
	$landingCasMismatch = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $landingCasMismatch 'landing-compare-and-swap-mismatch' 2 'blocked' 'git.compare-and-swap-failed'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'landing compare-and-swap mismatch leaves primary ref unchanged'
	Assert-True ([string]::IsNullOrWhiteSpace((@(Invoke-ScratchGit $primary @('status','--porcelain=v1','-z','--untracked-files=all')) -join ''))) 'landing compare-and-swap mismatch preserves primary checkout'
	$landingParameters.FixtureFailure = 'post-reset'
	$landingRollback = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $landingRollback 'landing-post-reset-rollback' 2 'blocked' 'candidate.postcondition-failed'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'landing post-reset rollback restores primary ref and checkout head'
	Assert-True ([string]::IsNullOrWhiteSpace((@(Invoke-ScratchGit $primary @('status','--porcelain=v1','-z','--untracked-files=all')) -join ''))) 'landing post-reset rollback restores primary index and worktree'
	$landingParameters.FixtureFailure = 'none'
	$landing = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $landing 'exact-candidate-landing-success' 0 'landed' 'ok'
	Assert-ExactProperties $landing.Json @('schemaVersion','status','code','message','messageLength','messageTruncated','primaryAdvanced','candidate','planClaim','lock','cleanup','disposition','requiresUserAuthority','retryAfterMilliseconds','diagnostics','residuals') 'landing success'
	Assert-ExactProperties $landing.Json.candidate @('commit','tree','treeVerified') 'landing candidate'
	Assert-ExactProperties $landing.Json.planClaim @('requested','released') 'landing Plan claim'
	Assert-ExactProperties $landing.Json.lock @('claimed','released','claimCode','disposition','requiresUserAuthority','retryAfterMilliseconds','attempts') 'landing lock'
	Assert-ExactProperties $landing.Json.cleanup @('worktreesClear','problems') 'landing cleanup'
	Assert-ExactProperties $landing.Json.cleanup.problems @('totalCount','items','truncated','selector','requery') 'landing cleanup problems'
	Assert-ExactProperties $landing.Json.residuals @('totalCount','items','truncated','selector','requery') 'landing residuals'
	Assert-True ($landing.Json.schemaVersion -ceq 'broken-engine-finalize-landing/v2' -and $landing.Json.candidate.commit -ceq $run.Json.candidate.commit -and $landing.Json.candidate.tree -ceq $run.Json.candidate.tree -and $landing.Json.candidate.treeVerified -and $landing.Json.lock.claimed -and $landing.Json.lock.released -and $landing.Json.cleanup.worktreesClear) 'landing success projects exact candidate, lock, and cleanup proof'
	Assert-True (-not $landing.Json.planClaim.requested -and -not $landing.Json.planClaim.released) 'a claim-free landing touches no Plan claim'
	Assert-True ($landing.Json.PSObject.Properties.Name -cnotcontains 'identities' -and $landing.Json.PSObject.Properties.Name -cnotcontains 'tips' -and $landing.Json.PSObject.Properties.Name -cnotcontains 'locks' -and $landing.Json.PSObject.Properties.Name -cnotcontains 'blocker') 'landing hides checkout, lock-owner, and raw blocker objects'
	Assert-True ($landing.Json.primaryAdvanced -and ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()) -ceq $run.Json.candidate.commit) 'landing primary ref equals the reviewed candidate commit exactly'
	$landingParameters.ExpectedPrimaryTip = $run.Json.candidate.commit
	$recovery = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $recovery 'exact-candidate-post-advance-recovery' 0 'landed' 'ok'
}

Write-Host ''
if ($script:Failures.Count -gt 0) {
	Write-Host "Finalize workflow fixtures FAILED ($($script:Failures.Count) assertion(s))."
	$fixtureExitCode = 1
}
else {
	Write-Host 'Finalize workflow fixtures passed.'
}
}
finally {
	if ($null -ne $fixtureEnvironment) {
		foreach ($entry in $fixtureEnvironment.GetEnumerator()) { [Environment]::SetEnvironmentVariable($entry.Key, $previousEnvironment[$entry.Key]) }
	}
	$validatedScratch = Assert-SafeScratchRoot $scratchParent $scratchBase $scratchLeaf
	if (Test-Path -LiteralPath $validatedScratch) {
		Remove-Item -LiteralPath $validatedScratch -Recurse -Force -Confirm:$false
	}
}
exit $fixtureExitCode
