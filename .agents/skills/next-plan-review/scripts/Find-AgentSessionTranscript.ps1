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

function ConvertTo-LexicalCanonicalPath([string] $Path) {
	if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
	try {
		$fullPath = [IO.Path]::GetFullPath($Path).Replace([IO.Path]::AltDirectorySeparatorChar, [IO.Path]::DirectorySeparatorChar)
		$root = [IO.Path]::GetPathRoot($fullPath)
		if ([string]::IsNullOrWhiteSpace($root)) { return $null }
		if ($fullPath.Length -gt $root.Length) {
			$fullPath = $fullPath.TrimEnd([char[]]@('\', '/'))
		}
		return $fullPath
	}
	catch { return $null }
}

function Get-CanonicalMetadataCwd([string] $Path) {
	if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
	if (-not [IO.Path]::IsPathFullyQualified($Path)) { return $null }
	foreach ($segment in $Path.Split([char[]]@('\', '/'), [StringSplitOptions]::RemoveEmptyEntries)) {
		if ($segment -eq '.' -or $segment -eq '..') { return $null }
	}
	return ConvertTo-LexicalCanonicalPath $Path
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

function Invoke-GitRaw([string[]] $Arguments) {
	$startInfo = [Diagnostics.ProcessStartInfo]::new()
	$startInfo.FileName = 'git'
	$startInfo.UseShellExecute = $false
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	[void] $startInfo.ArgumentList.Add('-C')
	[void] $startInfo.ArgumentList.Add($RepositoryRoot)
	foreach ($argument in $Arguments) { [void] $startInfo.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $startInfo
	[void] $process.Start()
	$output = $process.StandardOutput.ReadToEnd()
	$errorOutput = $process.StandardError.ReadToEnd()
	$process.WaitForExit()
	if ($process.ExitCode -ne 0) { throw 'Git metadata query failed.' }
	return $output
}

function Test-CommitContainedBy([string] $CommitHash, [string] $HeadHash) {
	if ($CommitHash -ceq $HeadHash) { return $true }
	$output = @(& git -C $RepositoryRoot merge-base --is-ancestor $CommitHash $HeadHash 2>&1)
	if ($LASTEXITCODE -eq 0) { return $true }
	if ($LASTEXITCODE -eq 1) { return $false }
	throw 'Git ancestry query failed.'
}

function Get-EligibleWorktreeRoots([string] $CommitHash, [string] $CommonDirectory) {
	# `git worktree list` is queried from the selected checkout, so every record is registered in its common directory.
	if ([string]::IsNullOrWhiteSpace($CommonDirectory)) { throw 'Git common directory is invalid.' }
	$raw = Invoke-GitRaw @('worktree', 'list', '--porcelain', '-z')
	$records = [Collections.Generic.List[object]]::new()
	$current = $null
	foreach ($entry in $raw.Split([char] 0)) {
		if ([string]::IsNullOrEmpty($entry)) { continue }
		$separator = $entry.IndexOf(' ')
		$field = if ($separator -lt 0) { $entry } else { $entry.Substring(0, $separator) }
		$value = if ($separator -lt 0) { $null } else { $entry.Substring($separator + 1) }
		if ($field -eq 'worktree') {
			if ($null -ne $current) { $records.Add([pscustomobject] $current) }
			$current = [ordered]@{ Path = $value; Head = $null; Bare = $false; Prunable = $false }
			continue
		}
		if ($null -eq $current) { throw 'Git worktree metadata is malformed.' }
		switch ($field) {
			'HEAD' { $current.Head = $value }
			'bare' { $current.Bare = $true }
			'prunable' { $current.Prunable = $true }
		}
	}
	if ($null -ne $current) { $records.Add([pscustomobject] $current) }

	$roots = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
	foreach ($record in $records) {
		if ($record.Bare -or $record.Prunable -or [string]::IsNullOrWhiteSpace($record.Path) -or [string]::IsNullOrWhiteSpace($record.Head)) { continue }
		$root = ConvertTo-LexicalCanonicalPath $record.Path
		if ($null -eq $root) { throw 'Git worktree metadata is malformed.' }
		if (Test-CommitContainedBy $CommitHash $record.Head) { [void] $roots.Add($root) }
	}
	return (, $roots)
}

function Test-PathWithin([string] $Candidate, [string] $Root) {
	$candidatePath = ConvertTo-LexicalCanonicalPath $Candidate
	$rootPath = ConvertTo-LexicalCanonicalPath $Root
	if ($null -eq $candidatePath -or $null -eq $rootPath) { return $false }
	$prefix = if ($rootPath.EndsWith([IO.Path]::DirectorySeparatorChar)) { $rootPath } else { "$rootPath$([IO.Path]::DirectorySeparatorChar)" }
	return $candidatePath.Equals($rootPath, [StringComparison]::OrdinalIgnoreCase) -or
		$candidatePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
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

function Get-PathSafety([string] $Path) {
	$fullPath = ConvertTo-LexicalCanonicalPath $Path
	if ($null -eq $fullPath) { return [pscustomobject]@{ Status = 'error'; Item = $null; Path = $null } }
	$root = [IO.Path]::GetPathRoot($fullPath)
	$segments = $fullPath.Substring($root.Length).Split([char[]]@('\', '/'), [StringSplitOptions]::RemoveEmptyEntries)
	$current = $root
	foreach ($segment in @('') + $segments) {
		if ($segment.Length -ne 0) { $current = Join-Path $current $segment }
		try { $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop }
		catch {
			if ($_.Exception -is [Management.Automation.ItemNotFoundException]) {
				return [pscustomobject]@{ Status = 'missing'; Item = $null; Path = $fullPath }
			}
			return [pscustomobject]@{ Status = 'error'; Item = $null; Path = $fullPath }
		}
		if (([IO.FileAttributes] $item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
			return [pscustomobject]@{ Status = 'unsafe'; Item = $null; Path = $fullPath }
		}
	}
	return [pscustomobject]@{ Status = 'safe'; Item = $item; Path = $fullPath }
}

function Add-ReadError([Collections.Generic.Dictionary[string, object]] $ReadErrors, [string] $Locator, [string] $Code, [string] $Message) {
	$key = "$Code`0$Locator"
	if (-not $ReadErrors.ContainsKey($key)) {
		$ReadErrors[$key] = [pscustomobject]@{ locator = $Locator; code = $Code; message = $Message }
	}
}

function Get-SafeStoreRoot([string] $Name, [string] $Root, [Collections.Generic.Dictionary[string, object]] $ReadErrors) {
	$safety = Get-PathSafety $Root
	if ($safety.Status -eq 'missing') { return $null }
	if ($safety.Status -ne 'safe' -or -not $safety.Item.PSIsContainer) {
		$code = if ($safety.Status -eq 'unsafe') { 'transcript.unsafe-path' } else { 'transcript.read-failed' }
		Add-ReadError $ReadErrors $Name $code 'Transcript store path could not be safely read.'
		return $null
	}
	return $safety.Path
}

function Get-ThreadSpawn($Payload) {
	# `payload.source` is a plain string ('cli', 'vscode') on root sessions, so every hop is probed
	# rather than dereferenced; chained access would fail under the caller's strict mode.
	$source = $Payload.PSObject.Properties['source']
	if ($null -eq $source -or $null -eq $source.Value -or $source.Value -is [string]) { return $null }
	$subagent = $source.Value.PSObject.Properties['subagent']
	if ($null -eq $subagent -or $null -eq $subagent.Value) { return $null }
	$threadSpawn = $subagent.Value.PSObject.Properties['thread_spawn']
	if ($null -eq $threadSpawn) { return $null }
	return $threadSpawn.Value
}

function Get-TranscriptMetadata([string] $Path, [string] $CommitHash) {
	$stream = [IO.FileStream]::new($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
	$reader = [IO.StreamReader]::new($stream)
	try {
		$start = $null
		$end = $null
		$cwd = $null
		$transcriptId = $null
		$rootSessionId = $null
		$depth = $null
		$agentPath = $null
		$commitHashMentions = 0
		while (($line = $reader.ReadLine()) -ne $null) {
			if ([string]::IsNullOrWhiteSpace($line)) { continue }
			if ($line.Contains($CommitHash, [StringComparison]::OrdinalIgnoreCase)) { $commitHashMentions++ }
			$record = $line | ConvertFrom-Json -Depth 64 -DateKind String
			$eventTime = ConvertTo-UtcTimestamp (Get-StringProperty $record 'timestamp')
			if ($null -ne $eventTime) {
				if ($null -eq $start -or $eventTime -lt $start) { $start = $eventTime }
				if ($null -eq $end -or $eventTime -gt $end) { $end = $eventTime }
			}
			if ($record.type -eq 'session_meta') {
				$cwd = Get-StringProperty $record.payload 'cwd'
				# `payload.session_id` is the root thread id, not this transcript's own id, so the two
				# are kept apart: identity comes from `id`, and their relationship classifies the record.
				$transcriptId = Get-StringProperty $record.payload 'id'
				$rootSessionId = Get-StringProperty $record.payload 'session_id'
				$threadSpawn = Get-ThreadSpawn $record.payload
				if ($null -ne $threadSpawn) {
					$agentPath = Get-StringProperty $threadSpawn 'agent_path'
					$depthProperty = $threadSpawn.PSObject.Properties['depth']
					if ($null -ne $depthProperty) { $depth = $depthProperty.Value }
				}
			}
		}
		$id = if ([string]::IsNullOrWhiteSpace($transcriptId)) { $rootSessionId } else { $transcriptId }
		if ($null -eq $start -or $null -eq $end -or [string]::IsNullOrWhiteSpace($cwd) -or [string]::IsNullOrWhiteSpace($id)) {
			throw 'Required session metadata is missing.'
		}
		return [pscustomobject]@{
			SessionId = $id
			RootSessionId = $rootSessionId
			IsRoot = [string]::IsNullOrWhiteSpace($rootSessionId) -or $rootSessionId -ceq $id
			Depth = $depth
			AgentPath = $agentPath
			CommitHashMentions = $commitHashMentions
			Start = $start
			End = $end
			Cwd = $cwd
		}
	}
	finally {
		$reader.Dispose()
		$stream.Dispose()
	}
}

function Add-Files(
	[Collections.Generic.Dictionary[string, IO.FileInfo]] $Files,
	[Collections.Generic.Dictionary[string, object]] $ReadErrors,
	[string] $Root,
	[string] $Filter,
	[switch] $Recurse,
	[scriptblock] $Include
) {
	if ([string]::IsNullOrWhiteSpace($Root)) { return }
	$rootSafety = Get-PathSafety $Root
	if ($rootSafety.Status -eq 'missing') { return }
	if ($rootSafety.Status -ne 'safe' -or -not $rootSafety.Item.PSIsContainer) {
		$code = if ($rootSafety.Status -eq 'unsafe') { 'transcript.unsafe-path' } else { 'transcript.read-failed' }
		Add-ReadError $ReadErrors (Get-Locator $Root) $code 'Transcript store path could not be safely read.'
		return
	}
	$Root = $rootSafety.Path
	$directories = [Collections.Generic.Stack[string]]::new()
	$directories.Push($Root)
	while ($directories.Count -ne 0) {
		$directory = $directories.Pop()
		try { $entries = @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop) }
		catch {
			Add-ReadError $ReadErrors (Get-Locator $directory) 'transcript.read-failed' 'Transcript store contents could not be read.'
			continue
		}
		foreach ($entry in $entries) {
			$isReparse = (([IO.FileAttributes] $entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
			if ($entry.PSIsContainer) {
				if (-not $isReparse -and $Recurse) { $directories.Push($entry.FullName) }
				continue
			}
			if ($entry.Name -notlike $Filter) { continue }
			if ($null -ne $Include -and -not (& $Include $entry)) { continue }
			if ($isReparse) {
				Add-ReadError $ReadErrors (Get-Locator $entry.FullName) 'transcript.unsafe-path' 'Transcript candidate is an unsafe reparse path.'
				continue
			}
			$safety = Get-PathSafety $entry.FullName
			if ($safety.Status -ne 'safe') {
				$code = if ($safety.Status -eq 'unsafe') { 'transcript.unsafe-path' } else { 'transcript.read-failed' }
				Add-ReadError $ReadErrors (Get-Locator $entry.FullName) $code 'Transcript candidate path could not be safely read.'
				continue
			}
			$Files[$safety.Path] = $entry
		}
	}
}

function Write-Result($Result, [int] $ExitCode) {
	$Result | ConvertTo-Json -Depth 8
	$global:LASTEXITCODE = $ExitCode
	exit $ExitCode
}

try {
	$RepositoryRoot = ConvertTo-LexicalCanonicalPath $RepositoryRoot
	if ($null -eq $RepositoryRoot) { throw 'RepositoryRoot is invalid.' }
	$actualRoot = ConvertTo-LexicalCanonicalPath (@(Invoke-Git @('rev-parse', '--show-toplevel'))[0].Trim())
	if ($null -eq $actualRoot -or -not $actualRoot.Equals($RepositoryRoot, [StringComparison]::OrdinalIgnoreCase)) {
		throw 'RepositoryRoot is not the selected Git worktree root.'
	}
	$commonDirectory = ConvertTo-LexicalCanonicalPath (@(Invoke-Git @('rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
	if ($null -eq $commonDirectory) { throw 'Git common directory is invalid.' }
	$commitHash = @(Invoke-Git @('rev-parse', "$Commit^{commit}"))[0].Trim()
	$commitDates = @(Invoke-Git @('show', '-s', '--format=%aI%n%cI', $commitHash))
	$authorTimestamp = ConvertTo-UtcTimestamp $commitDates[0].Trim()
	$commitTimestamp = ConvertTo-UtcTimestamp $commitDates[1].Trim()
	if ($null -eq $authorTimestamp -or $null -eq $commitTimestamp) { throw 'Commit timestamp is invalid.' }
	if (-not [string]::IsNullOrWhiteSpace($SessionId) -and $SessionId -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$') {
		throw 'SessionId must be an exact lowercase UUID.'
	}
	$eligibleWorktreeRoots = Get-EligibleWorktreeRoots $commitHash $commonDirectory

	$userProfile = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
	if ([string]::IsNullOrWhiteSpace($SessionStoreRoot)) { $SessionStoreRoot = Join-Path $userProfile '.codex\sessions' }
	if ([string]::IsNullOrWhiteSpace($ArchivedSessionStoreRoot)) { $ArchivedSessionStoreRoot = Join-Path $userProfile '.codex\archived_sessions' }
	$SessionStoreRoot = ConvertTo-LexicalCanonicalPath $SessionStoreRoot
	$ArchivedSessionStoreRoot = ConvertTo-LexicalCanonicalPath $ArchivedSessionStoreRoot
	if ($null -eq $SessionStoreRoot -or $null -eq $ArchivedSessionStoreRoot) { throw 'Transcript store path is invalid.' }

	$files = [Collections.Generic.Dictionary[string, IO.FileInfo]]::new([StringComparer]::OrdinalIgnoreCase)
	$readErrors = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
	$SessionStoreRoot = Get-SafeStoreRoot 'sessions' $SessionStoreRoot $readErrors
	$ArchivedSessionStoreRoot = Get-SafeStoreRoot 'archived_sessions' $ArchivedSessionStoreRoot $readErrors
	$selection = if ([string]::IsNullOrWhiteSpace($SessionId)) { 'bounded-commit-window' } else { 'explicit-session-id' }
	$windowStart = $commitTimestamp.AddMinutes(-$WindowMinutes)
	$windowEnd = $commitTimestamp.AddMinutes($WindowMinutes)
	if ($selection -eq 'explicit-session-id') {
		Add-Files -Files $files -ReadErrors $readErrors -Root $SessionStoreRoot -Filter "*-$SessionId.jsonl" -Recurse
		Add-Files -Files $files -ReadErrors $readErrors -Root $ArchivedSessionStoreRoot -Filter "*-$SessionId.jsonl" -Recurse
	}
	else {
		for ($date = $windowStart.Date; $date -le $windowEnd.Date; $date = $date.AddDays(1)) {
			$dateText = $date.ToString('yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture)
			$dateDirectory = if ([string]::IsNullOrWhiteSpace($SessionStoreRoot)) { $null } else { Join-Path $SessionStoreRoot ($date.ToString('yyyy\\MM\\dd', [Globalization.CultureInfo]::InvariantCulture)) }
			Add-Files -Files $files -ReadErrors $readErrors -Root $dateDirectory -Filter 'rollout-*.jsonl'
			Add-Files -Files $files -ReadErrors $readErrors -Root $SessionStoreRoot -Filter "rollout-$dateText*.jsonl"
			Add-Files -Files $files -ReadErrors $readErrors -Root $ArchivedSessionStoreRoot -Filter "rollout-$dateText*.jsonl" -Recurse
		}
		$includeLastWrite = {
			param($File)
			return $File.LastWriteTimeUtc -ge $windowStart.UtcDateTime -and $File.LastWriteTimeUtc -le $windowEnd.UtcDateTime
		}
		Add-Files -Files $files -ReadErrors $readErrors -Root $SessionStoreRoot -Filter '*.jsonl' -Recurse -Include $includeLastWrite
		Add-Files -Files $files -ReadErrors $readErrors -Root $ArchivedSessionStoreRoot -Filter '*.jsonl' -Recurse -Include $includeLastWrite
	}

	$records = [Collections.Generic.List[object]]::new()
	foreach ($file in @($files.Values | Sort-Object FullName)) {
		$locator = Get-Locator $file.FullName
		try { $metadata = Get-TranscriptMetadata $file.FullName $commitHash }
		catch {
			Add-ReadError $readErrors $locator 'transcript.read-failed' 'Transcript metadata could not be read.'
			continue
		}
		$records.Add([pscustomobject]@{ Locator = $locator; Metadata = $metadata })
	}

	# Descendants are discovery metadata drawn from the already-bounded file set, so they carry
	# neither the eligible-worktree nor the commit-covering constraint that gates candidacy.
	$descendantsByRoot = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
	if ($selection -eq 'bounded-commit-window') {
		foreach ($record in $records) {
			$metadata = $record.Metadata
			if ($metadata.IsRoot -or [string]::IsNullOrWhiteSpace($metadata.RootSessionId)) { continue }
			if (-not $descendantsByRoot.ContainsKey($metadata.RootSessionId)) {
				$descendantsByRoot[$metadata.RootSessionId] = [Collections.Generic.List[object]]::new()
			}
			$descendantsByRoot[$metadata.RootSessionId].Add([pscustomobject]@{
				sessionId = $metadata.SessionId
				locator = $record.Locator
				sessionStartUtc = $metadata.Start.ToString('O')
				sessionEndUtc = $metadata.End.ToString('O')
				agentPath = $metadata.AgentPath
				depth = $metadata.Depth
			})
		}
	}

	$candidates = [Collections.Generic.List[object]]::new()
	foreach ($record in $records) {
		$metadata = $record.Metadata
		if ($selection -eq 'explicit-session-id') {
			# The caller named one exact transcript, so it is returned whether root or descendant.
			if ($metadata.SessionId -cne $SessionId) { continue }
		}
		elseif (-not $metadata.IsRoot) { continue }
		$metadataRoot = Get-CanonicalMetadataCwd $metadata.Cwd
		if ($null -eq $metadataRoot -or -not $eligibleWorktreeRoots.Contains($metadataRoot)) { continue }
		if ($metadata.Start -gt $commitTimestamp -or $metadata.End -lt $commitTimestamp) { continue }
		$descendants = $null
		if ($selection -eq 'bounded-commit-window') {
			$descendants = @()
			if ($descendantsByRoot.ContainsKey($metadata.SessionId)) {
				$descendants = @($descendantsByRoot[$metadata.SessionId] | Sort-Object sessionStartUtc, sessionId)
			}
		}
		$candidates.Add([pscustomobject]@{
			client = 'codex'
			locator = $record.Locator
			sessionId = $metadata.SessionId
			sessionStartUtc = $metadata.Start.ToString('O')
			sessionEndUtc = $metadata.End.ToString('O')
			startsBeforeAuthorUtc = ($metadata.Start -lt $authorTimestamp)
			commitHashMentions = $metadata.CommitHashMentions
			descendantCount = if ($null -eq $descendants) { $null } else { $descendants.Count }
			descendants = $descendants
		})
	}

	$orderedReadErrors = @($readErrors.Values | Sort-Object locator, code)
	# Ordering is presentational only. Round-trip 'O' formatting of a UTC-adjusted DateTimeOffset is
	# fixed width, so the lexical sort is chronological; the evidence fields never order candidates.
	$orderedCandidates = @($candidates | Sort-Object sessionStartUtc, sessionId)
	$result = [ordered]@{
		schemaVersion = 'broken-engine-agent-session-transcript/v2'
		status = 'blocked'
		code = $null
		message = $null
		commit = [ordered]@{ hash = $commitHash; authoredUtc = $authorTimestamp.ToString('O'); committedUtc = $commitTimestamp.ToString('O') }
		selection = [ordered]@{
			mode = $selection
			sessionId = if ($selection -eq 'explicit-session-id') { $SessionId } else { $null }
			windowStartUtc = if ($selection -eq 'bounded-commit-window') { $windowStart.ToString('O') } else { $null }
			windowEndUtc = if ($selection -eq 'bounded-commit-window') { $windowEnd.ToString('O') } else { $null }
		}
		candidate = $null
		candidates = $orderedCandidates
		readErrors = $orderedReadErrors
	}
	if ($orderedReadErrors.Count -ne 0) {
		$result.code = 'transcript.read-error'
		$result.message = 'One or more bounded transcript metadata reads failed.'
		Write-Result $result 2
	}
	if ($orderedCandidates.Count -eq 0) {
		$result.code = 'transcript.not-found'
		$result.message = 'No transcript matched the commit time and an eligible retained worktree.'
		Write-Result $result 2
	}
	if ($orderedCandidates.Count -ne 1) {
		$result.status = 'needs-selection'
		$result.code = 'transcript.needs-selection'
		$result.message = 'Multiple root transcripts matched; select one from the reported evidence and prove production.'
		Write-Result $result 2
	}
	$result.status = 'pass'
	$result.code = 'transcript.found'
	$result.message = 'One transcript matched the bounded metadata constraints.'
	$result.candidate = $orderedCandidates[0]
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
