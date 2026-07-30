# Scratch-repository coverage for finalization preflight request binding, one
# session landing, and idempotent post-advance recovery. Uses the supplied
# capability-current WorktreeCli only inside disposable Output and never touches
# a real primary checkout, queue, or canonical executable.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\AgentWorktreeSession.psm1') -Force -DisableNameChecking

$script:Failures = [Collections.Generic.List[string]]::new()
$preflightScript = Join-Path $PSScriptRoot 'Test-FinalizePreflight.ps1'
$verifyPlanPrevalidationScript = Join-Path $PSScriptRoot '..\..\verify-changes\scripts\Test-VerifyPlanPrevalidation.ps1'
$landingScript = Join-Path $PSScriptRoot 'Invoke-FinalizeLanding.ps1'
$candidateScript = Join-Path $PSScriptRoot 'Invoke-FinalizeCandidateCommit.ps1'
$lockClaimScript = Join-Path $PSScriptRoot 'Invoke-FinalizeLockClaim.ps1'
$moduleSource = Join-Path $PSScriptRoot '..\..\..\scripts'
$WorktreeCliExecutable = (Get-Item -LiteralPath $WorktreeCliExecutable -Force -ErrorAction Stop).FullName

function Assert-True([bool] $Condition, [string] $Name) {
	if ($Condition) { Write-Host "pass $Name" } else { $script:Failures.Add($Name); Write-Host "FAIL $Name" }
}

function Assert-SafeScratchRoot([string] $Parent, [string] $Root, [string] $ExpectedLeaf) {
	$parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
	$rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
	if ((Split-Path -Parent $rootPath) -cne $parentPath -or (Split-Path -Leaf $rootPath) -cne $ExpectedLeaf -or $ExpectedLeaf -cnotmatch '^[0-9a-f]{32}$') {
		throw "Fixture scratch root failed containment validation: '$rootPath'."
	}
	return $rootPath
}

function Invoke-ScratchGit([string] $Root, [string[]] $Arguments) {
	$output = @(& git -C $Root -c user.name=fixture -c user.email=fixture@example.com @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
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
	if ($null -eq $Run.Json) { Write-Host "  stdout: $($Run.Text)"; Write-Host "  stderr: $($Run.Stderr)"; return }
	Assert-True ($Run.ExitCode -eq $ExpectedExit) "$Case exit=$ExpectedExit (was $($Run.ExitCode))"
	Assert-True ($Run.Json.status -ceq $ExpectedStatus) "$Case status=$ExpectedStatus (was $($Run.Json.status))"
	Assert-True ($Run.Json.code -ceq $ExpectedCode) "$Case code=$ExpectedCode (was $($Run.Json.code))"
	if ($Run.ExitCode -ne $ExpectedExit -or $Run.Json.code -cne $ExpectedCode) { Write-Host "  message: $($Run.Json.message)"; Write-Host "  stderr: $($Run.Stderr)" }
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
New-Item -ItemType Directory -Force (Join-Path $primary 'Documents\Plans') | Out-Null
$metadata = '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-01T00:00:00.000Z","dependsOn":[]} -->'
$terminalPlan = 'Documents/Plans/Terminal/Recovery.md'
$primaryTerminalPlan = Join-Path $primary $terminalPlan
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($primaryTerminalPlan)) | Out-Null
[IO.File]::WriteAllText($primaryTerminalPlan, "$metadata`n# Recovery fixture`n", [Text.UTF8Encoding]::new($false))
$baseChildMarker = '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-02T00:00:00.000Z","dependsOn":["' + $terminalPlan + '"]} -->'
[IO.File]::WriteAllText((Join-Path $primary 'Documents\\Plans\\LateChild.md'), "$baseChildMarker`n# Late child fixture`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'Documents\Plans\AGENTS.md'), '# Directory guidance', [Text.UTF8Encoding]::new($false))
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
# The receipt constructor requires branch == "<client>/<worktreeId>"; use codex.
$uuid = [guid]::NewGuid().ToString()
$owner = [guid]::NewGuid().ToString()
$sessionBranch = "codex/$uuid"
Invoke-ScratchGit $primary @('worktree', 'add', '-b', $sessionBranch, $session, $baseline) | Out-Null
New-Item -ItemType Directory -Force (Join-Path $session 'Temp') | Out-Null
$sessionOutputParent = Join-Path $session 'Tools\WorktreeCli\Platforms\VisualStudio2026'
New-Item -ItemType Directory -Force $sessionOutputParent | Out-Null
New-Item -ItemType Junction -Path (Join-Path $sessionOutputParent 'Output') -Target $primaryOutput | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'change.txt'), 'session change', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add', 'change.txt') | Out-Null
Invoke-ScratchGit $session @('commit', '-m', 'fixture change') | Out-Null
# Align primary onto the session tip so the receipt baseline is fixed for every session-landing
# scenario below; the terminal claim later selects from an equal session/primary tree.
$sessionTip = (@(Invoke-ScratchGit $session @('rev-parse', 'HEAD')))[0].Trim()
Invoke-ScratchGit $primary @('merge', '--ff-only', $sessionTip) | Out-Null
$baseline = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()
$candidateMessage = Join-Path $scratchBase 'candidate-message.txt'
[IO.File]::WriteAllText($candidateMessage, "fixture candidate`n", [Text.UTF8Encoding]::new($false))

# Candidate construction is deliberately before verification.  This isolated
# coverage exercises the Git boundary without a Plan receipt.
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
$run = Invoke-JsonScript $candidateScript @('-Route','primary-commit','-CurrentWorktree',$primary,'-PrimaryWorktree',$primary,'-CurrentBranch','main','-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$baseline,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','primary-active-owned.txt','primary-staged-owned.txt','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'primary-candidate-temporary-index' 0 'pass' 'candidate.created'
if ($null -ne $run.Json) {
	$verifiedCandidate = $run.Json.candidate.commit; $verifiedTree = $run.Json.candidate.tree
	Assert-True ($beforePrimaryIndex -ceq ((@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n"))) 'temporary index preserves real index'
	Assert-True ($primaryDisjointBefore -ceq (@(Invoke-ScratchGit $primary @('status','--porcelain=v1','--untracked-files=all')) -join "`n")) 'primary candidate preserves disjoint staged unstaged and untracked state'
	$rollback = Invoke-JsonScript $candidateScript @('-Route','primary-commit','-CurrentWorktree',$primary,'-PrimaryWorktree',$primary,'-CurrentBranch','main','-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$baseline,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','primary-active-owned.txt','primary-staged-owned.txt','-CommitMessageFile',$candidateMessage,'-VerifiedCandidateCommit',$verifiedCandidate,'-VerifiedCandidateTree',$verifiedTree,'-AdvancePrimary','-FixtureFailure','postcondition')
	Assert-Outcome $rollback 'primary-candidate-postcondition-rollback' 2 'blocked' 'candidate.postcondition-failed'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'guarded rollback restores expected old primary only from candidate'
	Assert-True ($beforePrimaryIndex -ceq ((@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n"))) 'guarded rollback preserves real index'
	$indexRollback = Invoke-JsonScript $candidateScript @('-Route','primary-commit','-CurrentWorktree',$primary,'-PrimaryWorktree',$primary,'-CurrentBranch','main','-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$baseline,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','primary-active-owned.txt','primary-staged-owned.txt','-CommitMessageFile',$candidateMessage,'-VerifiedCandidateCommit',$verifiedCandidate,'-VerifiedCandidateTree',$verifiedTree,'-AdvancePrimary','-FixtureFailure','post-index-mutation')
	Assert-Outcome $indexRollback 'primary-candidate-post-index-rollback' 2 'blocked' 'candidate.postcondition-failed'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'primary post-index rollback restores expected old ref'
	Assert-True ($beforePrimaryIndex -ceq ((@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n"))) 'primary post-index rollback restores owned and unrelated index entries'
	Assert-True ($primaryDisjointBefore -ceq (@(Invoke-ScratchGit $primary @('status','--porcelain=v1','--untracked-files=all')) -join "`n")) 'primary post-index rollback restores owned and unrelated status'
	Assert-True ($primaryStagedOwnedIndex -ceq (@(Invoke-ScratchGit $primary @('ls-files','--stage','--','primary-staged-owned.txt')) -join "`n")) 'primary post-index rollback restores staged owned mode object and stage exactly'
	Assert-True ($primaryActiveOwnedWorktree -ceq [IO.File]::ReadAllText((Join-Path $primary 'primary-active-owned.txt'), [Text.UTF8Encoding]::new($false,$true)) -and $primaryStagedOwnedWorktree -ceq [IO.File]::ReadAllText((Join-Path $primary 'primary-staged-owned.txt'), [Text.UTF8Encoding]::new($false,$true))) 'primary post-index rollback preserves owned worktree bytes'
	Assert-True ($primaryUnrelatedWorktree -ceq [IO.File]::ReadAllText((Join-Path $primary 'primary-disjoint-untracked.txt'), [Text.UTF8Encoding]::new($false,$true))) 'primary post-index rollback preserves unrelated worktree bytes'
	$advance = Invoke-JsonScript $candidateScript @('-Route','primary-commit','-CurrentWorktree',$primary,'-PrimaryWorktree',$primary,'-CurrentBranch','main','-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$baseline,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','primary-active-owned.txt','primary-staged-owned.txt','-CommitMessageFile',$candidateMessage,'-VerifiedCandidateCommit',$verifiedCandidate,'-VerifiedCandidateTree',$verifiedTree,'-AdvancePrimary')
	Assert-Outcome $advance 'primary-candidate-atomic-advance' 0 'pass' 'candidate.advanced'
	Assert-True ($verifiedCandidate -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'primary branch equals reviewed candidate'
	Assert-True ($verifiedTree -ceq ((@(Invoke-ScratchGit $primary @('rev-parse',"$verifiedCandidate^{tree}")))[0].Trim())) 'primary tree equals reviewed candidate tree'
}
Invoke-ScratchGit $primary @('reset','--hard',$baseline) | Out-Null
Remove-Item -LiteralPath (Join-Path $primary 'primary-active-owned.txt'),(Join-Path $primary 'primary-staged-owned.txt'),(Join-Path $primary 'primary-disjoint-staged.txt'),(Join-Path $primary 'primary-disjoint-untracked.txt') -Force -ErrorAction SilentlyContinue

$fixtureEnvironment = [ordered]@{
	LOCALAPPDATA = $localAppData
	BROKEN_ENGINE_WORKTREE_PATH = $session
	BROKEN_ENGINE_SESSION_BRANCH = $sessionBranch
	BROKEN_ENGINE_PRIMARY_CHECKOUT = $primary
	BROKEN_ENGINE_TARGET_BRANCH = 'main'
	BROKEN_ENGINE_BASELINE = $baseline
	BROKEN_ENGINE_SESSION_OWNER = $owner
	BROKEN_ENGINE_FINALIZE_WORKFLOW_FIXTURE = '1'
}
foreach ($entry in $fixtureEnvironment.GetEnumerator()) {
	$previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key)
	[Environment]::SetEnvironmentVariable($entry.Key, $entry.Value)
}

$commonDirectory = ((@(Invoke-ScratchGit $primary @('rev-parse', '--path-format=absolute', '--git-common-dir')))[0].Trim())
# The in-worktree session receipt is the session-landing identity authority now; write it after
# the primary/session alignment so its fixed baseline equals every session-landing preflight's.
$primaryIdentity = Get-AgentWorktreePrimaryIdentity $primary
$receipt = New-AgentWorktreeSessionReceipt -Client codex -PrimaryCheckout $primaryIdentity.Root -GitCommonDirectory $primaryIdentity.CommonDirectory `
	-Worktree $session -WorktreeId $uuid -Branch $sessionBranch -TargetBranch 'main' -Baseline $baseline -SessionOwner $owner
Write-AgentWorktreeSessionReceipt -Worktree $session -Receipt $receipt | Out-Null
# Landing-lock fixtures below retain their independent lease and recovery coverage.

# Reconciliation uses the claim sidecar; landing calls the same common helper.
$reconcileOwner = [guid]::NewGuid().ToString()
$lockClaimArguments = @('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $reconcileOwner, '-LeaseSeconds', '60')
$run = Invoke-JsonScript $lockClaimScript $lockClaimArguments
Assert-Outcome $run 'reconcile-lock-claim' 0 'pass' 'ok'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.owner -ceq $reconcileOwner) 'reconcile lock preserves supplied owner'
	Assert-True ($null -eq $run.Json.blocker) 'reconcile lock success has no blocker disposition'
}
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $reconcileOwner) | Out-Null

$foreignLeaseOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $foreignLeaseOwner, '--session', 'foreign-fixture', '--worktree', $session, '--lease-seconds', '60') | Out-Null
$run = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', ([guid]::NewGuid().ToString()), '-LeaseSeconds', '60', '-WaitSeconds', '1', '-PollMilliseconds', '50'))
Assert-Outcome $run 'reconcile-lock-live-contention' 2 'blocked' 'landing-lock.retryable-wait'
if ($null -ne $run.Json) {
	Assert-True ($run.Json.disposition -ceq 'retryable-wait') 'live foreign lease exposes top-level retryable disposition'
	Assert-True ($run.Json.blocker.disposition -ceq 'retryable-wait') 'live foreign lease is retryable'
	Assert-True (-not $run.Json.blocker.requiresUserAuthority) 'live foreign lease needs no authority'
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

# Primary-commit mode needs neither a session receipt nor session-owner environment: its claim
# classification is 'not-required' and the receipt gate never runs. Clear the six session
# environment variables first so this proves genuine environment-independence, not a value still in
# scope; restore them afterward because later scenarios rely on the fixture environment.
$primaryCommitEnvironmentNames = @('BROKEN_ENGINE_SESSION_OWNER','BROKEN_ENGINE_WORKTREE_PATH','BROKEN_ENGINE_SESSION_BRANCH','BROKEN_ENGINE_PRIMARY_CHECKOUT','BROKEN_ENGINE_TARGET_BRANCH','BROKEN_ENGINE_BASELINE')
foreach ($name in $primaryCommitEnvironmentNames) { [Environment]::SetEnvironmentVariable($name, $null) }
try {
	$primaryCommitPreflight = @('-Mode','primary-commit','-Checkpoint','initial','-CurrentWorktree',$primary,'-PrimaryWorktree',$primary,'-CurrentBranch','main','-PrimaryBranch','main','-Baseline',$baseline,'-WaitSeconds','5')
	$run = Invoke-JsonScript $preflightScript $primaryCommitPreflight
	Assert-Outcome $run 'preflight-primary-commit' 0 'pass' 'ok'
	if ($null -ne $run.Json) { Assert-True ($run.Json.claim.classification -ceq 'not-required') 'primary-commit mode requires no session claim (no session-owner environment set)' }
}
finally {
	foreach ($name in $primaryCommitEnvironmentNames) { [Environment]::SetEnvironmentVariable($name, $fixtureEnvironment[$name]) }
}

# Finalization validates the reconciled session tree before any lock or primary
# mutation. Invalid metadata and dependency cycles both fail closed. The landing takes
# its own transient claim; the fixture registers no session claim.
Invoke-ScratchGit $session @('reset','--hard',$baseline) | Out-Null
Invoke-ScratchGit $session @('clean','-fd') | Out-Null
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\Invalid.md'), '<!-- broken-engine-plan/v1 {"createdUtc":42,"dependsOn":[]} -->', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add','Documents/Plans/Invalid.md') | Out-Null
Invoke-ScratchGit $session @('commit','-m','fixture invalid Plan metadata') | Out-Null
$invalidTip = (@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()
$directParameters = [ordered]@{ CurrentWorktree=$session; PrimaryWorktree=$primary; CurrentBranch=$sessionBranch; PrimaryBranch='main'; Baseline=$baseline; ExpectedCurrentTip=$invalidTip; ExpectedPrimaryTip=$baseline; SessionOwner=$owner; SessionLabel='finalize-fixture'; ApprovedSessionCommit=$invalidTip; ApprovedCandidateTree=((@(Invoke-ScratchGit $session @('rev-parse',"$invalidTip^{tree}")))[0].Trim()) }
$run = Invoke-JsonScriptWithSplat $landingScript $directParameters $scratchBase
Assert-Outcome $run 'invalid-metadata-blocks-landing' 1 'error' 'plan.validation-failed'
Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'invalid metadata leaves primary unchanged'
$run = Invoke-JsonScript $verifyPlanPrevalidationScript @('-Worktree',$session,'-Baseline',$baseline)
Assert-Outcome $run 'verify-plan-prevalidation-invalid-metadata' 2 'blocked' 'validation.failed'
if ($null -ne $run.Json) { Assert-True ($run.Json.validation.diagnostics.Count -gt 0) 'verify Plan prevalidation projects invalid metadata diagnostics' }
Remove-Item -LiteralPath (Join-Path $session 'Documents\Plans\Invalid.md') -Force
$cycleA = '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-03T00:00:00.000Z","dependsOn":["Documents/Plans/CycleB.md"]} -->'
$cycleB = '<!-- broken-engine-plan/v1 {"createdUtc":"2024-01-04T00:00:00.000Z","dependsOn":["Documents/Plans/CycleA.md"]} -->'
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\CycleA.md'), "$cycleA`n# Cycle A`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $session 'Documents\Plans\CycleB.md'), "$cycleB`n# Cycle B`n", [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add','-A') | Out-Null
Invoke-ScratchGit $session @('commit','-m','fixture Plan metadata cycle') | Out-Null
$cycleTip = (@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()
$directParameters.ExpectedCurrentTip = $cycleTip
$directParameters.ApprovedSessionCommit = $cycleTip
$directParameters.ApprovedCandidateTree = (@(Invoke-ScratchGit $session @('rev-parse',"$cycleTip^{tree}")))[0].Trim()
$run = Invoke-JsonScriptWithSplat $landingScript $directParameters $scratchBase
Assert-Outcome $run 'metadata-cycle-blocks-landing' 1 'error' 'plan.validation-failed'
Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'metadata cycle leaves primary unchanged'
# The negative scenarios never mutated primary; discard their session commits so the terminal
# claim starts from an equal session/primary tree at the fixed receipt baseline.
Invoke-ScratchGit $session @('reset','--hard',$baseline) | Out-Null
Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim())) 'terminal claim starts from aligned primary and session tips'
$run = Invoke-JsonScript $verifyPlanPrevalidationScript @('-Worktree',$session,'-Baseline',$baseline)
Assert-Outcome $run 'verify-plan-prevalidation-no-claim' 0 'pass' 'ok'
$wrongOutput = Join-Path $scratchBase 'wrong-output'
New-Item -ItemType Directory -Force $wrongOutput | Out-Null
Copy-Item -LiteralPath (Join-Path $primaryOutput 'WorktreeCli.exe') -Destination (Join-Path $wrongOutput 'WorktreeCli.exe') -Force
$sessionOutput = Join-Path $sessionOutputParent 'Output'
Remove-Item -LiteralPath $sessionOutput -Force
New-Item -ItemType Junction -Path $sessionOutput -Target $wrongOutput | Out-Null
$run = Invoke-JsonScript $verifyPlanPrevalidationScript @('-Worktree',$session,'-Baseline',$baseline)
Assert-Outcome $run 'verify-plan-prevalidation-wrong-output-target' 2 'blocked' 'context.output-wrong-target'
Remove-Item -LiteralPath $sessionOutput -Force
New-Item -ItemType Junction -Path $sessionOutput -Target $primaryOutput | Out-Null

# Candidate creation consumes the original receipt-bound terminal proof. It must
# never replay terminal preparation after the receipt has reached awaiting-landing.
$receiptPath = Join-Path (Join-Path $session 'Temp') 'next-plan-claim.json'
$claim = (Invoke-WorktreeCli @('plan','claim-next','--repo',$commonDirectory,'--primary-worktree',$primary,'--worktree',$session,'--branch',$sessionBranch,'--owner',$owner,'--session',$owner,'--write-claim-receipt',$receiptPath,'--plan',$terminalPlan) | ConvertFrom-Json -Depth 100)
Assert-True $claim.claimed 'fixture claimed terminal Plan for candidate preparation'
$lateChildMarker = '<!-- broken-engine-plan/v1 ' + (([ordered]@{ createdUtc = '2024-01-02T00:00:00.000Z'; dependsOn = @($terminalPlan) }) | ConvertTo-Json -Compress) + ' -->'
[IO.File]::WriteAllText((Join-Path $session 'Documents\\Plans\\LateChild.md'), "$lateChildMarker`n# Late child fixture`n", [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $session @('add','Documents/Plans/LateChild.md') | Out-Null
$prepared = (Invoke-WorktreeCli @('plan','prepare-completion','--repo',$commonDirectory,'--worktree',$session,'--claim-receipt',$receiptPath,'--claim-receipt-sha256',((Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant())) | ConvertFrom-Json -Depth 100)
Assert-True ($prepared.prepared -and $prepared.claimState -ceq 'awaiting-landing' -and $prepared.disposition -ceq 'completed') 'fixture terminal preparation produced original completion result'
$receiptJson = [IO.File]::ReadAllText($receiptPath,[Text.UTF8Encoding]::new($false,$true)) | ConvertFrom-Json -Depth 32
$terminalProof = [ordered]@{schemaVersion='broken-engine-next-plan-terminal-proof/v1';receiptSha256=((Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant());repository=$receiptJson.repository;worktree=$receiptJson.worktree;branch=$receiptJson.branch;disposition=$prepared.disposition;claimState=$prepared.claimState;changedPaths=@($prepared.changedPaths);manifestDigest=$prepared.manifestDigest}
[IO.File]::WriteAllText((Join-Path $session 'Temp\next-plan-terminal-result.json'),($terminalProof | ConvertTo-Json -Depth 20 -Compress),[Text.UTF8Encoding]::new($false))
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$baseline,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','change.txt','-WorktreeCliExecutable',$WorktreeCliExecutable,'-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'terminal-proof-before-candidate' 0 'pass' 'candidate.created'
if ($null -ne $run.Json) {
	$terminalUnion = @(@($terminalPlan,'Documents/Plans/LateChild.md') | Sort-Object)
	$terminalActual = @($run.Json.terminalPaths | Sort-Object)
	if (($terminalActual -join "`n") -cne ($terminalUnion -join "`n")) { Write-Host "  terminal paths actual=[$($terminalActual -join ',')] expected=[$($terminalUnion -join ',')]" }
	Assert-True (($terminalActual -join "`n") -ceq ($terminalUnion -join "`n")) 'terminal deletion and direct child rewrite join candidate union'
	$candidatePaths = @((Invoke-ScratchGit $session @('diff-tree','--no-commit-id','--name-only','-r',$run.Json.candidate.commit)) | Sort-Object)
	if (($candidatePaths -join "`n") -cne ($terminalUnion -join "`n")) { Write-Host "  candidate paths actual=[$($candidatePaths -join ',')] expected=[$($terminalUnion -join ',')]" }
	Assert-True (($candidatePaths -join "`n") -ceq ($terminalUnion -join "`n")) 'terminal candidate delta is the exact receipt-proven union'
	Assert-True ($run.Json.terminalPreparation.disposition -ceq 'completed' -and $run.Json.terminalPreparation.changedPaths.Count -eq $prepared.changedPaths.Count) 'candidate preserved original terminal disposition and changed paths'
	$landingParameters = [ordered]@{ CurrentWorktree=$session; PrimaryWorktree=$primary; CurrentBranch=$sessionBranch; PrimaryBranch='main'; Baseline=$baseline; ExpectedCurrentTip=$run.Json.candidate.commit; ExpectedPrimaryTip=$baseline; SessionOwner=$owner; SessionLabel='finalize-fixture'; ApprovedSessionCommit=$run.Json.candidate.commit; ApprovedCandidateTree=$run.Json.candidate.tree }
	$landingParameters.FixtureFailure = 'compare-and-swap'
	$landingCasMismatch = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $landingCasMismatch 'landing-compare-and-swap-mismatch' 2 'blocked' 'git.compare-and-swap-failed'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'landing compare-and-swap mismatch leaves primary ref unchanged'
Assert-True ([string]::IsNullOrWhiteSpace((@(Invoke-ScratchGit $primary @('status','--porcelain=v1','-z','--untracked-files=all')) -join ''))) 'landing compare-and-swap mismatch preserves primary checkout'
	$landingParameters.FixtureFailure = 'post-reset'
	$landingRollback = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $landingRollback 'landing-post-reset-rollback' 2 'blocked' 'candidate.postcondition-failed'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'landing post-reset rollback restores primary ref'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'landing post-reset rollback restores primary checkout head'
Assert-True ([string]::IsNullOrWhiteSpace((@(Invoke-ScratchGit $primary @('status','--porcelain=v1','-z','--untracked-files=all')) -join ''))) 'landing post-reset rollback restores primary index and worktree'
	$landingParameters.FixtureFailure = 'none'
	$landing = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $landing 'exact-candidate-landing-success' 0 'landed' 'ok'
	Assert-True ($landing.Json.primaryAdvanced -and ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()) -ceq $run.Json.candidate.commit) 'landing primary ref equals the reviewed candidate commit exactly'
	$landingParameters.ExpectedPrimaryTip = $run.Json.candidate.commit
	$recovery = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $recovery 'exact-candidate-post-advance-recovery' 0 'landed' 'ok'
}
Invoke-ScratchGit $session @('reset','--hard',$baseline) | Out-Null
Remove-Item -LiteralPath (Join-Path $session 'Documents\\Plans\\LateChild.md') -Force -ErrorAction SilentlyContinue


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
