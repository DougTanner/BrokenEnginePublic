[CmdletBinding()]
param(
	[switch] $CompletedPlanReceiptOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$executor = Join-Path $PSScriptRoot 'Invoke-FinalizeLanding.ps1'
$writer = Join-Path $PSScriptRoot '..\..\..\scripts\Write-AgentVerificationReport.ps1'
$reportAllocator = Join-Path $PSScriptRoot '..\..\..\scripts\New-AgentReportPath.ps1'
$fixtureRoot = $null
$fixturePrimary = $null
$fixtureSourcePrimary = $null
$fixtureLocalAppData = $null
$fixtureBranchNames = [Collections.Generic.List[string]]::new()
$fixtureSessions = [Collections.Generic.List[object]]::new()
$environmentBackup = @{}

function Assert-Condition([bool] $Condition, [string] $Message) {
	if (-not $Condition) { throw $Message }
}

function Invoke-Process([string] $FilePath, [string[]] $Arguments, [string] $WorkingDirectory) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $FilePath
	$start.WorkingDirectory = $WorkingDirectory
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	foreach ($argument in $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start '$FilePath'." }
	$stdout = $process.StandardOutput.ReadToEndAsync()
	$stderr = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$result = [pscustomobject]@{ ExitCode = $process.ExitCode; Stdout = $stdout.GetAwaiter().GetResult(); Stderr = $stderr.GetAwaiter().GetResult() }
	$process.Dispose()
	return $result
}

function Invoke-Git([string] $WorkingDirectory, [string[]] $Arguments, [int] $ExpectedExitCode = 0) {
	$result = Invoke-Process 'git.exe' $Arguments $WorkingDirectory
	if ($result.ExitCode -ne $ExpectedExitCode) { throw "git $($Arguments -join ' ') exited $($result.ExitCode): $($result.Stdout)$($result.Stderr)" }
	return $result.Stdout.Trim()
}

function Get-AgentJson($Response, [string] $Label) {
	if ([string]::IsNullOrWhiteSpace($Response.Stdout)) { throw "$Label returned no JSON: $($Response.Stderr)" }
	try { return $Response.Stdout.Trim() | ConvertFrom-Json -Depth 32 -ErrorAction Stop }
	catch { throw "$Label returned invalid JSON: $($Response.Stdout)" }
}

function Set-SessionEnvironment($Session) {
	$env:BROKEN_ENGINE_WORKTREE_PATH = $Session.Path
	$env:BROKEN_ENGINE_SESSION_BRANCH = $Session.Branch
	$env:BROKEN_ENGINE_PRIMARY_CHECKOUT = $fixturePrimary
	$env:BROKEN_ENGINE_TARGET_BRANCH = $script:fixturePrimaryBranch
	$env:BROKEN_ENGINE_BASELINE = $script:fixtureBaseline
	$env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER = $Session.Owner
}

function Add-FixtureSession([string] $Name) {
	$branch = 'fixture/finalize-landing-' + $Name + '-' + [guid]::NewGuid().ToString('N')
	$path = Join-Path $fixtureRoot $Name
	$base = Invoke-Git $fixturePrimary @('rev-parse', 'HEAD')
	Invoke-Git $fixturePrimary @('worktree', 'add', '-b', $branch, $path, $base) | Out-Null
	$fixtureBranchNames.Add($branch)
	$sessionOutput = Join-Path $path 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	New-Item -ItemType Directory -Path (Split-Path -Parent $sessionOutput) -Force | Out-Null
	New-Item -ItemType SymbolicLink -Path $sessionOutput -Target $script:fixturePrimaryOutput | Out-Null
	$owner = [guid]::NewGuid().ToString()
	$module = Join-Path $path '.agents\scripts\WorktreeCliSessionExclusion.psm1'
	Import-Module $module -Force
	Register-WorktreeCliSession -RepositoryRoot $fixturePrimary -Owner $owner -Label 'finalize landing fixture' -Worktree $path -LegacySessionsClosed | Out-Null
	$session = [pscustomobject]@{ Name = $Name; Path = $path; Branch = $branch; Owner = $owner; Module = $module }
	$fixtureSessions.Add($session)
	return $session
}

function Get-FixtureExecutablePlan($Session) {
	$response = Invoke-Process $script:fixtureWorktreeCli @('plan', 'order', 'validate', '--repo', $script:fixtureCommon, '--worktree', $Session.Path) $Session.Path
	Assert-Condition ($response.ExitCode -eq 0) "Could not validate fixture plan queue: $($response.Stdout)$($response.Stderr)"
	$validation = Get-AgentJson $response 'fixture plan-order validate'
	$plans = @($validation.rows | Where-Object { $_.queue -ceq 'Documents/Plans/Order.md' })
	Assert-Condition ($plans.Count -gt 0) 'Fixture repository has no executable Plans row.'
	return [string] $plans[0].plan
}

function Add-SessionChange($Session, [string] $Name) {
	$relativePlan = Get-FixtureExecutablePlan $Session
	$path = Join-Path $Session.Path ($relativePlan.Replace('/', [IO.Path]::DirectorySeparatorChar))
	[IO.File]::AppendAllText($path, "`n<!-- fixture $Name -->`n", [Text.UTF8Encoding]::new($false))
	Invoke-Git $Session.Path @('add', '--', $relativePlan) | Out-Null
	Invoke-Git $Session.Path @('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', "fixture $Name") | Out-Null
	return [pscustomobject]@{ Commit = Invoke-Git $Session.Path @('rev-parse', 'HEAD'); Plan = $relativePlan.Substring('Documents/Plans/'.Length) }
}

function Write-Report($Session, [string] $Name, [string] $ComparisonBase) {
	$report = (& pwsh -NoProfile -File $reportAllocator -Worktree $Session.Path -Purpose $Name).Trim()
	$output = & pwsh -NoProfile -File $writer `
		-ReportPath $report `
		-Worktree $Session.Path `
		-Baseline $script:fixtureBaseline `
		-ManifestComparisonBase $ComparisonBase `
		-PlanIntent "landing fixture $Name" `
		-AcceptanceLedgerLine '- transaction fixture | disposable Git repository | PASS | report writer output is hash-bound' `
		-QueueReceiptOrResidualLine 'none' 2>$null
	if ($LASTEXITCODE -ne 0) { throw "Writer failed for ${Name}: $output" }
	$result = $output | ConvertFrom-Json
	Assert-Condition ($result.status -ceq 'pass') "Writer returned non-passing status for $Name."
	return $result
}

function Invoke-FixtureLanding($Session, [string] $SessionCommit, [string] $PrimaryCommit, $Report, [bool] $HasRowClaim, [bool] $HasCompletedClaim, [string] $Plan, [string] $CompletedReceipt = '') {
	Set-SessionEnvironment $Session
	$arguments = [Collections.Generic.List[string]]::new()
	foreach ($argument in @(
		'-NoProfile', '-File', $executor,
		'-CurrentWorktree', $Session.Path.ToUpperInvariant().Replace('\', '/'),
		'-PrimaryWorktree', $fixturePrimary.Replace('\', '/'),
		'-CurrentBranch', $Session.Branch,
		'-PrimaryBranch', $script:fixturePrimaryBranch,
		'-Baseline', $script:fixtureBaseline,
		'-ManifestComparisonBase', $PrimaryCommit,
		'-ExpectedCurrentTip', $SessionCommit,
		'-ExpectedPrimaryTip', $PrimaryCommit,
		'-VerificationReportPath', $Report.reportPath.Replace('\', '/'),
		'-VerificationReportSha256', $Report.sha256,
		'-ManifestRange', $Report.manifest.range,
		'-SessionOwner', $Session.Owner,
		'-SessionLabel', 'finalize landing fixture',
		'-ApprovedSessionCommit', $SessionCommit
	)) { $arguments.Add($argument) }
	if ($HasRowClaim) {
		foreach ($argument in @('-HasPlanRowClaim', '-PlanOrder', 'Documents/Plans/Order.md', '-Plan', $Plan)) { $arguments.Add($argument) }
	}
	if ($HasCompletedClaim) { foreach ($argument in @('-HasCompletedPlanClaim', '-CompletedPlanReceipt', $CompletedReceipt)) { $arguments.Add($argument) } }
	$response = Invoke-Process 'pwsh.exe' $arguments.ToArray() $Session.Path
	return [pscustomobject]@{ Response = $response; Result = Get-AgentJson $response "landing fixture $($Session.Name)" }
}

function Invoke-QueueStatus([string] $Order, [string] $Owner = '') {
	$arguments = [Collections.Generic.List[string]]::new()
	foreach ($argument in @('plan', 'queue', 'status', '--repo', $script:fixtureCommon, '--order', $Order)) { $arguments.Add($argument) }
	if (-not [string]::IsNullOrWhiteSpace($Owner)) { foreach ($argument in @('--owner', $Owner)) { $arguments.Add($argument) } }
	$response = Invoke-Process $script:fixtureWorktreeCli $arguments.ToArray() $fixturePrimary
	return [pscustomobject]@{ Response = $response; Result = Get-AgentJson $response "queue status $Order" }
}

function Assert-LandingLockAbsent {
	$response = Invoke-Process $script:fixtureWorktreeCli @('lock', 'status', '--repo', $script:fixtureCommon) $fixturePrimary
	$status = Get-AgentJson $response 'landing lock status'
	Assert-Condition ($response.ExitCode -eq 2 -and -not $status.held) 'Landing lock remained after transaction cleanup.'
}

function Assert-GlobalLandingArtifact($Landing) {
	Assert-Condition ($Landing.Result.artifact.status -in @('written', 'exists')) 'Landing did not report a global artifact.'
	Assert-Condition (Test-Path -LiteralPath $Landing.Result.artifact.path -PathType Leaf) "Landing artifact was not written: '$($Landing.Result.artifact.path)'."
	$localRoot = [IO.Path]::GetFullPath($env:LOCALAPPDATA).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
	Assert-Condition ($Landing.Result.artifact.path.StartsWith($localRoot, [StringComparison]::OrdinalIgnoreCase)) 'Landing artifact escaped fixture LOCALAPPDATA.'
}

function Claim-FixturePlanRow($Session, [string] $Plan) {
	$response = Invoke-Process $script:fixtureWorktreeCli @('plan', 'queue', 'lock', '--repo', $script:fixtureCommon, '--order', 'Documents/Plans/Order.md', '--owner', $Session.Owner, '--session', 'finalize landing fixture', '--worktree', $Session.Path) $Session.Path
	Assert-Condition ($response.ExitCode -eq 0) 'Could not acquire fixture Plans queue for row claim.'
	try {
		$response = Invoke-Process $script:fixtureWorktreeCli @('plan', 'row', 'claim', '--repo', $script:fixtureCommon, '--order', 'Documents/Plans/Order.md', '--plan', $Plan, '--owner', $Session.Owner, '--session', 'finalize landing fixture', '--worktree', $Session.Path) $Session.Path
		Assert-Condition ($response.ExitCode -eq 0) "Could not create fixture row claim: $($response.Stdout)$($response.Stderr)"
	}
	finally {
		$response = Invoke-Process $script:fixtureWorktreeCli @('plan', 'queue', 'unlock', '--repo', $script:fixtureCommon, '--order', 'Documents/Plans/Order.md', '--owner', $Session.Owner) $Session.Path
		Assert-Condition ($response.ExitCode -eq 0) 'Could not release fixture Plans queue after row claim.'
	}
}

function Complete-FixturePlan($Session) {
	$response = Invoke-Process $script:fixtureWorktreeCli @('plan', 'order', 'claim-next', '--repo', $script:fixtureCommon, '--primary-worktree', $fixturePrimary, '--worktree', $Session.Path, '--branch', $script:fixturePrimaryBranch, '--owner', $Session.Owner, '--session', 'finalize landing fixture', '--queue', 'plans') $Session.Path
	Assert-Condition ($response.ExitCode -eq 0) "Could not create fixture completed-plan claim: $($response.Stdout)$($response.Stderr)"
	$claim = Get-AgentJson $response 'fixture plan-order claim-next'
	$response = Invoke-Process $script:fixtureWorktreeCli @('plan', 'order', 'complete', '--repo', $script:fixtureCommon, '--worktree', $Session.Path, '--owner', $Session.Owner, '--session', 'finalize landing fixture', '--plan', $claim.plan) $Session.Path
	Assert-Condition ($response.ExitCode -eq 0) "Could not complete fixture plan: $($response.Stdout)$($response.Stderr)"
	$receipt = Get-AgentJson $response 'fixture plan-order complete'
	Invoke-Git $Session.Path @('add', '--', 'Documents/Plans') | Out-Null
	Invoke-Git $Session.Path @('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', 'fixture completed plan') | Out-Null
	return [pscustomobject]@{ Commit = Invoke-Git $Session.Path @('rev-parse', 'HEAD'); Plan = [string] $claim.plan; Receipt = $response.Stdout.Trim(); Result = $receipt }
}

foreach ($name in @('BROKEN_ENGINE_WORKTREE_PATH', 'BROKEN_ENGINE_SESSION_BRANCH', 'BROKEN_ENGINE_PRIMARY_CHECKOUT', 'BROKEN_ENGINE_TARGET_BRANCH', 'BROKEN_ENGINE_BASELINE', 'BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER', 'BROKEN_ENGINE_AGENT_CLIENT', 'LOCALAPPDATA')) {
	$environmentBackup[$name] = [Environment]::GetEnvironmentVariable($name)
}

try {
	$sourceRoot = Invoke-Git (Get-Location).Path @('rev-parse', '--show-toplevel')
	$sourceCommon = Invoke-Git $sourceRoot @('rev-parse', '--path-format=absolute', '--git-common-dir')
	$fixtureSourcePrimary = Split-Path -Parent $sourceCommon
	$sourceBranch = Invoke-Git $fixtureSourcePrimary @('branch', '--show-current')
	$sourceWorktreeCli = Join-Path $fixtureSourcePrimary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	$sourceSessionModule = Join-Path $sourceRoot '.agents\scripts\WorktreeCliSessionExclusion.psm1'
	Assert-Condition (Test-Path -LiteralPath $sourceWorktreeCli -PathType Leaf) "Fixture source WorktreeCli is missing: '$sourceWorktreeCli'."
	Assert-Condition (Test-Path -LiteralPath $sourceSessionModule -PathType Leaf) "Fixture source WorktreeCli session module is missing: '$sourceSessionModule'."

	$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('BrokenEngineFinalizeLanding-' + [guid]::NewGuid().ToString('N'))
	$fixturePrimary = Join-Path $fixtureRoot 'primary'
	New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
	$fixtureLocalAppData = Join-Path ([IO.Path]::GetPathRoot($fixtureRoot)) ('BEAL-' + [guid]::NewGuid().ToString('N'))
	$env:LOCALAPPDATA = $fixtureLocalAppData
	New-Item -ItemType Directory -Path (Join-Path $env:LOCALAPPDATA 'BrokenEngineLocks\plan-row') -Force | Out-Null
	$env:BROKEN_ENGINE_AGENT_CLIENT = 'unknown'
	$result = Invoke-Process 'git.exe' @('clone', '--branch', $sourceBranch, $fixtureSourcePrimary, $fixturePrimary) $fixtureRoot
	if ($result.ExitCode -ne 0) { throw "Fixture clone failed: $($result.Stdout)$($result.Stderr)" }
	Copy-Item -LiteralPath $sourceSessionModule -Destination (Join-Path $fixturePrimary '.agents\scripts\WorktreeCliSessionExclusion.psm1')
	foreach ($scriptName in @('AgentArtifactStore.psm1', 'Read-AgentReportSection.ps1')) {
		Copy-Item -LiteralPath (Join-Path $sourceRoot ('.agents\scripts\' + $scriptName)) -Destination (Join-Path $fixturePrimary ('.agents\scripts\' + $scriptName))
	}
	Invoke-Git $fixturePrimary @('add', '--', '.agents/scripts/WorktreeCliSessionExclusion.psm1', '.agents/scripts/AgentArtifactStore.psm1', '.agents/scripts/Read-AgentReportSection.ps1') | Out-Null
	if (-not [string]::IsNullOrWhiteSpace((Invoke-Git $fixturePrimary @('status', '--porcelain')))) {
		Invoke-Git $fixturePrimary @('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', 'fixture WorktreeCli session and artifact modules') | Out-Null
	}
	$script:fixtureBaseline = Invoke-Git $fixturePrimary @('rev-parse', 'HEAD')
	$script:fixturePrimaryBranch = Invoke-Git $fixturePrimary @('branch', '--show-current')
	$script:fixtureCommon = Invoke-Git $fixturePrimary @('rev-parse', '--path-format=absolute', '--git-common-dir')
	$script:fixturePrimaryOutput = Join-Path $fixturePrimary 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
	New-Item -ItemType Directory -Path $script:fixturePrimaryOutput -Force | Out-Null
	Copy-Item -LiteralPath $sourceWorktreeCli -Destination (Join-Path $script:fixturePrimaryOutput 'WorktreeCli.exe')
	$script:fixtureWorktreeCli = Join-Path $script:fixturePrimaryOutput 'WorktreeCli.exe'

	if ($CompletedPlanReceiptOnly) {
		$completedSession = Add-FixtureSession 'completed-session'
		$completed = Complete-FixturePlan $completedSession
		Assert-Condition ($completed.Result.operation -ceq 'complete' -and $completed.Result.handled) 'Fixture complete receipt was invalid.'
		$completedReport = Write-Report $completedSession 'landing-completed' $script:fixtureBaseline
		$completedLanding = Invoke-FixtureLanding $completedSession $completed.Commit $script:fixtureBaseline $completedReport $true $true $completed.Plan $completed.Receipt
		Assert-Condition ($completedLanding.Response.ExitCode -eq 0 -and $completedLanding.Result.status -ceq 'landed' -and $completedLanding.Result.planRow.completedReceiptVerified) 'Completed-plan landing did not verify the receipt and release the row claim.'
		Assert-Condition ((Invoke-Git $fixturePrimary @('rev-parse', 'HEAD')) -ceq $completed.Commit) 'Completed-plan landing did not advance primary.'
		Assert-GlobalLandingArtifact $completedLanding
		Assert-LandingLockAbsent
	}
	else {
	$successSession = Add-FixtureSession 'success-session'
	$successChange = Add-SessionChange $successSession 'FixtureSuccess'
	Claim-FixturePlanRow $successSession $successChange.Plan
	$successReport = Write-Report $successSession 'landing-success' $script:fixtureBaseline
	$success = Invoke-FixtureLanding $successSession $successChange.Commit $script:fixtureBaseline $successReport $true $false ('Documents/Plans/' + $successChange.Plan)
	Assert-Condition ($success.Response.ExitCode -eq 0 -and $success.Result.status -ceq 'landed') "Successful landing did not return LANDED: $($success.Response.Stdout)$($success.Response.Stderr)"
	Assert-Condition ($success.Result.planRow.plan -ceq $successChange.Plan) 'Landing did not normalize the public plan identity for row release.'
	Assert-Condition ((Invoke-Git $fixturePrimary @('rev-parse', 'HEAD')) -ceq $successChange.Commit) 'Successful landing did not advance primary to the approved session commit.'
	Assert-Condition ((Invoke-Git $successSession.Path @('rev-parse', 'HEAD')) -ceq $successChange.Commit) 'Successful landing changed the session tip.'
	Assert-Condition ([string]::IsNullOrEmpty((Invoke-Git $successSession.Path @('status', '--porcelain'))) -and [string]::IsNullOrEmpty((Invoke-Git $fixturePrimary @('status', '--porcelain')))) 'Successful landing left a worktree dirty.'
	Assert-GlobalLandingArtifact $success
	Assert-LandingLockAbsent
	$plans = Invoke-QueueStatus 'Documents/Plans/Order.md'
	$features = Invoke-QueueStatus 'Documents/Features/Order.md'
	Assert-Condition ($plans.Response.ExitCode -eq 2 -and -not $plans.Result.held -and $features.Response.ExitCode -eq 2 -and -not $features.Result.held) 'Successful landing retained a queue lock.'
	$response = Invoke-Process $script:fixtureWorktreeCli @('plan', 'row', 'status', '--repo', $script:fixtureCommon, '--order', 'Documents/Plans/Order.md', '--plan', $successChange.Plan, '--owner', $successSession.Owner) $successSession.Path
	$rowStatus = Get-AgentJson $response 'successful row status'
	Assert-Condition ($response.ExitCode -eq 2 -and -not $rowStatus.held) 'Successful landing retained the plan-row claim.'

	$partialSession = Add-FixtureSession 'partial-session'
	$partialChange = Add-SessionChange $partialSession 'FixturePartial'
	$partialReport = Write-Report $partialSession 'landing-partial' $successChange.Commit
	$blocker = [guid]::NewGuid().ToString()
	$response = Invoke-Process $script:fixtureWorktreeCli @('plan', 'queue', 'lock', '--repo', $script:fixtureCommon, '--order', 'Documents/Plans/Order.md', '--owner', $blocker, '--session', 'fixture blocker', '--worktree', $partialSession.Path) $partialSession.Path
	Assert-Condition ($response.ExitCode -eq 0) 'Could not set up Plans contention fixture.'
	try {
		$partial = Invoke-FixtureLanding $partialSession $partialChange.Commit $successChange.Commit $partialReport $false $false ''
		Assert-Condition ($partial.Response.ExitCode -eq 2 -and $partial.Result.code -ceq 'queue-lock.acquire-failed') 'Partial queue acquisition did not block at the expected point.'
		Assert-Condition ((Invoke-Git $fixturePrimary @('rev-parse', 'HEAD')) -ceq $successChange.Commit) 'Partial queue acquisition advanced primary.'
		$features = Invoke-QueueStatus 'Documents/Features/Order.md'
		Assert-Condition ($features.Response.ExitCode -eq 2 -and -not $features.Result.held) 'Partial queue failure retained the earlier-acquired Features lock.'
		Assert-LandingLockAbsent
	}
	finally {
		$response = Invoke-Process $script:fixtureWorktreeCli @('plan', 'queue', 'unlock', '--repo', $script:fixtureCommon, '--order', 'Documents/Plans/Order.md', '--owner', $blocker) $partialSession.Path
		Assert-Condition ($response.ExitCode -eq 0) 'Could not release Plans contention fixture lock.'
	}

	$postFailureSession = Add-FixtureSession 'post-failure-session'
	$postFailureChange = Add-SessionChange $postFailureSession 'FixturePostFailure'
	$postFailureReport = Write-Report $postFailureSession 'landing-post-failure' $successChange.Commit
	$postFailure = Invoke-FixtureLanding $postFailureSession $postFailureChange.Commit $successChange.Commit $postFailureReport $true $false 'MissingFixtureClaim.md'
	Assert-Condition ($postFailure.Response.ExitCode -eq 2 -and $postFailure.Result.code -ceq 'plan-row.not-owned' -and $postFailure.Result.primaryAdvanced) 'Post-advance claim failure did not report the required blocked state.'
	Assert-Condition ((Invoke-Git $fixturePrimary @('rev-parse', 'HEAD')) -ceq $postFailureChange.Commit) 'Post-advance failure did not preserve the completed primary advance.'
	$plans = Invoke-QueueStatus 'Documents/Plans/Order.md'
	$features = Invoke-QueueStatus 'Documents/Features/Order.md'
	Assert-Condition ($plans.Response.ExitCode -eq 2 -and -not $plans.Result.held -and $features.Response.ExitCode -eq 2 -and -not $features.Result.held) 'Post-advance failure retained an owner-held queue lock.'
	Assert-LandingLockAbsent
	}

	Write-Output 'PASS: finalization landing transaction fixtures passed.'
}
finally {
	foreach ($session in @($fixtureSessions)) {
		try { Import-Module $session.Module -Force; Unregister-WorktreeCliSession -RepositoryRoot $fixturePrimary -Owner $session.Owner } catch { Write-Warning "Could not release fixture wrapper session '$($session.Owner)': $($_.Exception.Message)" }
	}
	foreach ($session in @($fixtureSessions)) {
		if (Test-Path -LiteralPath $session.Path) {
			try { Invoke-Git $fixturePrimary @('worktree', 'remove', '--force', $session.Path) | Out-Null } catch { Write-Warning "Could not remove fixture worktree '$($session.Path)': $($_.Exception.Message)" }
		}
	}
	foreach ($branch in @($fixtureBranchNames)) {
		if ($null -ne $fixturePrimary -and (Test-Path -LiteralPath $fixturePrimary)) {
			try { Invoke-Git $fixturePrimary @('branch', '--delete', '--force', $branch) | Out-Null } catch { Write-Warning "Could not delete fixture branch '$branch': $($_.Exception.Message)" }
		}
	}
	if ($null -ne $fixtureRoot -and (Test-Path -LiteralPath $fixtureRoot)) { Remove-Item -LiteralPath $fixtureRoot -Recurse -Force }
	if ($null -ne $fixtureLocalAppData -and (Test-Path -LiteralPath $fixtureLocalAppData)) { Remove-Item -LiteralPath $fixtureLocalAppData -Recurse -Force }
	foreach ($entry in $environmentBackup.GetEnumerator()) {
		if ($null -eq $entry.Value) { Remove-Item "Env:$($entry.Key)" -ErrorAction SilentlyContinue }
		else { Set-Item "Env:$($entry.Key)" $entry.Value }
	}
}
