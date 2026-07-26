[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Assert-True([bool] $Condition, [string] $Message) {
	if (-not $Condition) { throw $Message }
}

function Invoke-Git([string] $Repository, [string[]] $Arguments) {
	$output = @(& git -C $Repository @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "Git fixture command failed: $($Arguments -join ' ')" }
	return $output
}

function New-Transcript([string] $Path, [string] $SessionId, [string] $Cwd, [DateTimeOffset] $Start, [DateTimeOffset] $End, [DateTimeOffset] $LastWrite) {
	$directory = Split-Path -Parent $Path
	[void] (New-Item -ItemType Directory -Path $directory -Force)
	$records = @(
		[ordered]@{
			timestamp = $Start.ToString('O')
			type = 'session_meta'
			payload = [ordered]@{ cwd = $Cwd; session_id = $SessionId }
		},
		[ordered]@{
			timestamp = $End.ToString('O')
			type = 'event'
			payload = [ordered]@{}
		}
	)
	($records | ForEach-Object { $_ | ConvertTo-Json -Depth 8 -Compress }) | Set-Content -LiteralPath $Path -Encoding utf8
	[IO.File]::SetLastWriteTimeUtc($Path, $LastWrite.UtcDateTime)
}

function Clear-SessionStores([string[]] $Roots) {
	foreach ($root in $Roots) {
		Get-ChildItem -LiteralPath $root -Force | Remove-Item -Force -Recurse
	}
}

function Invoke-Finder(
	[string] $Finder,
	[string] $Commit,
	[string] $ReviewRoot,
	[string] $Sessions,
	[string] $ArchivedSessions,
	[string] $SessionId,
	[string] $OverrideSessionStoreRoot
) {
	$pwsh = Join-Path $PSHOME 'pwsh.exe'
	if (-not (Test-Path -LiteralPath $pwsh -PathType Leaf)) { throw 'PowerShell executable is unavailable.' }
	$sessionRoot = if ([string]::IsNullOrWhiteSpace($OverrideSessionStoreRoot)) { $Sessions } else { $OverrideSessionStoreRoot }
	$arguments = [Collections.Generic.List[string]]::new()
	foreach ($argument in @(
		'-NoLogo', '-NoProfile', '-File', $Finder,
		'-Commit', $Commit,
		'-RepositoryRoot', $ReviewRoot,
		'-SessionStoreRoot', $sessionRoot,
		'-ArchivedSessionStoreRoot', $ArchivedSessions
	)) { [void] $arguments.Add($argument) }
	if (-not [string]::IsNullOrWhiteSpace($SessionId)) {
		[void] $arguments.Add('-SessionId')
		[void] $arguments.Add($SessionId)
	}
	$output = @(& $pwsh @arguments 2>&1)
	$exitCode = $LASTEXITCODE
	$json = ($output | ForEach-Object { [string] $_ }) -join [Environment]::NewLine
	try { $result = $json | ConvertFrom-Json -Depth 16 }
	catch { throw "Finder did not return JSON: $json" }
	return [pscustomobject]@{ ExitCode = $exitCode; Result = $result }
}

function Assert-Finder([object] $Response, [int] $ExpectedExitCode, [string] $ExpectedCode) {
	Assert-True ($Response.ExitCode -eq $ExpectedExitCode) "[$script:FixtureStage] Expected finder exit $ExpectedExitCode, got $($Response.ExitCode): $($Response.Result | ConvertTo-Json -Compress -Depth 16)"
	Assert-True ($Response.Result.code -eq $ExpectedCode) "[$script:FixtureStage] Expected finder code $ExpectedCode, got $($Response.Result.code): $($Response.Result | ConvertTo-Json -Compress -Depth 16)"
}

function New-Junction([string] $Path, [string] $Target) {
	try {
		New-Item -ItemType Junction -Path $Path -Target $Target -ErrorAction Stop | Out-Null
		return $true
	}
	catch { return $false }
}

function New-FileLink([string] $Path, [string] $Target) {
	try {
		New-Item -ItemType SymbolicLink -Path $Path -Target $Target -ErrorAction Stop | Out-Null
		return $true
	}
	catch { return $false }
}

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) "next-plan-review-transcript-fixture-$([guid]::NewGuid().ToString('N'))"
$fixtureRootFull = [IO.Path]::GetFullPath($fixtureRoot)
$tempRootFull = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$primary = Join-Path $fixtureRoot 'primary'
$worktreeUuid = '11111111-1111-1111-1111-111111111111'
$producing = Join-Path $fixtureRoot $worktreeUuid
$review = Join-Path $fixtureRoot 'review'
$sessions = Join-Path $fixtureRoot 'sessions'
$archivedSessions = Join-Path $fixtureRoot 'archived_sessions'
$finder = Join-Path $PSScriptRoot 'Find-AgentSessionTranscript.ps1'
$originalAuthorDate = [Environment]::GetEnvironmentVariable('GIT_AUTHOR_DATE', 'Process')
$originalCommitterDate = [Environment]::GetEnvironmentVariable('GIT_COMMITTER_DATE', 'Process')
$primaryCreated = $false

try {
	[void] (New-Item -ItemType Directory -Path $fixtureRoot)
	[void] (New-Item -ItemType Directory -Path $sessions)
	[void] (New-Item -ItemType Directory -Path $archivedSessions)
	& git init $primary | Out-Null
	if ($LASTEXITCODE -ne 0) { throw 'Git fixture initialization failed.' }
	$primaryCreated = $true
	Invoke-Git $primary @('config', 'user.email', 'fixture@example.invalid') | Out-Null
	Invoke-Git $primary @('config', 'user.name', 'Fixture') | Out-Null
	Set-Content -LiteralPath (Join-Path $primary 'fixture.txt') -Value 'fixture' -Encoding utf8
	Invoke-Git $primary @('add', 'fixture.txt') | Out-Null
	Invoke-Git $primary @('commit', '-m', 'fixture base') | Out-Null
	$baseCommit = @(Invoke-Git $primary @('rev-parse', 'HEAD'))[0].Trim()
	Invoke-Git $primary @('worktree', 'add', '-b', 'fixture-producing', $producing, $baseCommit) | Out-Null
	Invoke-Git $primary @('worktree', 'add', '-b', 'fixture-review', $review, $baseCommit) | Out-Null
	$commitTimestamp = [DateTimeOffset]::UtcNow.AddMinutes(-30)
	[Environment]::SetEnvironmentVariable('GIT_AUTHOR_DATE', $commitTimestamp.ToString('O'), 'Process')
	[Environment]::SetEnvironmentVariable('GIT_COMMITTER_DATE', $commitTimestamp.ToString('O'), 'Process')
	Set-Content -LiteralPath (Join-Path $producing 'landing.txt') -Value 'fixture landing' -Encoding utf8
	Invoke-Git $producing @('add', 'landing.txt') | Out-Null
	Invoke-Git $producing @('commit', '-m', 'fixture landing') | Out-Null
	$commit = @(Invoke-Git $producing @('rev-parse', 'HEAD'))[0].Trim()
	$eligibleRootCount = 0
	foreach ($worktree in @($primary, $producing, $review)) {
		& git -C $worktree merge-base --is-ancestor $commit HEAD
		if ($LASTEXITCODE -eq 0) { $eligibleRootCount++ }
		elseif ($LASTEXITCODE -ne 1) { throw 'Fixture worktree ancestry check failed.' }
	}
	Assert-True ($eligibleRootCount -eq 1) 'Fixture must have exactly one eligible worktree root.'

	$sessionId = '22222222-2222-2222-2222-222222222222'
	Assert-True ($sessionId -cne $worktreeUuid) 'Fixture Codex SessionId must not equal the worktree UUID.'
	$startBeforeCutoff = $commitTimestamp.AddMinutes(-361)
	$coveringEnd = $commitTimestamp.AddMinutes(1)
	$bucket = Join-Path $sessions ($commitTimestamp.ToString('yyyy\\MM\\dd', [Globalization.CultureInfo]::InvariantCulture))
	$transcript = Join-Path $bucket "rollout-$($commitTimestamp.ToString('yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture))-$sessionId.jsonl"
	New-Transcript $transcript $sessionId $producing $startBeforeCutoff $coveringEnd $commitTimestamp

	$response = Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions
	Assert-Finder $response 0 'transcript.found'
	Assert-True ($response.Result.status -eq 'pass') 'Default lookup did not pass.'
	Assert-True ($response.Result.candidate.sessionId -eq $sessionId) 'Default lookup did not return the producing-session ID.'
	Assert-True ($response.Result.candidate.sessionStartUtc -eq $startBeforeCutoff.ToString('O')) 'Default lookup unexpectedly required start inside cutoff.'

	$response = Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions -SessionId $sessionId
	Assert-Finder $response 0 'transcript.found'
	Assert-True ($response.Result.candidate.sessionId -eq $sessionId) 'Exact SessionId lookup did not return the requested transcript.'

	Clear-SessionStores @($sessions, $archivedSessions)
	New-Transcript $transcript '23232323-2323-2323-2323-232323232323' $fixtureRoot $startBeforeCutoff $coveringEnd $commitTimestamp
	$script:FixtureStage = 'single eligible root ancestor candidate'
	Assert-Finder (Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions) 2 'transcript.not-found'

	Clear-SessionStores @($sessions, $archivedSessions)
	New-Transcript $transcript '24242424-2424-2424-2424-242424242424' (Split-Path -Leaf $producing) $startBeforeCutoff $coveringEnd $commitTimestamp
	$script:FixtureStage = 'relative eligible root leaf'
	Push-Location $fixtureRoot
	try {
		Assert-Finder (Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions) 2 'transcript.not-found'
	}
	finally { Pop-Location }

	$unrelated = Join-Path $fixtureRoot 'unrelated'
	& git init $unrelated | Out-Null
	if ($LASTEXITCODE -ne 0) { throw 'Unrelated repository initialization failed.' }
	$unregistered = Join-Path $producing 'unregistered'
	[void] (New-Item -ItemType Directory -Path $unregistered)

	Clear-SessionStores @($sessions, $archivedSessions)
	New-Transcript $transcript '33333333-3333-3333-3333-333333333333' $unrelated $startBeforeCutoff $coveringEnd $commitTimestamp
	Assert-Finder (Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions) 2 'transcript.not-found'

	Clear-SessionStores @($sessions, $archivedSessions)
	New-Transcript $transcript '44444444-4444-4444-4444-444444444444' $unregistered $startBeforeCutoff $coveringEnd $commitTimestamp
	Assert-Finder (Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions) 2 'transcript.not-found'

	Clear-SessionStores @($sessions, $archivedSessions)
	$cwdAlias = "$unregistered\.."
	New-Transcript $transcript '45454545-4545-4545-4545-454545454545' $cwdAlias $startBeforeCutoff $coveringEnd $commitTimestamp
	$script:FixtureStage = 'dot-segment cwd alias'
	Assert-Finder (Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions) 2 'transcript.not-found'

	Clear-SessionStores @($sessions, $archivedSessions)
	New-Transcript $transcript '55555555-5555-5555-5555-555555555555' $producing $commitTimestamp.AddMinutes(-2) $commitTimestamp.AddMinutes(-1) $commitTimestamp
	Assert-Finder (Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions) 2 'transcript.not-found'

	Clear-SessionStores @($sessions, $archivedSessions)
	New-Transcript $transcript '66666666-6666-6666-6666-666666666666' $producing $startBeforeCutoff $coveringEnd $commitTimestamp
	New-Transcript (Join-Path $bucket "rollout-$($commitTimestamp.ToString('yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture))-77777777-7777-7777-7777-777777777777.jsonl") '77777777-7777-7777-7777-777777777777' $producing $startBeforeCutoff $coveringEnd $commitTimestamp
	$script:FixtureStage = 'multiple root candidates'
	$response = Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions
	Assert-Finder $response 2 'transcript.needs-selection'
	Assert-True ($response.Result.status -eq 'needs-selection') 'Multiple root candidates did not return needs-selection.'
	Assert-True ($response.Result.candidates.Count -eq 2) 'Multiple root candidates were not both listed.'
	$candidateIds = @($response.Result.candidates | ForEach-Object { $_.sessionId })
	Assert-True ($candidateIds -contains '66666666-6666-6666-6666-666666666666' -and $candidateIds -contains '77777777-7777-7777-7777-777777777777') 'Root candidate list lost a session ID.'
	# Both fixture roots share a start, so this asserts the documented sessionId tiebreak.
	$orderKeys = @($response.Result.candidates | ForEach-Object { "$($_.sessionStartUtc)`0$($_.sessionId)" })
	Assert-True ((($orderKeys) -join '|') -ceq ((@($orderKeys | Sort-Object)) -join '|')) 'Root candidates were not ordered by session start then session ID.'
	foreach ($rootCandidate in $response.Result.candidates) {
		foreach ($evidenceField in @('startsBeforeAuthorUtc', 'commitHashMentions', 'descendantCount', 'descendants')) {
			Assert-True ($null -ne $rootCandidate.PSObject.Properties[$evidenceField]) "[$script:FixtureStage] Root candidate is missing evidence field $evidenceField."
		}
	}
	Assert-True (-not [string]::IsNullOrWhiteSpace($response.Result.commit.authoredUtc)) 'Result did not report the commit author timestamp.'

	Clear-SessionStores @($sessions, $archivedSessions)
	[void] (New-Item -ItemType Directory -Path $bucket -Force)
	$malformed = Join-Path $bucket "rollout-$($commitTimestamp.ToString('yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture))-malformed.jsonl"
	Set-Content -LiteralPath $malformed -Value '{ malformed' -Encoding utf8
	[IO.File]::SetLastWriteTimeUtc($malformed, $commitTimestamp.UtcDateTime)
	$script:FixtureStage = 'malformed transcript'
	Assert-Finder (Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions) 2 'transcript.read-error'

	Clear-SessionStores @($sessions, $archivedSessions)
	$reparseRoot = Join-Path $fixtureRoot 'reparse-store-root'
	if (New-Junction $reparseRoot $sessions) {
		$script:FixtureStage = 'reparse store root'
		Assert-Finder (Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions -OverrideSessionStoreRoot $reparseRoot) 2 'transcript.read-error'
	}

	Clear-SessionStores @($sessions, $archivedSessions)
	$outsideTranscript = Join-Path $fixtureRoot 'outside.jsonl'
	New-Transcript $outsideTranscript '88888888-8888-8888-8888-888888888888' $producing $startBeforeCutoff $coveringEnd $commitTimestamp
	$reparseFile = Join-Path $sessions 'rollout-reparse.jsonl'
	Assert-True (New-FileLink $reparseFile $outsideTranscript) 'Required selected-file reparse fixture could not be created.'
	$script:FixtureStage = 'reparse candidate file'
	Assert-Finder (Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions) 2 'transcript.read-error'

	Clear-SessionStores @($sessions, $archivedSessions)
	$validSessionId = '99999999-9999-9999-9999-999999999999'
	New-Transcript $transcript $validSessionId $producing $startBeforeCutoff $coveringEnd $commitTimestamp
	$staleReparseTarget = Join-Path $fixtureRoot 'stale-reparse-target'
	[void] (New-Item -ItemType Directory -Path $staleReparseTarget)
	$staleReparseParent = Join-Path $sessions '2000\01'
	[void] (New-Item -ItemType Directory -Path $staleReparseParent -Force)
	$staleReparse = Join-Path $staleReparseParent '01'
	if (New-Junction $staleReparse $staleReparseTarget) {
		$script:FixtureStage = 'unselected stale reparse directory'
		$response = Invoke-Finder -Finder $finder -Commit $commit -ReviewRoot $review -Sessions $sessions -ArchivedSessions $archivedSessions
		Assert-Finder $response 0 'transcript.found'
		Assert-True ($response.Result.candidate.sessionId -eq $validSessionId) 'Unselected stale reparse path blocked default discovery.'
	}

	Write-Output 'PASS: Find-AgentSessionTranscript fixture completed.'
}
finally {
	[Environment]::SetEnvironmentVariable('GIT_AUTHOR_DATE', $originalAuthorDate, 'Process')
	[Environment]::SetEnvironmentVariable('GIT_COMMITTER_DATE', $originalCommitterDate, 'Process')
	if ($primaryCreated) {
		if (Test-Path -LiteralPath $producing -PathType Container) { & git -C $primary worktree remove --force $producing 2>$null }
		if (Test-Path -LiteralPath $review -PathType Container) { & git -C $primary worktree remove --force $review 2>$null }
	}
	if ($fixtureRootFull.StartsWith($tempRootFull, [StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $fixtureRootFull).StartsWith('next-plan-review-transcript-fixture-', [StringComparison]::Ordinal)) {
		Remove-Item -LiteralPath $fixtureRootFull -Force -Recurse -ErrorAction SilentlyContinue
	}
}
