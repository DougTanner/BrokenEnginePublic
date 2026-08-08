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
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'WorktreeCliSessionExclusion.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
Import-Module (Join-Path $sharedScripts 'WorktreeCliSessionExclusion.psm1') -Force -DisableNameChecking
Import-Module (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1') -Force -DisableNameChecking

$script:Failures = [Collections.Generic.List[string]]::new()
$landingScript = Join-Path $PSScriptRoot 'Invoke-FinalizeLanding.ps1'
$approvalPreparationScript = Join-Path $PSScriptRoot 'Invoke-FinalizeApprovalPreparation.ps1'
$candidateScript = Join-Path $PSScriptRoot 'Invoke-FinalizeCandidateCommit.ps1'
$lockClaimScript = Join-Path $PSScriptRoot 'Invoke-FinalizeLockClaim.ps1'
$approvalReviewScript = Join-Path $PSScriptRoot 'Show-FinalizeApprovalReview.ps1'
$moduleSource = $sharedScripts
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

# A live foreign landing lease makes production landing wait for its documented 300-second bound.
# Start the child separately so these fixtures can observe that it did not adopt the lease, then
# terminate and reap it before releasing the scratch lease; production wait values stay untouched.
function Start-JsonScriptWithSplat([string] $Script, [Collections.IDictionary] $Parameters, [string] $ScratchRoot) {
	$invocationRoot = Join-Path $ScratchRoot 'splat-invocation'
	New-Item -ItemType Directory -Force $invocationRoot | Out-Null
	$suffix = [guid]::NewGuid().ToString('N')
	$payloadPath = Join-Path $invocationRoot ($suffix + '.json')
	$wrapperPath = Join-Path $invocationRoot ($suffix + '.ps1')
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
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = "$PSHOME\pwsh.exe"
	$start.WorkingDirectory = $ScratchRoot
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	[void] $start.ArgumentList.Add('-NoProfile')
	[void] $start.ArgumentList.Add('-File')
	[void] $start.ArgumentList.Add($wrapperPath)
	[void] $start.ArgumentList.Add('-TargetScript')
	[void] $start.ArgumentList.Add($Script)
	[void] $start.ArgumentList.Add('-PayloadPath')
	[void] $start.ArgumentList.Add($payloadPath)
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start '$PSHOME\pwsh.exe'." }
	return [pscustomobject]@{
		Process = $process
		StdoutTask = $process.StandardOutput.ReadToEndAsync()
		StderrTask = $process.StandardError.ReadToEndAsync()
	}
}

function Stop-JsonScript($Started) {
	try {
		if (-not $Started.Process.WaitForExit(0)) {
			try { $Started.Process.Kill($true) } catch {
				if (-not $Started.Process.HasExited) { try { $Started.Process.Kill() } catch { } }
			}
			if (-not $Started.Process.WaitForExit(5000)) { throw "Timed out reaping JSON script process $($Started.Process.Id)." }
		}
		$Started.StdoutTask.GetAwaiter().GetResult() | Out-Null
		$Started.StderrTask.GetAwaiter().GetResult() | Out-Null
	}
	finally { $Started.Process.Dispose() }
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

function Set-NearExpiryLandingLease([string] $LocalAppData, [string] $Owner, [int] $RemainingMilliseconds = 1500) {
	$lease = @(Get-ChildItem -LiteralPath $LocalAppData -Recurse -Filter '*.lock' -File -Force | Where-Object {
		try {
			$metadata = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
			return $metadata.domain -ceq 'landing' -and $metadata.owner -ceq $Owner
		}
		catch { return $false }
	})
	if ($lease.Count -ne 1) { throw "Could not locate one scratch landing lease for '$Owner'." }
	$metadata = Get-Content -LiteralPath $lease[0].FullName -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
	$durationSeconds = [int]$metadata.leaseDurationSeconds
	$now = [DateTime]::UtcNow
	$heartbeat = $now.AddMilliseconds(-($durationSeconds * 1000 - $RemainingMilliseconds))
	$expires = $heartbeat.AddSeconds($durationSeconds)
	[IO.File]::SetAttributes($lease[0].FullName, [IO.FileAttributes]::Normal)
	$metadata.claimedAt = $heartbeat.ToString('yyyy-MM-ddTHH:mm:ss.fffZ', [Globalization.CultureInfo]::InvariantCulture)
	$metadata.heartbeatAt = $metadata.claimedAt
	$metadata.expiresAt = $expires.ToString('yyyy-MM-ddTHH:mm:ss.fffZ', [Globalization.CultureInfo]::InvariantCulture)
	[IO.File]::WriteAllText($lease[0].FullName, ($metadata | ConvertTo-Json -Depth 16 -Compress), [Text.UTF8Encoding]::new($false))
	return [pscustomobject]@{ Path = $lease[0].FullName; ExpiresAt = [DateTimeOffset]::Parse($metadata.expiresAt, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AssumeUniversal) }
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
# The landing script runs its internal rebase through plain git.exe, so the committer identity has to
# live in the scratch repository instead of the -c arguments Invoke-ScratchGit injects.
Invoke-ScratchGit $primary @('config', 'user.name', 'fixture') | Out-Null
Invoke-ScratchGit $primary @('config', 'user.email', 'fixture@example.com') | Out-Null
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
$literalBracketPath = 'Engine/Data/Textures/Water/[BC4]FoamNoiseAbstract.png'

# Candidate construction is deliberately before verification. This isolated coverage
# exercises the Git boundary and its guarded rollbacks.
[IO.File]::WriteAllText((Join-Path $session 'candidate-session.txt'), 'session candidate', [Text.UTF8Encoding]::new($false))
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$sessionTip,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','*.txt','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'candidate-rejects-pathspec-owned-path' 1 'error' 'input.path-invalid'
$traversalSessionRefBefore = (@(Invoke-ScratchGit $session @('rev-parse',"refs/heads/$sessionBranch")))[0].Trim()
$traversalPrimaryRefBefore = (@(Invoke-ScratchGit $primary @('rev-parse','refs/heads/main')))[0].Trim()
$traversalSessionIndexBefore = (@(Invoke-ScratchGit $session @('ls-files','-s')) -join "`n")
$traversalPrimaryIndexBefore = (@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n")
$traversalSessionStatusBefore = (@(Invoke-ScratchGit $session @('status','--porcelain=v1','-z','--untracked-files=all')) -join "`n")
$traversalPrimaryStatusBefore = (@(Invoke-ScratchGit $primary @('status','--porcelain=v1','-z','--untracked-files=all')) -join "`n")
$traversalCandidateTextBefore = [IO.File]::ReadAllText((Join-Path $session 'candidate-session.txt'), [Text.UTF8Encoding]::new($false,$true))
$traversalPrimaryBaseTextBefore = [IO.File]::ReadAllText((Join-Path $primary 'base.txt'), [Text.UTF8Encoding]::new($false,$true))
$assertInvalidPathState = {
	param([string] $Case)
	Assert-True ($traversalSessionRefBefore -ceq ((@(Invoke-ScratchGit $session @('rev-parse',"refs/heads/$sessionBranch")))[0].Trim()) -and $traversalPrimaryRefBefore -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','refs/heads/main')))[0].Trim())) "$Case preserves both refs"
	Assert-True ($traversalSessionIndexBefore -ceq (@(Invoke-ScratchGit $session @('ls-files','-s')) -join "`n") -and $traversalPrimaryIndexBefore -ceq (@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n") -and $traversalSessionStatusBefore -ceq (@(Invoke-ScratchGit $session @('status','--porcelain=v1','-z','--untracked-files=all')) -join "`n") -and $traversalPrimaryStatusBefore -ceq (@(Invoke-ScratchGit $primary @('status','--porcelain=v1','-z','--untracked-files=all')) -join "`n")) "$Case preserves indexes and disjoint status"
	Assert-True ($traversalCandidateTextBefore -ceq [IO.File]::ReadAllText((Join-Path $session 'candidate-session.txt'), [Text.UTF8Encoding]::new($false,$true)) -and $traversalPrimaryBaseTextBefore -ceq [IO.File]::ReadAllText((Join-Path $primary 'base.txt'), [Text.UTF8Encoding]::new($false,$true))) "$Case preserves worktree bytes"
}
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$sessionTip,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','foo/..','-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'candidate-rejects-terminal-traversal-owned-path' 1 'error' 'input.path-invalid'
& $assertInvalidPathState 'terminal traversal rejection'
foreach ($dotPath in @('./file.txt', 'dir/./file.txt', 'dir/.')) {
	$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$sessionTip,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths',$dotPath,'-CommitMessageFile',$candidateMessage)
	Assert-Outcome $run "candidate-rejects-dot-owned-path-$dotPath" 1 'error' 'input.path-invalid'
	& $assertInvalidPathState "dot path '$dotPath' rejection"
}
$literalSessionDiskPath = Join-Path $session ($literalBracketPath.Replace('/','\'))
New-Item -ItemType Directory -Force (Split-Path -Parent $literalSessionDiskPath) | Out-Null
[IO.File]::WriteAllText($literalSessionDiskPath, 'literal session candidate', [Text.UTF8Encoding]::new($false))
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$sessionTip,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths',$literalBracketPath,'-CommitMessageFile',$candidateMessage)
Assert-Outcome $run 'session-candidate-accepts-bracketed-literal-path' 0 'pass' 'candidate.created'
if ($null -ne $run.Json) {
	Assert-True (@($run.Json.ownedPaths).Count -eq 1 -and @($run.Json.ownedPaths)[0] -ceq $literalBracketPath) 'session bracketed candidate owns exactly the literal path'
	$sessionChangedPaths = @(@(Invoke-ScratchGit $session @('diff-tree','--no-commit-id','--name-only','-r',$sessionTip,$run.Json.candidate.commit)) | ForEach-Object { $_.Trim() } | Where-Object { -not [string]::IsNullOrEmpty($_) })
	Assert-True ($sessionChangedPaths.Count -eq 1 -and $sessionChangedPaths[0] -ceq $literalBracketPath) 'session bracketed candidate changes exactly the literal path'
}
Invoke-ScratchGit $session @('reset','--hard',$sessionTip) | Out-Null
Remove-Item -LiteralPath (Join-Path $session 'Engine') -Recurse -Force -ErrorAction SilentlyContinue
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
$run = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$baseline,'-ExpectedCurrentTip',$baseline,'-ExpectedPrimaryTip',$baseline,'-OwnedPaths','rollback-active-owned.txt,rollback-staged-owned.txt','-CommitMessageFile',$candidateMessage,'-FixtureFailure','post-index-mutation')
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
$literalPrimaryDiskPath = Join-Path $primary ($literalBracketPath.Replace('/','\'))
New-Item -ItemType Directory -Force (Split-Path -Parent $literalPrimaryDiskPath) | Out-Null
[IO.File]::WriteAllText($literalPrimaryDiskPath, 'literal primary candidate', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'literal-disjoint-staged.txt'), 'staged disjoint', [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $primary @('add','literal-disjoint-staged.txt') | Out-Null
[IO.File]::WriteAllText((Join-Path $primary 'base.txt'), 'literal disjoint unstaged', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $primary 'literal-disjoint-untracked.txt'), 'untracked disjoint', [Text.UTF8Encoding]::new($false))
$literalPrimaryIndexBefore = (@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n")
$literalPrimaryStatusBefore = (@(Invoke-ScratchGit $primary @('status','--porcelain=v1','-z','--untracked-files=all')) -join "`n")
$literalPrimaryParameters = [ordered]@{ Route='primary-commit'; CurrentWorktree=$primary; PrimaryWorktree=$primary; CurrentBranch='main'; PrimaryBranch='main'; Baseline=$baseline; ExpectedCurrentTip=$baseline; ExpectedPrimaryTip=$baseline; OwnedPaths=@($literalBracketPath); CommitMessageFile=$candidateMessage }
$run = Invoke-JsonScriptWithSplat $candidateScript $literalPrimaryParameters $scratchBase
Assert-Outcome $run 'primary-candidate-accepts-bracketed-literal-path' 0 'pass' 'candidate.created'
if ($null -ne $run.Json) {
	Assert-True (@($run.Json.ownedPaths).Count -eq 1 -and @($run.Json.ownedPaths)[0] -ceq $literalBracketPath) 'primary bracketed candidate owns exactly the literal path'
	$primaryChangedPaths = @(@(Invoke-ScratchGit $primary @('diff-tree','--no-commit-id','--name-only','-r',$baseline,$run.Json.candidate.commit)) | ForEach-Object { $_.Trim() } | Where-Object { -not [string]::IsNullOrEmpty($_) })
	Assert-True ($primaryChangedPaths.Count -eq 1 -and $primaryChangedPaths[0] -ceq $literalBracketPath) 'primary bracketed candidate changes exactly the literal path'
	Assert-True ($literalPrimaryIndexBefore -ceq (@(Invoke-ScratchGit $primary @('ls-files','-s')) -join "`n") -and $literalPrimaryStatusBefore -ceq (@(Invoke-ScratchGit $primary @('status','--porcelain=v1','-z','--untracked-files=all')) -join "`n")) 'primary bracketed candidate preserves real index and disjoint staged unstaged untracked state'
}
Invoke-ScratchGit $primary @('reset','--hard',$baseline) | Out-Null
Invoke-ScratchGit $primary @('clean','-fd') | Out-Null
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
	Assert-ExactProperties $landingBoundedFailure.Json @('schemaVersion','status','code','message','messageLength','messageTruncated','primaryAdvanced','candidate','landed','planClaim','lock','cleanup','disposition','requiresUserAuthority','retryAfterMilliseconds','diagnostics','residuals') 'landing failure'
	Assert-ExactProperties $landingBoundedFailure.Json.diagnostics @('totalCount','items','truncated','selector','requery') 'landing failure diagnostics'
	Assert-ExactProperties $landingBoundedFailure.Json.diagnostics.items[0] @('source','code','codeLength','codeTruncated','path','pathLength','pathTruncated','message','messageLength','messageTruncated') 'landing failure diagnostic item'
	Assert-True ($landingBoundedFailure.Json.code.Length -eq 128 -and $landingBoundedFailure.Json.message.Length -eq 512 -and $landingBoundedFailure.Json.messageLength -eq 600 -and $landingBoundedFailure.Json.messageTruncated) 'landing failure top-level text is bounded'
	# A forced compare-and-swap failure is the one stale-primary signal this scenario can raise: primary
	# never moved, so the internal rebase is a no-op and the second forced failure exhausts the single
	# retry instead of reporting the lost swap.
	$landingParameters.FixtureFailure = 'compare-and-swap'
	$landingCasMismatch = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $landingCasMismatch 'landing-retry-exhausted' 2 'blocked' 'landing.retry-exhausted'
	Assert-True ($landingCasMismatch.Json.disposition -ceq 'retryable-wait' -and $landingCasMismatch.Json.retryAfterMilliseconds -eq 500 -and -not $landingCasMismatch.Json.primaryAdvanced) 'landing retry exhaustion is retryable and advanced nothing'
	Assert-True ($landingCasMismatch.Json.landed.rebaseAttempts -eq 1 -and $null -eq $landingCasMismatch.Json.landed.commit) 'landing retry exhaustion reports its one rebase and lands nothing'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'landing retry exhaustion leaves primary ref unchanged'
	Assert-True ($run.Json.candidate.commit -ceq ((@(Invoke-ScratchGit $session @('rev-parse',"refs/heads/$sessionBranch")))[0].Trim())) 'landing retry exhaustion leaves the confirmed session commit in place'
	Assert-True ([string]::IsNullOrWhiteSpace((@(Invoke-ScratchGit $primary @('status','--porcelain=v1','-z','--untracked-files=all')) -join ''))) 'landing retry exhaustion preserves primary checkout'
	$landingParameters.FixtureFailure = 'post-reset'
	$landingRollback = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $landingRollback 'landing-post-reset-rollback' 2 'blocked' 'candidate.postcondition-failed'
	Assert-True ($baseline -ceq ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim())) 'landing post-reset rollback restores primary ref and checkout head'
	Assert-True ([string]::IsNullOrWhiteSpace((@(Invoke-ScratchGit $primary @('status','--porcelain=v1','-z','--untracked-files=all')) -join ''))) 'landing post-reset rollback restores primary index and worktree'
	$landingParameters.FixtureFailure = 'none'
	$landing = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $landing 'exact-candidate-landing-success' 0 'landed' 'ok'
	Assert-ExactProperties $landing.Json @('schemaVersion','status','code','message','messageLength','messageTruncated','primaryAdvanced','candidate','landed','planClaim','lock','cleanup','disposition','requiresUserAuthority','retryAfterMilliseconds','diagnostics','residuals') 'landing success'
	Assert-ExactProperties $landing.Json.candidate @('commit','tree','treeVerified') 'landing candidate'
	Assert-ExactProperties $landing.Json.landed @('commit','tree','rebaseAttempts') 'landing landed'
	Assert-ExactProperties $landing.Json.planClaim @('requested','released') 'landing Plan claim'
	Assert-ExactProperties $landing.Json.lock @('claimed','released','claimCode','disposition','requiresUserAuthority','retryAfterMilliseconds','attempts') 'landing lock'
	Assert-ExactProperties $landing.Json.cleanup @('worktreesClear','problems') 'landing cleanup'
	Assert-ExactProperties $landing.Json.cleanup.problems @('totalCount','items','truncated','selector','requery') 'landing cleanup problems'
	Assert-ExactProperties $landing.Json.residuals @('totalCount','items','truncated','selector','requery') 'landing residuals'
	Assert-True ($landing.Json.schemaVersion -ceq 'broken-engine-finalize-landing/v3' -and $landing.Json.candidate.commit -ceq $run.Json.candidate.commit -and $landing.Json.candidate.tree -ceq $run.Json.candidate.tree -and $landing.Json.candidate.treeVerified -and $landing.Json.lock.claimed -and $landing.Json.lock.released -and $landing.Json.cleanup.worktreesClear) 'landing success projects exact candidate, lock, and cleanup proof'
	Assert-True ($landing.Json.landed.commit -ceq $run.Json.candidate.commit -and $landing.Json.landed.tree -ceq $run.Json.candidate.tree -and $landing.Json.landed.rebaseAttempts -eq 0) 'a mint-fresh landing lands the confirmed candidate with no rebase'
	Assert-True (-not $landing.Json.planClaim.requested -and -not $landing.Json.planClaim.released) 'a claim-free landing touches no Plan claim'
	Assert-True ($landing.Json.PSObject.Properties.Name -cnotcontains 'identities' -and $landing.Json.PSObject.Properties.Name -cnotcontains 'tips' -and $landing.Json.PSObject.Properties.Name -cnotcontains 'locks' -and $landing.Json.PSObject.Properties.Name -cnotcontains 'blocker') 'landing hides checkout, lock-owner, and raw blocker objects'
	Assert-True ($landing.Json.primaryAdvanced -and ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()) -ceq $run.Json.candidate.commit) 'landing primary ref equals the reviewed candidate commit exactly'
	$landingParameters.ExpectedPrimaryTip = $run.Json.candidate.commit
	$recoveryOwner = [guid]::NewGuid().ToString()
	Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $recoveryOwner, '--session', 'finalize-fixture/landing', '--worktree', $session, '--lease-seconds', '3600') | Out-Null
	$recovery = Invoke-JsonScriptWithSplat $landingScript $landingParameters $scratchBase
	Assert-Outcome $recovery 'exact-candidate-post-advance-recovery' 0 'landed' 'ok'
	if ($null -ne $recovery.Json) {
		Assert-True ($recovery.Json.lock.claimed -and $recovery.Json.lock.released) 'omitted-token recovery adopts and releases the matching retained claim'
	}
}

# Lease continuity and the single internal rebase-and-retry. Every scenario below changes the last
# line of one twenty-line tracked file, so an upstream commit can touch a distant line of the same
# file and produce a clean rebase, or the same line and produce a conflict.
function New-RetryFileText([string] $First, [string] $Last) {
	return ((@($First) + @(2..19 | ForEach-Object { "line $_" }) + @($Last)) -join "`n") + "`n"
}
function New-RetryCandidate([string] $Text, [string] $Case) {
	$tip = (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
	Invoke-ScratchGit $session @('reset','--hard',$tip) | Out-Null
	Invoke-ScratchGit $session @('clean','-fd') | Out-Null
	[IO.File]::WriteAllText((Join-Path $session 'retry-file.txt'), $Text, [Text.UTF8Encoding]::new($false))
	$candidate = Invoke-JsonScript $candidateScript @('-Route','session-landing','-CurrentWorktree',$session,'-PrimaryWorktree',$primary,'-CurrentBranch',$sessionBranch,'-PrimaryBranch','main','-Baseline',$tip,'-ExpectedCurrentTip',$tip,'-ExpectedPrimaryTip',$tip,'-OwnedPaths','retry-file.txt','-CommitMessageFile',$candidateMessage)
	Assert-Outcome $candidate "$Case-candidate" 0 'pass' 'candidate.created'
	return [pscustomobject]@{ PrimaryTip = $tip; Commit = $candidate.Json.candidate.commit; Tree = $candidate.Json.candidate.tree }
}
function New-RetryLandingParameters($Candidate) {
	return [ordered]@{ CurrentWorktree=$session; PrimaryWorktree=$primary; CurrentBranch=$sessionBranch; PrimaryBranch='main'; ExpectedCurrentTip=$Candidate.Commit; ExpectedPrimaryTip=$Candidate.PrimaryTip; SessionLabel='finalize-fixture'; ApprovedSessionCommit=$Candidate.Commit; ApprovedCandidateTree=$Candidate.Tree }
}
function Add-UpstreamPrimaryCommit([string] $Text) {
	[IO.File]::WriteAllText((Join-Path $primary 'retry-file.txt'), $Text, [Text.UTF8Encoding]::new($false))
	Invoke-ScratchGit $primary @('add','retry-file.txt') | Out-Null
	Invoke-ScratchGit $primary @('commit','-m','upstream change') | Out-Null
	return (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
}
function Test-SessionRebaseMarkersAbsent {
	foreach ($marker in @('rebase-merge','rebase-apply')) {
		if (Test-Path -LiteralPath (@(Invoke-ScratchGit $session @('rev-parse','--path-format=absolute','--git-path',$marker)))[0].Trim()) { return $false }
	}
	return $true
}

$retryHead = 'line 1'
$retryTail = 'line 20'
[IO.File]::WriteAllText((Join-Path $primary 'retry-file.txt'), (New-RetryFileText $retryHead $retryTail), [Text.UTF8Encoding]::new($false))
Invoke-ScratchGit $primary @('add','retry-file.txt') | Out-Null
Invoke-ScratchGit $primary @('commit','-m','retry fixture base') | Out-Null

$retryTail = 'continuity tail'
$continuityCandidate = New-RetryCandidate (New-RetryFileText $retryHead $retryTail) 'lease-continuity'
$continuityOwner = [guid]::NewGuid().ToString()
$continuityClaim = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $continuityOwner, '-LeaseSeconds', '3600'))
Assert-Outcome $continuityClaim 'lease-continuity-claim' 0 'pass' 'ok'
$continuityParameters = New-RetryLandingParameters $continuityCandidate
$continuityParameters.OwnerToken = $continuityOwner
$continuity = Invoke-JsonScriptWithSplat $landingScript $continuityParameters $scratchBase
Assert-Outcome $continuity 'landing-continues-caller-lease' 0 'landed' 'ok'
if ($null -ne $continuity.Json) {
	Assert-True ($continuity.Json.lock.claimed -and $continuity.Json.lock.claimCode -ceq 'ok' -and $continuity.Json.lock.attempts -eq 1 -and $continuity.Json.lock.released) 'landing continues under the caller lease without a fresh claim round'
	Assert-True ($continuity.Json.landed.commit -ceq $continuityCandidate.Commit -and $continuity.Json.landed.rebaseAttempts -eq 0) 'a continued lease lands the confirmed candidate with no rebase'
	Assert-True ($continuity.Json.primaryAdvanced -and ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()) -ceq $continuityCandidate.Commit) 'a continued lease advances primary to the confirmed candidate'
}

# A continued lease must be able to outlast the advance, and WorktreeCli's refresh keeps a lease's
# own duration, so a token minted with a shorter lease is refused instead of continued.
$shortLeaseCandidate = New-RetryCandidate (New-RetryFileText $retryHead 'short lease tail') 'short-lease'
$shortLeaseOwner = [guid]::NewGuid().ToString()
$shortLeaseClaim = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $shortLeaseOwner, '-LeaseSeconds', '60'))
Assert-Outcome $shortLeaseClaim 'short-lease-continuity-claim' 0 'pass' 'ok'
$shortLeasePrimaryBefore = (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
$shortLeaseParameters = New-RetryLandingParameters $shortLeaseCandidate
$shortLeaseParameters.OwnerToken = $shortLeaseOwner
$shortLease = Invoke-JsonScriptWithSplat $landingScript $shortLeaseParameters $scratchBase
Assert-Outcome $shortLease 'landing-refuses-short-caller-lease' 2 'blocked' 'landing-lock.claim-failed'
if ($null -ne $shortLease.Json) {
	Assert-True (-not $shortLease.Json.lock.claimed -and $shortLease.Json.lock.claimCode -ceq 'landing-lock.retryable-wait' -and $shortLease.Json.disposition -ceq 'retryable-wait') 'a caller lease shorter than the landing lease is refused as retryable contention'
	Assert-True (-not $shortLease.Json.primaryAdvanced -and ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()) -ceq $shortLeasePrimaryBefore) 'a refused short lease leaves primary unchanged'
}
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $shortLeaseOwner) | Out-Null

# Independent signal: the same supplied token, live, but recorded against another worktree stays
# foreign contention, so the landing refuses instead of continuing or minting behind the caller.
$foreignCandidate = New-RetryCandidate (New-RetryFileText $retryHead 'foreign lease tail') 'foreign-lease'
$foreignOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $foreignOwner, '--session', 'finalize-fixture', '--worktree', $primary, '--lease-seconds', '600') | Out-Null
$foreignPrimaryBefore = (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
$foreignParameters = New-RetryLandingParameters $foreignCandidate
$foreignParameters.OwnerToken = $foreignOwner
$foreign = Invoke-JsonScriptWithSplat $landingScript $foreignParameters $scratchBase
Assert-Outcome $foreign 'landing-refuses-foreign-lease' 2 'blocked' 'landing-lock.claim-failed'
if ($null -ne $foreign.Json) {
	Assert-True (-not $foreign.Json.lock.claimed -and $foreign.Json.lock.claimCode -ceq 'landing-lock.retryable-wait' -and $foreign.Json.disposition -ceq 'retryable-wait') 'a lease recorded for another worktree is refused as retryable contention'
	Assert-True (-not $foreign.Json.primaryAdvanced -and ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()) -ceq $foreignPrimaryBefore) 'a refused foreign lease leaves primary unchanged'
}
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $foreignOwner) | Out-Null

# An omitted-token landing adopts only a retained claim under its derived identity. Foreign live
# leases are observed in a bounded child process so the production 300-second wait is not shortened;
# the child is reaped before the fixture releases the foreign owner. Unverifiable metadata must fail
# immediately with authority required and remain untouched.
function Assert-OmittedForeignLease([string] $Case, [string] $LeaseSession, [string] $LeaseWorktree, [bool] $Unverifiable = $false, [int] $LeaseSeconds = 600) {
	$candidate = New-RetryCandidate (New-RetryFileText $retryHead "$Case tail") $Case
	$owner = [guid]::NewGuid().ToString()
	Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $owner, '--session', $LeaseSession, '--worktree', $LeaseWorktree, '--lease-seconds', [string]$LeaseSeconds) | Out-Null
	if ($Unverifiable) { Set-UnverifiableLandingLease $localAppData $owner }
	$primaryBefore = (@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()
	$parameters = New-RetryLandingParameters $candidate
	if ($Unverifiable) {
		$run = Invoke-JsonScriptWithSplat $landingScript $parameters $scratchBase
		Assert-Outcome $run "$Case-omitted-token" 2 'blocked' 'landing-lock.claim-failed'
		if ($null -ne $run.Json) {
			Assert-True ($run.Json.disposition -ceq 'authority-required' -and $run.Json.requiresUserAuthority -and -not $run.Json.primaryAdvanced) "$Case unverifiable lease requires authority and leaves primary unchanged"
		}
		$status = ((@(Invoke-WorktreeCli @('lock', 'status', '--repo', $commonDirectory)) -join '') | ConvertFrom-Json -Depth 16)
		Assert-True ($status.held -eq $true -and $status.leaseState -ceq 'unverifiable') "$Case unverifiable lease remains untouched"
	}
	else {
		$started = Start-JsonScriptWithSplat $landingScript $parameters $scratchBase
		try {
			Start-Sleep -Seconds 2
			Assert-True (-not $started.Process.HasExited) "$Case foreign lease remains a bounded wait"
			Assert-True (((@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()) -ceq $primaryBefore) "$Case foreign lease leaves primary unchanged"
			$status = ((@(Invoke-WorktreeCli @('lock', 'status', '--repo', $commonDirectory)) -join '') | ConvertFrom-Json -Depth 16)
			Assert-True ($status.held -eq $true -and $status.owner -ceq $owner -and $status.leaseDurationSeconds -eq $LeaseSeconds) "$Case foreign lease is not adopted and keeps its recorded duration"
		}
		finally { Stop-JsonScript $started }
	}
	Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $owner) | Out-Null
}

# A matching retained lease with only 60 seconds recorded is not adopted by an omitted-token
# landing: refresh preserves that duration, so continuing could expire during rebase or advance.
Assert-OmittedForeignLease 'omitted-short-derived-lease' 'finalize-fixture/landing' $session $false 60

$omittedCandidate = New-RetryCandidate (New-RetryFileText $retryHead 'omitted adoption tail') 'omitted-adoption'
$omittedOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $omittedOwner, '--session', 'finalize-fixture/landing', '--worktree', $session, '--lease-seconds', '3600') | Out-Null
$omitted = Invoke-JsonScriptWithSplat $landingScript (New-RetryLandingParameters $omittedCandidate) $scratchBase
Assert-Outcome $omitted 'landing-adopts-omitted-token-retained-claim' 0 'landed' 'ok'
if ($null -ne $omitted.Json) {
	Assert-True ($omitted.Json.lock.claimed -and $omitted.Json.lock.claimCode -ceq 'ok' -and $omitted.Json.lock.released) 'omitted-token landing adopts and releases the derived retained claim'
	Assert-True ($omitted.Json.primaryAdvanced -and ((@(Invoke-ScratchGit $primary @('rev-parse', 'HEAD')))[0].Trim()) -ceq $omittedCandidate.Commit) 'omitted-token adoption lands the confirmed candidate'
}

# A matching retained lease whose full 3600-second duration is almost spent must be refreshed
# before a primary race sends landing through its rebase. Poll the scratch metadata while the child
# is active and require the approved session tip when the extension is observed, so the fixture
# proves the refresh happened before the rebase rather than only during its later retry.
$nearExpiryCandidate = New-RetryCandidate (New-RetryFileText $retryHead 'omitted near-expiry tail') 'omitted-near-expiry'
$nearExpiryUpstream = Add-UpstreamPrimaryCommit (New-RetryFileText 'omitted near-expiry upstream' 'omitted adoption tail')
$nearExpiryOwner = [guid]::NewGuid().ToString()
Invoke-WorktreeCli @('lock', 'claim', '--repo', $commonDirectory, '--owner', $nearExpiryOwner, '--session', 'finalize-fixture/landing', '--worktree', $session, '--lease-seconds', '3600') | Out-Null
$nearExpiryLease = Set-NearExpiryLandingLease $localAppData $nearExpiryOwner 30000
$nearExpiryStarted = Start-JsonScriptWithSplat $landingScript (New-RetryLandingParameters $nearExpiryCandidate) $scratchBase
$nearExpiryRefreshed = $false
$nearExpiryCompleted = $false
$nearExpiryExitCode = $null
try {
	$nearExpiryDeadline = [DateTime]::UtcNow.AddSeconds(10)
	while ([DateTime]::UtcNow -lt $nearExpiryDeadline) {
		if (Test-Path -LiteralPath $nearExpiryLease.Path) {
			try {
				$metadata = Get-Content -LiteralPath $nearExpiryLease.Path -Raw | ConvertFrom-Json -Depth 16 -ErrorAction Stop
				$sessionTip = (@(Invoke-ScratchGit $session @('rev-parse', "refs/heads/$sessionBranch")))[0].Trim()
				if ($metadata.owner -ceq $nearExpiryOwner -and $sessionTip -ceq $nearExpiryCandidate.Commit -and ([DateTimeOffset]::Parse($metadata.expiresAt, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AssumeUniversal) -gt $nearExpiryLease.ExpiresAt.AddSeconds(60))) {
					$nearExpiryRefreshed = $true
				}
			}
			catch { }
		}
		if ($nearExpiryStarted.Process.WaitForExit(0)) {
			$nearExpiryCompleted = $true
			break
		}
		Start-Sleep -Milliseconds 25
	}
	if (-not $nearExpiryCompleted -and $nearExpiryStarted.Process.WaitForExit(0)) { $nearExpiryCompleted = $true }
	if ($nearExpiryCompleted) {
		$nearExpiryExitCode = $nearExpiryStarted.Process.ExitCode
	}
}
finally { Stop-JsonScript $nearExpiryStarted }
Assert-True $nearExpiryRefreshed 'omitted-token adoption refreshes a near-expiry matching lease before rebase work'
$nearExpiryHead = (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
$nearExpiryParent = (@(Invoke-ScratchGit $primary @('rev-parse', "$nearExpiryHead^")))[0].Trim()
Assert-True ($nearExpiryCompleted -and $nearExpiryExitCode -eq 0 -and $nearExpiryHead -cne $nearExpiryCandidate.Commit -and $nearExpiryParent -ceq $nearExpiryUpstream) 'near-expiry omitted-token adoption lands after the primary race'
$retryHead = 'omitted near-expiry upstream'
$retryTail = 'omitted near-expiry tail'
try { Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $nearExpiryOwner) | Out-Null } catch { }

Assert-OmittedForeignLease 'omitted-raw-session' 'finalize-fixture' $session
Assert-OmittedForeignLease 'omitted-foreign-session' 'foreign-fixture' $session
Assert-OmittedForeignLease 'omitted-foreign-worktree' 'finalize-fixture/landing' $primary
Assert-OmittedForeignLease 'omitted-unverifiable' 'finalize-fixture/landing' $session $true

$identicalTail = 'identical retry tail'
$identicalCandidate = New-RetryCandidate (New-RetryFileText $retryHead $identicalTail) 'identical-retry'
$retryHead = 'upstream identical head'
$identicalUpstream = Add-UpstreamPrimaryCommit (New-RetryFileText $retryHead $retryTail)
$identicalRetry = Invoke-JsonScriptWithSplat $landingScript (New-RetryLandingParameters $identicalCandidate) $scratchBase
Assert-Outcome $identicalRetry 'landing-retries-identical-patch' 0 'landed' 'ok'
$retryTail = $identicalTail
if ($null -ne $identicalRetry.Json) {
	$rebasedTip = (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
	Assert-True ($identicalRetry.Json.landed.rebaseAttempts -eq 1 -and $identicalRetry.Json.landed.commit -cne $identicalCandidate.Commit -and $identicalRetry.Json.landed.commit -ceq $rebasedTip) 'one internal rebase lands the rebased commit instead of the stale candidate'
	Assert-True ($identicalRetry.Json.landed.tree -ceq ((@(Invoke-ScratchGit $primary @('rev-parse',"$rebasedTip^{tree}")))[0].Trim())) 'the landed tree is the rebased commit tree'
	Assert-True (((@(Invoke-ScratchGit $primary @('rev-parse',"$rebasedTip^")))[0].Trim()) -ceq $identicalUpstream) 'the rebased tip descends from the upstream commit that advanced primary'
	Assert-True (([IO.File]::ReadAllText((Join-Path $primary 'retry-file.txt'), [Text.UTF8Encoding]::new($false,$true))) -ceq (New-RetryFileText $retryHead $retryTail)) 'the landed primary content carries both the upstream and the confirmed change'
	Assert-True ([string]::IsNullOrWhiteSpace((@(Invoke-ScratchGit $session @('status','--porcelain=v1','-z','--untracked-files=all')) -join '')) -and (Test-SessionRebaseMarkersAbsent)) 'the internal rebase leaves the session worktree clean with no rebase markers'
}

# A crash after an internally rebased advance is rerun with the same original approved inputs: the
# landing must recognize its own rebased commit on primary and report the landed outcome again.
$rebasedRecoveryTip = (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
$rebasedRecovery = Invoke-JsonScriptWithSplat $landingScript (New-RetryLandingParameters $identicalCandidate) $scratchBase
Assert-Outcome $rebasedRecovery 'landing-recovers-internally-rebased-advance' 0 'landed' 'ok'
if ($null -ne $rebasedRecovery.Json) {
	Assert-True ($rebasedRecovery.Json.landed.commit -ceq $rebasedRecoveryTip -and $rebasedRecovery.Json.landed.rebaseAttempts -eq 1 -and $rebasedRecovery.Json.candidate.treeVerified) 'rebased-advance recovery reports the rebased primary tip as landed with its one rebase'
	Assert-True ($rebasedRecovery.Json.primaryAdvanced -and ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()) -ceq $rebasedRecoveryTip) 'rebased-advance recovery leaves primary unchanged'
}

# The crashed invocation's own landing lease is still live when the recovery rerun succeeds, so that
# rerun must release it instead of leaving every other session waiting out the full lease.
$recoveryLeaseOwner = [guid]::NewGuid().ToString()
$recoveryLeaseClaim = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $recoveryLeaseOwner, '-LeaseSeconds', '3600'))
Assert-Outcome $recoveryLeaseClaim 'recovery-lease-claim' 0 'pass' 'ok'
$recoveryLeaseParameters = New-RetryLandingParameters $identicalCandidate
$recoveryLeaseParameters.OwnerToken = $recoveryLeaseOwner
$recoveryLease = Invoke-JsonScriptWithSplat $landingScript $recoveryLeaseParameters $scratchBase
Assert-Outcome $recoveryLease 'landing-recovery-releases-caller-lease' 0 'landed' 'ok'
if ($null -ne $recoveryLease.Json) {
	Assert-True ($recoveryLease.Json.landed.commit -ceq $rebasedRecoveryTip -and $recoveryLease.Json.primaryAdvanced) 'recovery under the caller lease reports the already-landed tip'
	Assert-True ($recoveryLease.Json.lock.claimed -and $recoveryLease.Json.lock.released) 'recovery under the caller lease releases that same-actor lease'
}
$recoveryReclaimOwner = [guid]::NewGuid().ToString()
$recoveryReclaim = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $recoveryReclaimOwner, '-LeaseSeconds', '60', '-WaitSeconds', '1', '-PollMilliseconds', '50'))
Assert-Outcome $recoveryReclaim 'landing-lock-free-after-recovery' 0 'pass' 'ok'
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $recoveryReclaimOwner) | Out-Null

$mismatchCandidate = New-RetryCandidate (New-RetryFileText $retryHead 'mismatch tail') 'patch-mismatch'
$retryHead = 'upstream mismatch head'
[void] (Add-UpstreamPrimaryCommit (New-RetryFileText $retryHead $retryTail))
$mismatchPrimaryBefore = (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
$mismatchParameters = New-RetryLandingParameters $mismatchCandidate
$mismatchParameters.FixtureFailure = 'retry-patch-mismatch'
$mismatch = Invoke-JsonScriptWithSplat $landingScript $mismatchParameters $scratchBase
Assert-Outcome $mismatch 'landing-aborts-non-identical-rebase' 2 'blocked' 'rebase.patch-not-identical'
if ($null -ne $mismatch.Json) {
	Assert-True (-not $mismatch.Json.primaryAdvanced -and ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()) -ceq $mismatchPrimaryBefore) 'a non-identical rebase leaves the primary ref byte-identical'
	Assert-True ($mismatch.Json.landed.rebaseAttempts -eq 1 -and $null -eq $mismatch.Json.landed.commit) 'a non-identical rebase reports its attempt and lands nothing'
	Assert-True (((@(Invoke-ScratchGit $session @('rev-parse',"refs/heads/$sessionBranch")))[0].Trim()) -ceq $mismatchCandidate.Commit -and ((@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()) -ceq $mismatchCandidate.Commit) 'a non-identical rebase restores the confirmed session commit'
}

# A real rebase followed by a lost compare-and-swap exhausts the single retry with the session branch
# already moved, so the rerun the retryable result documents needs the confirmed commit restored.
$exhaustedCandidate = New-RetryCandidate (New-RetryFileText $retryHead 'retry exhausted tail') 'retry-exhausted'
$retryHead = 'upstream exhausted head'
[void] (Add-UpstreamPrimaryCommit (New-RetryFileText $retryHead $retryTail))
$exhaustedPrimaryBefore = (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
$exhaustedParameters = New-RetryLandingParameters $exhaustedCandidate
$exhaustedParameters.FixtureFailure = 'compare-and-swap'
$exhausted = Invoke-JsonScriptWithSplat $landingScript $exhaustedParameters $scratchBase
Assert-Outcome $exhausted 'landing-restores-branch-on-retry-exhaustion' 2 'blocked' 'landing.retry-exhausted'
if ($null -ne $exhausted.Json) {
	Assert-True ($exhausted.Json.disposition -ceq 'retryable-wait' -and $exhausted.Json.landed.rebaseAttempts -eq 1 -and -not $exhausted.Json.primaryAdvanced) 'a real rebase that then loses the advance stays retryable after its one attempt'
	Assert-True (((@(Invoke-ScratchGit $session @('rev-parse',"refs/heads/$sessionBranch")))[0].Trim()) -ceq $exhaustedCandidate.Commit -and ((@(Invoke-ScratchGit $session @('rev-parse','HEAD')))[0].Trim()) -ceq $exhaustedCandidate.Commit) 'retry exhaustion after a real rebase restores the confirmed session commit'
	Assert-True (((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()) -ceq $exhaustedPrimaryBefore -and (Test-SessionRebaseMarkersAbsent)) 'retry exhaustion after a real rebase leaves primary unchanged with no rebase markers'
}

$conflictCandidate = New-RetryCandidate (New-RetryFileText $retryHead 'conflict session tail') 'rebase-conflict'
$retryTail = 'conflict upstream tail'
[void] (Add-UpstreamPrimaryCommit (New-RetryFileText $retryHead $retryTail))
$conflictPrimaryBefore = (@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()
$conflict = Invoke-JsonScriptWithSplat $landingScript (New-RetryLandingParameters $conflictCandidate) $scratchBase
Assert-Outcome $conflict 'landing-aborts-conflicting-rebase' 2 'blocked' 'rebase.conflicted'
if ($null -ne $conflict.Json) {
	Assert-True ($conflict.Json.cleanup.worktreesClear -eq $true -and (Test-SessionRebaseMarkersAbsent)) 'a conflicting rebase is aborted and leaves no active Git markers'
	Assert-True ($conflict.Json.lock.released -and -not $conflict.Json.primaryAdvanced -and ((@(Invoke-ScratchGit $primary @('rev-parse','HEAD')))[0].Trim()) -ceq $conflictPrimaryBefore) 'a conflicting rebase releases the lease and leaves primary unchanged'
	Assert-True (((@(Invoke-ScratchGit $session @('rev-parse',"refs/heads/$sessionBranch")))[0].Trim()) -ceq $conflictCandidate.Commit) 'a conflicting rebase restores the confirmed session commit'
}
$reclaimOwner = [guid]::NewGuid().ToString()
$reclaim = Invoke-JsonScript $lockClaimScript (@('-WorktreeCliExecutable', (Join-Path $primaryOutput 'WorktreeCli.exe'), '-GitCommonDirectory', $commonDirectory, '-SessionLabel', 'finalize-fixture', '-Worktree', $session, '-LandingOwner', $reclaimOwner, '-LeaseSeconds', '60', '-WaitSeconds', '1', '-PollMilliseconds', '50'))
Assert-Outcome $reclaim 'landing-lock-free-after-conflict-abort' 0 'pass' 'ok'
Invoke-WorktreeCli @('lock', 'release', '--repo', $commonDirectory, '--owner', $reclaimOwner) | Out-Null

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
