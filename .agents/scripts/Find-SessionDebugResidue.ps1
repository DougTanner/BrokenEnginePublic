# Added-lines-only debug residue scanner for the session cleanup step of /code-style-review: it reports
# candidate temporary instrumentation and navigation comments on lines this session added, so a cleanup
# never has to separate them from pre-existing intentional debug logs by hand. The scan reports
# candidates only — it never decides whether a hit is temporary or intentional, never edits a file, and
# writes nothing to disk (GIT_OPTIONAL_LOCKS=0 keeps Git from refreshing the index), so it is safe under
# a read-only sandbox. Stdout carries only the result document.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $RepositoryRoot,
	[Parameter(Mandatory)][string] $Baseline,
	[string] $Head,
	[switch] $IncludeUntracked
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1') -Force

$script:MaximumHits = 400
# The regions cap of Get-SessionChangeInventory.ps1: an emitted row count at the cap means the scan
# may have missed added lines the inventory dropped, so the result reports itself truncated. The
# inventory also sheds region rows below that cap to fit its own stdout budget, so a full count above
# the emitted count means the same thing.
$script:InventoryRegionCap = 400
$script:MaximumTextLength = 200
$script:MaximumMessageLength = 256
$script:MaximumOutputBytes = 131072

$script:InventoryScript = Join-Path $PSScriptRoot 'Get-SessionChangeInventory.ps1'
$script:CppClasses = @('cpp', 'dual-language-header')
# One entry per candidate kind listed in .agents/skills/code-style-review/SKILL.md "Session Cleanup",
# in the order a line is attributed: a line reports the first kind that matches it.
$script:ResiduePatterns = @(
	@{ Kind = 'log'; Pattern = '\bLOG\s*\(' }
	@{ Kind = 'printf'; Pattern = '\bprintf\s*\(' }
	@{ Kind = 'debug-break'; Pattern = '\bDEBUG_BREAK\s*\(\s*\)' }
	# The engine spells the macro ASSERT, so the always-failing assertion is matched without case.
	@{ Kind = 'assert-false'; Pattern = '(?i)\bassert\s*\(\s*false\s*\)' }
	@{ Kind = 'fixme'; Pattern = '(?://|/\*|^\s*\*).*\bFIXME\b' }
	@{ Kind = 'hack'; Pattern = '(?://|/\*|^\s*\*).*\bHACK\b' }
	@{ Kind = 'navigation-comment'; Pattern = '(?://|/\*|^\s*\*).*\b(?:AGENTS|CLAUDE)\.md\b' }
)
$script:Utf8 = [Text.UTF8Encoding]::new($false)
$script:Root = $null
$script:HeadSha = ''
$script:NewSideLines = @{}

$result = [ordered]@{
	schemaVersion = 'broken-engine-session-debug-residue/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Session debug residue scan did not run.'
	hits = @()
	counts = $null
	truncated = $false
}

function Complete-SessionDebugResidue([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = if ($Message.Length -gt $script:MaximumMessageLength) { $Message.Substring(0, $script:MaximumMessageLength) } else { $Message }
	$stream = [Console]::OpenStandardOutput()
	$bytes = $script:Utf8.GetBytes(($result | ConvertTo-Json -Depth 32 -Compress))
	$stream.Write($bytes, 0, $bytes.Length)
	$stream.Flush()
	exit $ExitCode
}

function Invoke-ResidueProcess([string] $FileName, [string[]] $Arguments, [string] $WorkingDirectory) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $FileName
	$start.WorkingDirectory = $WorkingDirectory
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = $script:Utf8
	$start.StandardErrorEncoding = $script:Utf8
	$start.Environment['GIT_OPTIONAL_LOCKS'] = '0'
	foreach ($argument in $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start $FileName with: $($Arguments -join ' ')" }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$run = [pscustomobject] @{
		ExitCode = $process.ExitCode
		Stdout = $stdoutTask.GetAwaiter().GetResult()
		Stderr = $stderrTask.GetAwaiter().GetResult()
	}
	$process.Dispose()
	return $run
}

function Invoke-ResidueGit([string[]] $Arguments) {
	$run = Invoke-ResidueProcess 'git' (@('-C', $script:Root, '--no-pager') + $Arguments) $script:Root
	if ($run.ExitCode -ne 0) { throw "git $($Arguments -join ' ') failed with exit $($run.ExitCode): $($run.Stderr.Trim())" }
	return $run.Stdout
}

function Get-NewSideLine([string] $Path) {
	# Each return wraps the cached array so a one-line file stays an array of one line instead of
	# unrolling to a bare string, whose indexer would hand back a single character.
	if ($script:NewSideLines.ContainsKey($Path)) { return , $script:NewSideLines[$Path] }
	# The added lines belong to the head side of the compared diff: a commit-valued head reads its blob,
	# and a working-tree head (including an untracked addition) reads the file itself.
	$text = if ([string]::IsNullOrEmpty($script:HeadSha)) {
		[IO.File]::ReadAllText((Join-Path $script:Root ($Path -replace '/', [IO.Path]::DirectorySeparatorChar)), $script:Utf8)
	}
	else {
		Invoke-ResidueGit @('show', "$($script:HeadSha):$Path")
	}
	$script:NewSideLines[$Path] = @($text -split "`r`n|`n|`r")
	return , $script:NewSideLines[$Path]
}

function Get-InventoryDocument() {
	$arguments = @('-NoProfile', '-File', $script:InventoryScript, '-RepositoryRoot', $script:Root, '-Baseline', $Baseline, '-Regions')
	if (-not [string]::IsNullOrWhiteSpace($Head)) { $arguments += @('-Head', $Head) }
	if ($IncludeUntracked) {
		# The inventory reports an untracked path only when the caller lists it, so the pass-through
		# switch supplies the whole untracked set in the comma-separated form that script splits.
		$untracked = @((Invoke-ResidueGit @('ls-files', '--others', '--exclude-standard', '-z')) -split "`0" | Where-Object { -not [string]::IsNullOrEmpty($_) })
		if ($untracked.Count -gt 0) { $arguments += @('-IncludeUntracked', ($untracked -join ',')) }
	}
	$shell = [Environment]::ProcessPath
	if ([string]::IsNullOrEmpty($shell)) { $shell = 'pwsh' }
	$run = Invoke-ResidueProcess $shell $arguments $script:Root
	$document = $null
	if (-not [string]::IsNullOrWhiteSpace($run.Stdout)) { $document = $run.Stdout | ConvertFrom-Json }
	if ($run.ExitCode -ne 0 -or $null -eq $document -or $document.status -cne 'pass') {
		$reason = if ($null -ne $document) { "$($document.code): $($document.message)" } else { $run.Stderr.Trim() }
		Complete-SessionDebugResidue 2 'blocked' 'residue.inventory-unavailable' "The session change inventory did not produce a scannable result: $reason"
	}
	return $document
}

function Get-AddedLine([object] $Inventory) {
	# Only C++ classes are scanned, and only the head side of each added or modified region: with the
	# inventory's -U0 regions, every line in a region's head-side range is a line this session added.
	$cppPaths = [Collections.Generic.HashSet[string]]::new([string[]] @())
	foreach ($entry in $Inventory.entries) {
		if ($script:CppClasses -ccontains $entry.class) { [void] $cppPaths.Add($entry.path) }
	}
	$added = [Collections.Generic.List[object]]::new()
	if ($null -eq $Inventory.regions) { return $added }
	foreach ($region in $Inventory.regions) {
		if (-not $cppPaths.Contains($region.path) -or $null -eq $region.startLine) { continue }
		$lines = Get-NewSideLine $region.path
		for ($number = [int] $region.startLine; $number -le [int] $region.endLine; $number++) {
			if ($number -lt 1 -or $number -gt $lines.Count) { continue }
			$added.Add([pscustomobject] @{ Path = $region.path; Line = $number; Text = $lines[$number - 1] })
		}
	}
	return $added
}

function Test-DebugResiduePattern([string] $Text) {
	foreach ($pattern in $script:ResiduePatterns) {
		if ($Text -cmatch $pattern.Pattern) { return $pattern.Kind }
	}
	return $null
}

try {
	$script:Root = Get-AgentCanonicalPath $RepositoryRoot
	if (-not [IO.Path]::IsPathRooted($RepositoryRoot) -or -not (Test-Path -LiteralPath $script:Root -PathType Container)) {
		Complete-SessionDebugResidue 2 'blocked' 'residue.repository-root-invalid' "-RepositoryRoot must be an existing absolute directory: '$RepositoryRoot'."
	}
	if (-not (Test-Path -LiteralPath $script:InventoryScript -PathType Leaf)) {
		Complete-SessionDebugResidue 2 'blocked' 'residue.inventory-missing' "The session change inventory script is missing: '$($script:InventoryScript)'."
	}
	$inventory = Get-InventoryDocument
	$script:HeadSha = if ([string]::IsNullOrWhiteSpace($inventory.headSha)) { '' } else { $inventory.headSha }
	# A passing inventory always carries truncation.regions, so both counts are read directly.
	$emittedRegionCount = @($inventory.regions).Count
	$regionsCapped = $emittedRegionCount -ge $script:InventoryRegionCap -or [int] $inventory.truncation.regions.full -gt $emittedRegionCount

	$hits = [Collections.Generic.List[object]]::new()
	foreach ($line in (Get-AddedLine $inventory)) {
		$kind = Test-DebugResiduePattern $line.Text
		if ($null -eq $kind) { continue }
		$text = $line.Text.Trim()
		if ($text.Length -gt $script:MaximumTextLength) { $text = $text.Substring(0, $script:MaximumTextLength) }
		$hits.Add([ordered]@{ path = $line.Path; line = $line.Line; kind = $kind; text = $text })
	}
	$sorted = [Collections.Generic.List[object]]::new($hits)
	$sorted.Sort([Comparison[object]] {
		param($left, $right)
		$compare = [string]::CompareOrdinal($left.path, $right.path)
		if ($compare -ne 0) { return $compare }
		return $left.line - $right.line
	})

	# Counts always describe the complete scan, never the truncated emission.
	$counts = [ordered]@{ total = $sorted.Count }
	foreach ($pattern in $script:ResiduePatterns) { $counts[$pattern.Kind] = @($sorted | Where-Object { $_.kind -ceq $pattern.Kind }).Count }
	$result.counts = $counts
	$emitted = [Collections.Generic.List[object]]::new()
	foreach ($hit in ($sorted | Select-Object -First $script:MaximumHits)) { $emitted.Add($hit) }
	while ($true) {
		$result.hits = [object[]] $emitted.ToArray()
		$result.truncated = $emitted.Count -lt $sorted.Count -or $regionsCapped
		if ($script:Utf8.GetByteCount(($result | ConvertTo-Json -Depth 32 -Compress)) -le $script:MaximumOutputBytes) { break }
		if ($emitted.Count -eq 0) { break }
		$drop = [Math]::Max(1, [int] [Math]::Ceiling($emitted.Count * 0.1))
		$emitted.RemoveRange($emitted.Count - $drop, $drop)
	}
	Complete-SessionDebugResidue 0 'pass' 'ok' "Scanned the session-added C++ lines and found $($sorted.Count) residue candidate(s)."
}
catch {
	Complete-SessionDebugResidue 1 'error' 'internal.error' $_.Exception.Message
}
