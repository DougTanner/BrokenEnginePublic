[CmdletBinding()]
param(
	[string] $Commit = 'HEAD',
	[string] $RepositoryRoot = (Get-Location).Path,
	[string] $SessionId,
	[ValidateRange(1, 1440)]
	[int] $WindowMinutes = 360,
	[string] $SessionStoreRoot,
	[string] $ArchivedSessionStoreRoot
)

$ErrorActionPreference = 'Stop'

function ConvertTo-UtcTimestamp([string] $Value) {
	if ([string]::IsNullOrWhiteSpace($Value)) { return $null }
	try {
		return ([DateTimeOffset]::Parse(
			$Value,
			[Globalization.CultureInfo]::InvariantCulture,
			[Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal
		)).ToUniversalTime()
	}
	catch { return $null }
}

function Get-StringProperty($Object, [string] $Name) {
	if ($null -eq $Object) { return $null }
	$property = $Object.PSObject.Properties[$Name]
	if ($null -eq $property) { return $null }
	return [string] $property.Value
}

function Invoke-Git([string[]] $Arguments) {
	$output = @(& git -C $RepositoryRoot @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw 'Git metadata query failed.' }
	return $output
}

function Test-PathWithin([string] $Candidate, [string] $Root) {
	try {
		$candidatePath = [IO.Path]::GetFullPath($Candidate).TrimEnd([char[]]@('\', '/'))
		$rootPath = [IO.Path]::GetFullPath($Root).TrimEnd([char[]]@('\', '/'))
	}
	catch { return $false }
	return $candidatePath.Equals($rootPath, [StringComparison]::OrdinalIgnoreCase) -or
		$candidatePath.StartsWith("$rootPath$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::OrdinalIgnoreCase)
}

function Get-Locator([string] $Path) {
	foreach ($store in @(
		@{ Name = 'sessions'; Root = $SessionStoreRoot },
		@{ Name = 'archived_sessions'; Root = $ArchivedSessionStoreRoot }
	)) {
		if (-not [string]::IsNullOrWhiteSpace($store.Root) -and (Test-PathWithin $Path $store.Root)) {
			$relative = [IO.Path]::GetRelativePath($store.Root, $Path).Replace('\', '/')
			return "$($store.Name)/$relative"
		}
	}
	return [IO.Path]::GetFileName($Path)
}

function Get-TranscriptMetadata([string] $Path) {
	$stream = [IO.FileStream]::new($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
	$reader = [IO.StreamReader]::new($stream)
	try {
		$start = $null
		$end = $null
		$cwd = $null
		$id = $null
		while (($line = $reader.ReadLine()) -ne $null) {
			if ([string]::IsNullOrWhiteSpace($line)) { continue }
			$record = $line | ConvertFrom-Json -Depth 64 -DateKind String
			$eventTime = ConvertTo-UtcTimestamp (Get-StringProperty $record 'timestamp')
			if ($null -ne $eventTime) {
				if ($null -eq $start -or $eventTime -lt $start) { $start = $eventTime }
				if ($null -eq $end -or $eventTime -gt $end) { $end = $eventTime }
			}
			if ($record.type -eq 'session_meta') {
				$cwd = Get-StringProperty $record.payload 'cwd'
				$id = Get-StringProperty $record.payload 'session_id'
				if ([string]::IsNullOrWhiteSpace($id)) { $id = Get-StringProperty $record.payload 'id' }
			}
		}
		if ($null -eq $start -or $null -eq $end -or [string]::IsNullOrWhiteSpace($cwd) -or [string]::IsNullOrWhiteSpace($id)) {
			throw 'Required session metadata is missing.'
		}
		return [pscustomobject]@{ SessionId = $id; Start = $start; End = $end; Cwd = $cwd }
	}
	finally {
		$reader.Dispose()
		$stream.Dispose()
	}
}

function Add-Files([Collections.Generic.Dictionary[string, IO.FileInfo]] $Files, [string] $Root, [string] $Filter, [switch] $Recurse) {
	if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path -LiteralPath $Root -PathType Container)) { return }
	$parameters = @{ LiteralPath = $Root; File = $true; Filter = $Filter; ErrorAction = 'Stop' }
	if ($Recurse) { $parameters.Recurse = $true }
	foreach ($file in Get-ChildItem @parameters) { $Files[$file.FullName] = $file }
}

function Write-Result($Result, [int] $ExitCode) {
	$Result | ConvertTo-Json -Depth 8
	$global:LASTEXITCODE = $ExitCode
	exit $ExitCode
}

try {
	$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
	$actualRoot = @(Invoke-Git @('rev-parse', '--show-toplevel'))[0].Trim()
	if (-not ([IO.Path]::GetFullPath($actualRoot)).Equals($RepositoryRoot, [StringComparison]::OrdinalIgnoreCase)) {
		throw 'RepositoryRoot is not the selected Git worktree root.'
	}
	$commitHash = @(Invoke-Git @('rev-parse', "$Commit^{commit}"))[0].Trim()
	$commitTimestamp = ConvertTo-UtcTimestamp (@(Invoke-Git @('show', '-s', '--format=%cI', $commitHash))[0].Trim())
	if ($null -eq $commitTimestamp) { throw 'Commit timestamp is invalid.' }
	if (-not [string]::IsNullOrWhiteSpace($SessionId) -and $SessionId -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') {
		throw 'SessionId must be an exact lowercase UUID.'
	}

	$userProfile = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
	if ([string]::IsNullOrWhiteSpace($SessionStoreRoot)) { $SessionStoreRoot = Join-Path $userProfile '.codex\sessions' }
	if ([string]::IsNullOrWhiteSpace($ArchivedSessionStoreRoot)) { $ArchivedSessionStoreRoot = Join-Path $userProfile '.codex\archived_sessions' }
	$SessionStoreRoot = [IO.Path]::GetFullPath($SessionStoreRoot)
	$ArchivedSessionStoreRoot = [IO.Path]::GetFullPath($ArchivedSessionStoreRoot)

	$files = [Collections.Generic.Dictionary[string, IO.FileInfo]]::new([StringComparer]::OrdinalIgnoreCase)
	$selection = if ([string]::IsNullOrWhiteSpace($SessionId)) { 'bounded-commit-window' } else { 'explicit-session-id' }
	$windowStart = $commitTimestamp.AddMinutes(-$WindowMinutes)
	$windowEnd = $commitTimestamp.AddMinutes($WindowMinutes)
	if ($selection -eq 'explicit-session-id') {
		Add-Files $files $SessionStoreRoot "*-$SessionId.jsonl" -Recurse
		Add-Files $files $ArchivedSessionStoreRoot "*-$SessionId.jsonl"
	}
	else {
		for ($date = $windowStart.Date; $date -le $windowEnd.Date; $date = $date.AddDays(1)) {
			$dateText = $date.ToString('yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture)
			$dateDirectory = Join-Path $SessionStoreRoot ($date.ToString('yyyy\\MM\\dd', [Globalization.CultureInfo]::InvariantCulture))
			Add-Files $files $dateDirectory 'rollout-*.jsonl'
			Add-Files $files $SessionStoreRoot "rollout-$dateText*.jsonl"
			Add-Files $files $ArchivedSessionStoreRoot "rollout-$dateText*.jsonl"
		}
	}

	$candidates = [Collections.Generic.List[object]]::new()
	$readErrors = [Collections.Generic.List[object]]::new()
	foreach ($file in $files.Values) {
		$locator = Get-Locator $file.FullName
		try { $metadata = Get-TranscriptMetadata $file.FullName }
		catch {
			$readErrors.Add([pscustomobject]@{ locator = $locator; code = 'transcript.read-failed'; message = 'Transcript metadata could not be read.' })
			continue
		}
		if ($selection -eq 'explicit-session-id' -and $metadata.SessionId -cne $SessionId) { continue }
		if (-not (Test-PathWithin $metadata.Cwd $RepositoryRoot)) { continue }
		if ($metadata.Start -gt $commitTimestamp -or $metadata.End -lt $commitTimestamp) { continue }
		if ($selection -eq 'bounded-commit-window' -and ($metadata.Start -lt $windowStart -or $metadata.Start -gt $windowEnd)) { continue }
		$candidates.Add([pscustomobject]@{
			client = 'codex'
			locator = $locator
			sessionId = $metadata.SessionId
			sessionStartUtc = $metadata.Start.ToString('O')
			sessionEndUtc = $metadata.End.ToString('O')
		})
	}

	$result = [ordered]@{
		schemaVersion = 'broken-engine-agent-session-transcript/v2'
		status = 'blocked'
		code = $null
		message = $null
		commit = [ordered]@{ hash = $commitHash; committedUtc = $commitTimestamp.ToString('O') }
		selection = [ordered]@{
			mode = $selection
			sessionId = if ($selection -eq 'explicit-session-id') { $SessionId } else { $null }
			windowStartUtc = if ($selection -eq 'bounded-commit-window') { $windowStart.ToString('O') } else { $null }
			windowEndUtc = if ($selection -eq 'bounded-commit-window') { $windowEnd.ToString('O') } else { $null }
		}
		candidate = $null
		candidates = @($candidates)
		readErrors = @($readErrors)
	}
	if ($readErrors.Count -ne 0) {
		$result.code = 'transcript.read-error'
		$result.message = 'One or more bounded transcript metadata reads failed.'
		Write-Result $result 2
	}
	if ($candidates.Count -eq 0) {
		$result.code = 'transcript.not-found'
		$result.message = 'No transcript matched the commit time and selected worktree.'
		Write-Result $result 2
	}
	if ($candidates.Count -ne 1) {
		$result.code = 'transcript.ambiguous'
		$result.message = 'Multiple transcripts matched; rerun with an exact SessionId.'
		Write-Result $result 2
	}
	$result.status = 'pass'
	$result.code = 'transcript.found'
	$result.message = 'One transcript matched the bounded metadata constraints.'
	$result.candidate = $candidates[0]
	$result.candidates = @()
	Write-Result $result 0
}
catch {
	Write-Result ([ordered]@{
		schemaVersion = 'broken-engine-agent-session-transcript/v2'
		status = 'blocked'
		code = 'finder.setup-error'
		message = 'Transcript finder setup or input validation failed.'
		commit = $null
		selection = $null
		candidate = $null
		candidates = @()
		readErrors = @()
	}) 2
}
