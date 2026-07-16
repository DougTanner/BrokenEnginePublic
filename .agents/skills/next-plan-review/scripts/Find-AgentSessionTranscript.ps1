[CmdletBinding()]
param(
	[string] $Commit = 'HEAD',
	[string] $RepositoryRoot = (Get-Location).Path
)

$ErrorActionPreference = 'Stop'

function Invoke-Git([string[]] $Arguments) {
	$output = @(& git -C $RepositoryRoot @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed: $($output -join '; ')" }
	return $output
}

function Get-StringProperty($Object, [string] $Name) {
	if ($null -eq $Object) { return $null }
	$property = $Object.PSObject.Properties[$Name]
	if ($null -eq $property) { return $null }
	return [string] $property.Value
}

function ConvertTo-UtcTimestamp([string] $Value) {
	if ([string]::IsNullOrWhiteSpace($Value)) { return $null }
	try {
		$parsed = [DateTimeOffset]::Parse(
			$Value,
			[Globalization.CultureInfo]::InvariantCulture,
			[Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal
		)
		return $parsed.ToUniversalTime()
	}
	catch { return $null }
}

function Get-ToolInputTexts($Payload) {
	$texts = [System.Collections.Generic.List[string]]::new()
	foreach ($propertyName in @('input', 'arguments')) {
		$value = Get-StringProperty $Payload $propertyName
		if ([string]::IsNullOrWhiteSpace($value)) { continue }
		$texts.Add($value)
		try {
			$decoded = $value | ConvertFrom-Json -Depth 32 -DateKind String
			foreach ($nestedName in @('command', 'script')) {
				$nestedValue = Get-StringProperty $decoded $nestedName
				if (-not [string]::IsNullOrWhiteSpace($nestedValue)) { $texts.Add($nestedValue) }
			}
		}
		catch {}
	}
	return $texts.ToArray()
}

function Get-TranscriptLinesShared([string] $Path) {
	$stream = [IO.FileStream]::new($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
	$reader = [IO.StreamReader]::new($stream)
	try {
		while (($line = $reader.ReadLine()) -ne $null) {
			Write-Output $line
		}
	}
	finally {
		$reader.Dispose()
		$stream.Dispose()
	}
}

$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$commitHash = @(Invoke-Git @('rev-parse', "$Commit^{commit}"))[0].Trim()
$shortHash = @(Invoke-Git @('rev-parse', '--short=12', $commitHash))[0].Trim()
$commitSubject = @(Invoke-Git @('show', '-s', '--format=%s', $commitHash))[0].Trim()
$commitTimestamp = ConvertTo-UtcTimestamp (@(Invoke-Git @('show', '-s', '--format=%cI', $commitHash))[0].Trim())
$containingBranches = @(Invoke-Git @('branch', '--all', '--contains', $commitHash) | ForEach-Object { $_.Trim().TrimStart([char[]]@('*', '+')).Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })

$userProfile = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
$roots = @(
	(Join-Path $userProfile '.codex\sessions'),
	(Join-Path $userProfile '.codex\archived_sessions')
)
$escapedHash = [regex]::Escape($commitHash)
$escapedShortHash = [regex]::Escape($shortHash)
$escapedSubject = [regex]::Escape($commitSubject)
$candidates = [System.Collections.Generic.List[object]]::new()

foreach ($root in $roots) {
	if (-not (Test-Path -LiteralPath $root)) { continue }
	$rgArguments = @('--files-with-matches', '--ignore-case', '--fixed-strings', '--glob', '*.jsonl', '-e', $commitHash, '-e', $shortHash)
	if (-not [string]::IsNullOrWhiteSpace($commitSubject)) { $rgArguments += @('-e', $commitSubject) }
	$rgArguments += @('--', $root)
	$matchingPaths = @(& rg @rgArguments 2>$null)
	$rgExitCode = $LASTEXITCODE
	if ($rgExitCode -gt 1) { throw "rg Codex transcript search failed beneath '$root' with exit code $rgExitCode." }

	foreach ($matchingPath in $matchingPaths) {
		$hasFullHash = $false
		$hasShortHash = $false
		$hasSubject = $false
		$hasCommitCommand = $false
		$hasLandingCommand = $false
		$sessionStart = $null
		$sessionEnd = $null
		$sessionCwd = $null
		$sessionBranch = $null
		$sessionId = $null
		try {
			foreach ($line in Get-TranscriptLinesShared $matchingPath) {
				if (-not $hasFullHash) { $hasFullHash = $line.IndexOf($commitHash, [StringComparison]::OrdinalIgnoreCase) -ge 0 }
				if (-not $hasShortHash) { $hasShortHash = $line.IndexOf($shortHash, [StringComparison]::OrdinalIgnoreCase) -ge 0 }
				if (-not $hasSubject) { $hasSubject = (-not [string]::IsNullOrWhiteSpace($commitSubject)) -and ($line.IndexOf($commitSubject, [StringComparison]::OrdinalIgnoreCase) -ge 0) }
				try { $record = $line | ConvertFrom-Json -Depth 64 -DateKind String }
				catch { continue }
				$eventTime = ConvertTo-UtcTimestamp (Get-StringProperty $record 'timestamp')
				if ($null -ne $eventTime) {
					if ($null -eq $sessionStart -or $eventTime -lt $sessionStart) { $sessionStart = $eventTime }
					if ($null -eq $sessionEnd -or $eventTime -gt $sessionEnd) { $sessionEnd = $eventTime }
				}
				$payload = $record.payload
				if ($record.type -eq 'session_meta') {
					$sessionCwd = Get-StringProperty $payload 'cwd'
					$sessionId = Get-StringProperty $payload 'session_id'
					if ([string]::IsNullOrWhiteSpace($sessionId)) { $sessionId = Get-StringProperty $payload 'id' }
					if ($null -ne $payload -and $null -ne $payload.git) { $sessionBranch = Get-StringProperty $payload.git 'branch' }
				}
				foreach ($toolText in Get-ToolInputTexts $payload) {
					if ($toolText -match '\bgit(?:\s+-C\s+(?:"[^"]*"|\S+))?\s+commit\b') { $hasCommitCommand = $true }
					if ($toolText -match 'Invoke-FinalizeLanding\.ps1|\bgit(?:\s+-C\s+(?:"[^"]*"|\S+))?\s+rebase\b') { $hasLandingCommand = $true }
				}
			}
		}
		catch { continue }

		$spansCommit = ($null -ne $sessionStart) -and ($null -ne $sessionEnd) -and ($sessionStart -le $commitTimestamp) -and ($sessionEnd -ge $commitTimestamp)
		$isCandidate = $spansCommit -and (($hasFullHash -or $hasShortHash) -or ($hasSubject -and ($hasCommitCommand -or $hasLandingCommand)))
		if (-not $isCandidate) { continue }

		$evidence = [System.Collections.Generic.List[string]]::new()
		$score = 0
		if ($hasFullHash) { $evidence.Add('contains full commit hash'); $score += 4 }
		if ($hasShortHash) { $evidence.Add('contains short commit hash'); $score += 2 }
		if ($hasSubject) { $evidence.Add('contains commit subject'); $score += 2 }
		if ($hasCommitCommand) { $evidence.Add('contains decoded git commit command'); $score += 3 }
		if ($hasLandingCommand) { $evidence.Add('contains decoded landing or rebase command'); $score += 2 }
		if ($spansCommit) { $evidence.Add('session time spans commit timestamp'); $score += 3 }
		if ($null -ne $sessionBranch -and $containingBranches -contains $sessionBranch) { $evidence.Add('session branch contains commit'); $score += 2 }
		$candidates.Add([pscustomobject]@{
			client = 'codex'
			path = $matchingPath
			sessionId = $sessionId
			score = $score
			sessionStartUtc = if ($null -eq $sessionStart) { $null } else { $sessionStart.ToString('O') }
			sessionEndUtc = if ($null -eq $sessionEnd) { $null } else { $sessionEnd.ToString('O') }
			cwd = $sessionCwd
			branch = $sessionBranch
			evidence = @($evidence)
		})
	}
}

[pscustomobject]@{
	schema = 'broken-engine-codex-transcript-candidates/v1'
	commit = [pscustomobject]@{
		hash = $commitHash
		shortHash = $shortHash
		subject = $commitSubject
		committedUtc = $commitTimestamp.ToString('O')
		containingBranches = @($containingBranches)
	}
	searchRoots = @($roots | Where-Object { Test-Path -LiteralPath $_ })
	candidates = @($candidates | Sort-Object @{ Expression = 'score'; Descending = $true }, @{ Expression = 'sessionStartUtc'; Descending = $false }, path)
} | ConvertTo-Json -Depth 8
$global:LASTEXITCODE = 0
