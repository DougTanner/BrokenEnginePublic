# Deterministic scratch-repository fixtures for Test-SessionAuditRequirement.ps1.
# These checks never inspect or mutate the caller's repository.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Failures = [Collections.Generic.List[string]]::new()
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$evaluator = Join-Path $PSScriptRoot 'Test-SessionAuditRequirement.ps1'
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) "BrokenEngineSessionAuditFixtures\$([guid]::NewGuid().ToString('N'))"

function Assert-True([bool] $Condition, [string] $Name) {
	if ($Condition) { Write-Host "pass $Name" }
	else { $script:Failures.Add($Name); Write-Host "FAIL $Name" }
}

function Invoke-NativeText([string] $Executable, [string[]] $Arguments, [string] $WorkingDirectory, [string] $StandardInput = $null) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $Executable
	$start.WorkingDirectory = $WorkingDirectory
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.RedirectStandardInput = $null -ne $StandardInput
	$start.StandardOutputEncoding = $utf8
	$start.StandardErrorEncoding = $utf8
	foreach ($argument in $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start '$Executable'." }
	if ($null -ne $StandardInput) {
		$process.StandardInput.Write($StandardInput)
		$process.StandardInput.Close()
	}
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$response = [pscustomobject]@{ ExitCode = $process.ExitCode; Stdout = $stdoutTask.GetAwaiter().GetResult(); Stderr = $stderrTask.GetAwaiter().GetResult() }
	$process.Dispose()
	return $response
}

function Invoke-Git([string] $Repository, [string[]] $Arguments) {
	$response = Invoke-NativeText 'git.exe' (@('-C', $Repository) + $Arguments) $Repository
	if ($response.ExitCode -ne 0) { throw "git $($Arguments -join ' ') failed: $($response.Stderr.Trim())." }
	return $response.Stdout.Trim()
}

function Write-Utf8([string] $Path, [string] $Text) {
	[IO.File]::WriteAllText($Path, $Text, $utf8)
}

function New-FixtureRepository([string] $Name, [bool] $AddedSessionFile = $false, [bool] $OverlappingAdvance = $false) {
	$repository = Join-Path $fixtureRoot $Name
	New-Item -ItemType Directory -Path $repository | Out-Null
	Invoke-Git $repository @('init', '-b', 'main') | Out-Null
	Invoke-Git $repository @('config', 'user.name', 'Fixture User') | Out-Null
	Invoke-Git $repository @('config', 'user.email', 'fixture@example.invalid') | Out-Null
	Invoke-Git $repository @('config', 'core.autocrlf', 'false') | Out-Null
	Invoke-Git $repository @('config', 'core.filemode', 'true') | Out-Null
	Write-Utf8 (Join-Path $repository 'session.txt') "base-a`nkeep-1`nkeep-2`nkeep-3`nbase-b`n"
	Write-Utf8 (Join-Path $repository 'stable.txt') "stable`n"
	Invoke-Git $repository @('add', '--', 'session.txt', 'stable.txt') | Out-Null
	Invoke-Git $repository @('commit', '-m', 'base') | Out-Null
	$base = Invoke-Git $repository @('rev-parse', 'HEAD')

	Invoke-Git $repository @('checkout', '-b', 'pre-rebase') | Out-Null
	$sessionPath = if ($AddedSessionFile) { 'new.txt' } else { 'session.txt' }
	if ($AddedSessionFile) { Write-Utf8 (Join-Path $repository $sessionPath) "new-session`n" }
	else { Write-Utf8 (Join-Path $repository $sessionPath) "session-a`nkeep-1`nkeep-2`nkeep-3`nbase-b`n" }
	Invoke-Git $repository @('add', '--', $sessionPath) | Out-Null
	Invoke-Git $repository @('commit', '-m', 'session change') | Out-Null
	$preRebase = Invoke-Git $repository @('rev-parse', 'HEAD')

	Invoke-Git $repository @('checkout', 'main') | Out-Null
	if ($OverlappingAdvance) { Write-Utf8 (Join-Path $repository 'session.txt') "base-a`nkeep-1`nkeep-2`nkeep-3`nprimary-b`n" }
	else { Write-Utf8 (Join-Path $repository 'primary.txt') "primary`n" }
	$primaryPath = if ($OverlappingAdvance) { 'session.txt' } else { 'primary.txt' }
	Invoke-Git $repository @('add', '--', $primaryPath) | Out-Null
	Invoke-Git $repository @('commit', '-m', 'primary advance') | Out-Null
	$rebasedParent = Invoke-Git $repository @('rev-parse', 'HEAD')
	Invoke-Git $repository @('checkout', '-b', 'rebased') | Out-Null
	Invoke-Git $repository @('cherry-pick', $preRebase) | Out-Null
	$rebased = Invoke-Git $repository @('rev-parse', 'HEAD')

	$status = if ($AddedSessionFile) { 'untracked' } else { 'M' }
	$contentIdentity = Invoke-Git $repository @('rev-parse', "$preRebase`:$sessionPath")
	$changedPaths = @(if ($OverlappingAdvance) { 'session.txt' } else { 'primary.txt' })
	$overlappingPaths = @(if ($OverlappingAdvance) { 'session.txt' })
	$inputObject = [ordered]@{
		schemaVersion = 'broken-engine-session-audit-input/v1'
		repositoryRoot = $repository
		preRebaseSessionCommit = $preRebase
		rebasedCommit = $rebased
		verifiedManifest = @([ordered]@{ path = $sessionPath; status = $status; mode = '100644'; contentIdentity = $contentIdentity })
		dependencyOverlapEvidence = [ordered]@{ preRebaseParent = $base; rebasedParent = $rebasedParent; changedPaths = $changedPaths; directDependencyPaths = @(); overlappingPaths = $overlappingPaths }
		conflictFree = $true
		requiredDomainCoverage = $true
		requiredAdversarialCoverage = $true
		lateSemanticFixes = $false
		manualResolution = $false
		invalidatedAssumptions = $false
		unseenContractSignificantRegions = $false
		explicitUserRequest = $false
	}
	return [pscustomobject]@{ Root = $repository; Base = $base; PreRebase = $preRebase; RebasedParent = $rebasedParent; Rebased = $rebased; SessionPath = $sessionPath; Input = $inputObject }
}

function Copy-Input($InputObject) {
	return (($InputObject | ConvertTo-Json -Depth 20 -Compress) | ConvertFrom-Json -Depth 20)
}

function Invoke-EvaluatorJson([string] $InputJson, [string] $Case) {
	$response = Invoke-NativeText 'pwsh.exe' @('-NoProfile', '-File', $evaluator, '-InputJson', $InputJson) $fixtureRoot
	Assert-True ($response.ExitCode -eq 0) "$Case exit=0"
	Assert-True ([string]::IsNullOrWhiteSpace($response.Stderr)) "$Case stderr empty"
	try { $json = $response.Stdout | ConvertFrom-Json -Depth 32 -ErrorAction Stop }
	catch { $script:Failures.Add("$Case emitted valid JSON"); Write-Host "FAIL $Case emitted valid JSON"; return $null }
	Assert-True ($json.schemaVersion -ceq 'broken-engine-session-audit-decision/v1') "$Case decision schema"
	return $json
}

function Invoke-Evaluator($InputObject, [string] $Case) {
	return Invoke-EvaluatorJson ($InputObject | ConvertTo-Json -Depth 20 -Compress) $Case
}

function Assert-RequiredReason($Decision, [string] $Reason, [string] $Case) {
	if ($null -eq $Decision) { return }
	Assert-True ($Decision.required -eq $true) "$Case required=true"
	Assert-True (@($Decision.reasons) -ccontains $Reason) "$Case reason $Reason (actual: $(@($Decision.reasons) -join ', '); $($Decision.message))"
}

function New-VariantCommit($RepositoryFixture, [string] $Name, [string] $Kind) {
	$root = $RepositoryFixture.Root
	Invoke-Git $root @('reset', '--hard') | Out-Null
	Invoke-Git $root @('checkout', '-f', '-B', $Name, $RepositoryFixture.RebasedParent) | Out-Null
	switch ($Kind) {
		'content' {
			Write-Utf8 (Join-Path $root 'session.txt') "different-session`nkeep-1`nkeep-2`nkeep-3`nbase-b`n"
			Invoke-Git $root @('add', '--', 'session.txt') | Out-Null
		}
		'mode' {
			Write-Utf8 (Join-Path $root 'session.txt') "session-a`nkeep-1`nkeep-2`nkeep-3`nbase-b`n"
			Invoke-Git $root @('add', '--', 'session.txt') | Out-Null
			Invoke-Git $root @('update-index', '--chmod=+x', '--', 'session.txt') | Out-Null
		}
		'path' {
			Invoke-Git $root @('mv', '--', 'session.txt', 'renamed.txt') | Out-Null
			Write-Utf8 (Join-Path $root 'renamed.txt') "session-a`nkeep-1`nkeep-2`nkeep-3`nbase-b`n"
			Invoke-Git $root @('add', '--', 'renamed.txt') | Out-Null
		}
		default { throw "Unknown variant '$Kind'." }
	}
	Invoke-Git $root @('commit', '-m', "$Kind variant") | Out-Null
	return Invoke-Git $root @('rev-parse', 'HEAD')
}

function New-GitlinkFixture([string] $Name) {
	$repository = Join-Path $fixtureRoot $Name
	$submoduleRepository = Join-Path $fixtureRoot "$Name-submodule"
	New-Item -ItemType Directory -Path $repository, $submoduleRepository | Out-Null
	Invoke-Git $submoduleRepository @('init', '-b', 'main') | Out-Null
	Invoke-Git $submoduleRepository @('config', 'user.name', 'Fixture User') | Out-Null
	Invoke-Git $submoduleRepository @('config', 'user.email', 'fixture@example.invalid') | Out-Null
	Write-Utf8 (Join-Path $submoduleRepository 'module.txt') "base`n"
	Invoke-Git $submoduleRepository @('add', '--', 'module.txt') | Out-Null
	Invoke-Git $submoduleRepository @('commit', '-m', 'module base') | Out-Null

	Invoke-Git $repository @('init', '-b', 'main') | Out-Null
	Invoke-Git $repository @('config', 'user.name', 'Fixture User') | Out-Null
	Invoke-Git $repository @('config', 'user.email', 'fixture@example.invalid') | Out-Null
	Write-Utf8 (Join-Path $repository 'stable.txt') "stable`n"
	Invoke-Git $repository @('add', '--', 'stable.txt') | Out-Null
	Invoke-Git $repository @('commit', '-m', 'host base') | Out-Null
	Invoke-Git $repository @('-c', 'protocol.file.allow=always', 'submodule', 'add', $submoduleRepository, 'module') | Out-Null
	Invoke-Git $repository @('add', '--', 'module') | Out-Null
	Invoke-Git $repository @('commit', '-m', 'add module') | Out-Null
	$base = Invoke-Git $repository @('rev-parse', 'HEAD')
	Write-Utf8 (Join-Path $submoduleRepository 'module.txt') "session`n"
	Invoke-Git $submoduleRepository @('add', '--', 'module.txt') | Out-Null
	Invoke-Git $submoduleRepository @('commit', '-m', 'module session') | Out-Null
	$sessionObjectId = Invoke-Git $submoduleRepository @('rev-parse', 'HEAD')

	Invoke-Git $repository @('checkout', '-b', 'pre-rebase') | Out-Null
	$submodulePath = Join-Path $repository 'module'
	Invoke-Git $submodulePath @('fetch', 'origin') | Out-Null
	Invoke-Git $submodulePath @('checkout', '--detach', $sessionObjectId) | Out-Null
	Invoke-Git $repository @('add', '--', 'module') | Out-Null
	Invoke-Git $repository @('commit', '-m', 'session module update') | Out-Null
	$preRebase = Invoke-Git $repository @('rev-parse', 'HEAD')

	Invoke-Git $repository @('checkout', 'main') | Out-Null
	Write-Utf8 (Join-Path $repository 'primary.txt') "primary`n"
	Invoke-Git $repository @('add', '--', 'primary.txt') | Out-Null
	Invoke-Git $repository @('commit', '-m', 'primary advance') | Out-Null
	$rebasedParent = Invoke-Git $repository @('rev-parse', 'HEAD')
	Invoke-Git $repository @('checkout', '-b', 'rebased') | Out-Null
	Invoke-Git $repository @('cherry-pick', $preRebase) | Out-Null
	$rebased = Invoke-Git $repository @('rev-parse', 'HEAD')
	$submoduleStatusResponse = Invoke-NativeText 'git.exe' @('-C', $repository, 'submodule', 'status', '--', 'module') $repository
	if ($submoduleStatusResponse.ExitCode -ne 0) { throw "git submodule status failed: $($submoduleStatusResponse.Stderr.Trim())." }
	$submoduleStatus = $submoduleStatusResponse.Stdout.TrimEnd("`r", "`n")

	$inputObject = [ordered]@{
		schemaVersion = 'broken-engine-session-audit-input/v1'
		repositoryRoot = $repository
		preRebaseSessionCommit = $preRebase
		rebasedCommit = $rebased
		verifiedManifest = @([ordered]@{ path = 'module'; status = 'M'; mode = '160000'; contentIdentity = $submoduleStatus })
		dependencyOverlapEvidence = [ordered]@{ preRebaseParent = $base; rebasedParent = $rebasedParent; changedPaths = @('primary.txt'); directDependencyPaths = @(); overlappingPaths = @() }
		conflictFree = $true
		requiredDomainCoverage = $true
		requiredAdversarialCoverage = $true
		lateSemanticFixes = $false
		manualResolution = $false
		invalidatedAssumptions = $false
		unseenContractSignificantRegions = $false
		explicitUserRequest = $false
	}
	return [pscustomobject]@{
		Root = $repository
		SubmoduleRepository = $submoduleRepository
		RebasedParent = $rebasedParent
		Input = $inputObject
		SessionObjectId = $sessionObjectId
		SubmoduleStatus = $submoduleStatus
	}
}

function New-GitlinkPointerVariant($RepositoryFixture, [string] $Content = 'rebased-pointer') {
	$root = $RepositoryFixture.Root
	$submoduleRepository = $RepositoryFixture.SubmoduleRepository
	Write-Utf8 (Join-Path $submoduleRepository 'module.txt') "$Content`n"
	Invoke-Git $submoduleRepository @('add', '--', 'module.txt') | Out-Null
	Invoke-Git $submoduleRepository @('commit', '-m', 'module rebased pointer') | Out-Null
	$rebasedObjectId = Invoke-Git $submoduleRepository @('rev-parse', 'HEAD')

	Invoke-Git $root @('reset', '--hard') | Out-Null
	Invoke-Git $root @('checkout', '-f', '-B', 'gitlink-pointer-variant', $RepositoryFixture.RebasedParent) | Out-Null
	$submodulePath = Join-Path $root 'module'
	Invoke-Git $submodulePath @('fetch', 'origin') | Out-Null
	Invoke-Git $submodulePath @('checkout', '--detach', $rebasedObjectId) | Out-Null
	Invoke-Git $root @('add', '--', 'module') | Out-Null
	Invoke-Git $root @('commit', '-m', 'rebased module pointer') | Out-Null
	$submoduleStatusResponse = Invoke-NativeText 'git.exe' @('-C', $root, 'submodule', 'status', '--', 'module') $root
	if ($submoduleStatusResponse.ExitCode -ne 0) { throw "git submodule status failed: $($submoduleStatusResponse.Stderr.Trim())." }
	return [pscustomobject]@{
		Rebased = Invoke-Git $root @('rev-parse', 'HEAD')
		ObjectId = $rebasedObjectId
		Status = $submoduleStatusResponse.Stdout.TrimEnd("`r", "`n")
	}
}

try {
	if (-not (Test-Path -LiteralPath $evaluator -PathType Leaf)) { throw "Evaluator is missing: $evaluator" }
	New-Item -ItemType Directory -Path $fixtureRoot | Out-Null

	$clean = New-FixtureRepository 'clean'
	$cleanDecision = Invoke-Evaluator $clean.Input 'unchanged conflict-free rebase'
	if ($null -ne $cleanDecision) {
		Assert-True ($cleanDecision.required -eq $false) "unchanged conflict-free rebase skips audit (actual: $(@($cleanDecision.reasons) -join ', '); $($cleanDecision.message))"
		Assert-True (@($cleanDecision.reasons).Count -eq 0) "unchanged conflict-free rebase has no reasons (actual: $(@($cleanDecision.reasons) -join ', '); $($cleanDecision.message))"
		Assert-True ($cleanDecision.digests.before -ceq $cleanDecision.digests.after) 'unchanged conflict-free before/after digest equal'
		Assert-True ($cleanDecision.digests.before -ceq $cleanDecision.digests.verified) 'unchanged conflict-free verified digest bound'
	}
	Assert-True ($clean.Input.dependencyOverlapEvidence.changedPaths.Count -eq 1 -and
		$clean.Input.dependencyOverlapEvidence.changedPaths[0] -ceq 'primary.txt') 'non-overlapping primary advance recorded'

	$added = New-FixtureRepository 'added' $true
	$addedDecision = Invoke-Evaluator $added.Input 'verified-untracked normalized to added'
	if ($null -ne $addedDecision) {
		Assert-True ($addedDecision.required -eq $false) "verified-untracked normalization skips audit (actual: $(@($addedDecision.reasons) -join ', '); $($addedDecision.message))"
		Assert-True ($addedDecision.digests.before -ceq $addedDecision.digests.verified) 'verified-untracked digest normalizes to added'
	}

	foreach ($kind in @('content', 'mode', 'path')) {
		$inputVariant = Copy-Input $clean.Input
		$inputVariant.rebasedCommit = New-VariantCommit $clean "variant-$kind" $kind
		$decision = Invoke-Evaluator $inputVariant "$kind difference"
		Assert-RequiredReason $decision 'reconciliation.delta-changed' "$kind difference"
	}

	$overlap = New-FixtureRepository 'overlap' $false $true
	$overlapDecision = Invoke-Evaluator $overlap.Input 'dependency overlap'
	Assert-RequiredReason $overlapDecision 'dependency-overlap.detected' 'dependency overlap'

	$dependencyOnlyInput = Copy-Input $clean.Input
	$dependencyOnlyInput.dependencyOverlapEvidence.directDependencyPaths = @('primary.txt')
	$dependencyOnlyInput.dependencyOverlapEvidence.overlappingPaths = @('primary.txt')
	$dependencyOnlyDecision = Invoke-Evaluator $dependencyOnlyInput 'dependency-only overlap'
	Assert-RequiredReason $dependencyOnlyDecision 'dependency-overlap.detected' 'dependency-only overlap'

	$badDependencyOnlyInput = Copy-Input $dependencyOnlyInput
	$badDependencyOnlyInput.dependencyOverlapEvidence.overlappingPaths = @()
	$badDependencyOnlyDecision = Invoke-Evaluator $badDependencyOnlyInput 'dependency-only overlap evidence mismatch'
	Assert-RequiredReason $badDependencyOnlyDecision 'dependency-overlap.evidence-mismatch' 'dependency-only overlap evidence mismatch'

	$gitlink = New-GitlinkFixture 'gitlink'
	$gitlinkDecision = Invoke-Evaluator $gitlink.Input 'unchanged gitlink rebase'
	if ($null -ne $gitlinkDecision) {
		Assert-True ($gitlinkDecision.required -eq $false) "unchanged gitlink rebase skips audit (actual: $(@($gitlinkDecision.reasons) -join ', '); $($gitlinkDecision.message))"
		Assert-True ($gitlinkDecision.digests.before -ceq $gitlinkDecision.digests.verified) 'unchanged gitlink verified status identity bound'
	}
	Assert-True ($gitlink.SubmoduleStatus -cne $gitlink.SessionObjectId) 'gitlink verified identity retains submodule status prefix and path'
	$badGitlinkInput = Copy-Input $gitlink.Input
	$badGitlinkInput.verifiedManifest[0].contentIdentity = $gitlink.SessionObjectId
	$badGitlinkDecision = Invoke-Evaluator $badGitlinkInput 'gitlink bare object identity mismatch'
	Assert-RequiredReason $badGitlinkDecision 'verification.manifest-mismatch' 'gitlink bare object identity mismatch'
	$wholeLineGitlinkInput = Copy-Input $gitlink.Input
	$wholeLineGitlinkInput.verifiedManifest[0].contentIdentity = "$($gitlink.SubmoduleStatus) stale-description"
	$wholeLineGitlinkDecision = Invoke-Evaluator $wholeLineGitlinkInput 'gitlink whole-line identity mismatch'
	Assert-RequiredReason $wholeLineGitlinkDecision 'verification.manifest-mismatch' 'gitlink whole-line identity mismatch'
	Assert-True ($gitlink.SubmoduleStatus[0] -ceq ' ') 'gitlink clean status has space prefix'
	$normalizedGitlinkInput = Copy-Input $gitlink.Input
	$normalizedGitlinkInput.verifiedManifest[0].contentIdentity = "+$($gitlink.SubmoduleStatus.Substring(1))"
	$normalizedGitlinkDecision = Invoke-Evaluator $normalizedGitlinkInput 'gitlink committed dirty status normalizes to clean'
	if ($null -ne $normalizedGitlinkDecision) {
		Assert-True ($normalizedGitlinkDecision.required -eq $false) "gitlink committed dirty status normalizes to clean (actual: $(@($normalizedGitlinkDecision.reasons) -join ', '); $($normalizedGitlinkDecision.message))"
	}
	$descriptionVariantGitlinkInput = Copy-Input $gitlink.Input
	$descriptionVariantGitlinkInput.verifiedManifest[0].contentIdentity = "+$($gitlink.SubmoduleStatus.Substring(1)) stale-description"
	$descriptionVariantGitlinkDecision = Invoke-Evaluator $descriptionVariantGitlinkInput 'gitlink dirty status description mismatch'
	Assert-RequiredReason $descriptionVariantGitlinkDecision 'verification.manifest-mismatch' 'gitlink dirty status description mismatch'
	foreach ($prefix in @('-', 'U')) {
		$stateGitlinkInput = Copy-Input $gitlink.Input
		$stateGitlinkInput.verifiedManifest[0].contentIdentity = "$prefix$($gitlink.SubmoduleStatus.Substring(1))"
		$stateGitlinkDecision = Invoke-Evaluator $stateGitlinkInput "gitlink $prefix state mismatch"
		Assert-RequiredReason $stateGitlinkDecision 'verification.manifest-mismatch' "gitlink $prefix state mismatch"
	}
	$gitlinkPointerVariant = New-GitlinkPointerVariant $gitlink
	$changedGitlinkInput = Copy-Input $gitlink.Input
	$changedGitlinkInput.rebasedCommit = $gitlinkPointerVariant.Rebased
	$changedGitlinkInput.verifiedManifest[0].contentIdentity = $gitlinkPointerVariant.Status
	$changedGitlinkDecision = Invoke-Evaluator $changedGitlinkInput 'changed gitlink pointer at rebased checkout'
	Assert-RequiredReason $changedGitlinkDecision 'reconciliation.delta-changed' 'changed gitlink pointer at rebased checkout'
	Assert-True ($gitlinkPointerVariant.Status.StartsWith(" $($gitlinkPointerVariant.ObjectId) ", [StringComparison]::Ordinal)) 'changed gitlink fixture current status binds rebased object id'

	$gitlinkPointerConflictVariant = New-GitlinkPointerVariant $gitlink 'conflict-pointer'
	Invoke-Git $gitlink.Root @('update-index', '--force-remove', '--', 'module') | Out-Null
	$gitlinkConflictIndex = "160000 $($gitlink.SessionObjectId) 1`tmodule`n160000 $($gitlinkPointerVariant.ObjectId) 2`tmodule`n160000 $($gitlinkPointerConflictVariant.ObjectId) 3`tmodule`n"
	$gitlinkConflictResponse = Invoke-NativeText 'git.exe' @('-C', $gitlink.Root, 'update-index', '--index-info') $gitlink.Root $gitlinkConflictIndex
	if ($gitlinkConflictResponse.ExitCode -ne 0) { throw "git update-index --index-info failed: $($gitlinkConflictResponse.Stderr.Trim())." }
	$conflictedGitlinkStatus = (Invoke-NativeText 'git.exe' @('-C', $gitlink.Root, 'submodule', 'status', '--', 'module') $gitlink.Root).Stdout.TrimEnd("`r", "`n")
	Assert-True ($conflictedGitlinkStatus.StartsWith('U', [StringComparison]::Ordinal)) "gitlink conflict fixture produces U status (actual: $conflictedGitlinkStatus)"
	$conflictedGitlinkInput = Copy-Input $gitlink.Input
	$conflictedGitlinkInput.verifiedManifest[0].contentIdentity = $conflictedGitlinkStatus
	$conflictedGitlinkDecision = Invoke-Evaluator $conflictedGitlinkInput 'gitlink exact U status mismatch'
	Assert-RequiredReason $conflictedGitlinkDecision 'verification.manifest-mismatch' 'gitlink exact U status mismatch'

	Invoke-Git $gitlink.Root @('reset', '--hard', $gitlinkPointerConflictVariant.Rebased) | Out-Null
	Invoke-Git $gitlink.Root @('submodule', 'deinit', '-f', '--', 'module') | Out-Null
	$uninitializedGitlinkStatus = (Invoke-NativeText 'git.exe' @('-C', $gitlink.Root, 'submodule', 'status', '--', 'module') $gitlink.Root).Stdout.TrimEnd("`r", "`n")
	Assert-True ($uninitializedGitlinkStatus.StartsWith('-', [StringComparison]::Ordinal)) 'gitlink uninitialized fixture produces - status'
	$uninitializedGitlinkInput = Copy-Input $gitlink.Input
	$uninitializedGitlinkInput.verifiedManifest[0].contentIdentity = $uninitializedGitlinkStatus
	$uninitializedGitlinkDecision = Invoke-Evaluator $uninitializedGitlinkInput 'gitlink exact - status mismatch'
	Assert-RequiredReason $uninitializedGitlinkDecision 'verification.manifest-mismatch' 'gitlink exact - status mismatch'

	$conflictInput = Copy-Input $clean.Input
	$conflictInput.conflictFree = $false
	$conflictDecision = Invoke-Evaluator $conflictInput 'conflict reconciliation'
	Assert-RequiredReason $conflictDecision 'reconciliation.not-conflict-free' 'conflict reconciliation'

	$manualInput = Copy-Input $clean.Input
	$manualInput.manualResolution = $true
	$manualDecision = Invoke-Evaluator $manualInput 'manual resolution'
	Assert-RequiredReason $manualDecision 'manual-resolution.present' 'manual resolution'

	$flagCases = [ordered]@{
		requiredDomainCoverage = @($false, 'coverage.domain-incomplete')
		requiredAdversarialCoverage = @($false, 'coverage.adversarial-incomplete')
		lateSemanticFixes = @($true, 'late-semantic-fixes.present')
		invalidatedAssumptions = @($true, 'invalidated-assumptions.present')
		unseenContractSignificantRegions = @($true, 'unseen-contract-significant-regions.present')
		explicitUserRequest = @($true, 'explicit-user-request')
	}
	foreach ($property in $flagCases.Keys) {
		$flagInput = Copy-Input $clean.Input
		$flagInput.$property = $flagCases[$property][0]
		$flagDecision = Invoke-Evaluator $flagInput $property
		Assert-RequiredReason $flagDecision $flagCases[$property][1] $property
	}

	$badOverlapInput = Copy-Input $clean.Input
	$badOverlapInput.dependencyOverlapEvidence.changedPaths = @()
	$badOverlapDecision = Invoke-Evaluator $badOverlapInput 'unproven overlap evidence'
	Assert-RequiredReason $badOverlapDecision 'dependency-overlap.evidence-mismatch' 'unproven overlap evidence'

	$missingInput = Copy-Input $clean.Input
	$missingInput.PSObject.Properties.Remove('requiredDomainCoverage')
	$missingDecision = Invoke-Evaluator $missingInput 'missing evidence'
	Assert-RequiredReason $missingDecision 'evidence.missing-malformed-or-unproven' 'missing evidence'

	$malformedDecision = Invoke-EvaluatorJson '{not-json' 'malformed input'
	Assert-RequiredReason $malformedDecision 'evidence.missing-malformed-or-unproven' 'malformed input'
}
catch {
	$script:Failures.Add("fixture exception: $($_.Exception.Message)")
	Write-Host "FAIL fixture exception: $($_.Exception.Message)"
}
finally {
	try {
		$resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
		$resolvedTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
		if ($resolvedFixtureRoot.StartsWith($resolvedTempRoot, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolvedFixtureRoot)) {
			Remove-Item -LiteralPath $resolvedFixtureRoot -Recurse -Force
		}
	}
	catch { $script:Failures.Add("fixture cleanup failed: $($_.Exception.Message)") }
}

Write-Host ''
if ($script:Failures.Count -ne 0) {
	Write-Host "Session-audit requirement fixtures FAILED ($($script:Failures.Count) assertion(s))."
	exit 1
}
Write-Host 'Session-audit requirement fixtures passed.'
exit 0
