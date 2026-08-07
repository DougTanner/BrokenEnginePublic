# Deterministic session change inventory shared by the review, audit, and workflow skills:
# one read-only answer to "what changed between the session baseline and the head side, and
# what artifact type is each change". The script writes no file and no repository metadata
# (GIT_OPTIONAL_LOCKS=0 keeps Git from refreshing the index), so it is safe under a
# read-only sandbox. Diagnostics go to stderr; stdout carries only the result document.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $RepositoryRoot,
	[Parameter(Mandatory)][string] $Baseline,
	[string] $Head,
	[string[]] $IncludeUntracked,
	[switch] $Regions,
	[switch] $Landing,
	[switch] $EmitTargets
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1') -Force

$script:MaximumEntries = 500
$script:MaximumRegions = 400
$script:MaximumSymbolLength = 96
$script:MaximumMessageLength = 256
$script:MaximumOutputBytes = 131072

# GLSL source extensions, matching the list in .agents/skills/glsl-review/SKILL.md.
$script:ShaderExtensions = @('.vert', '.frag', '.comp', '.geom', '.tesc', '.tese', '.glsl', '.mesh', '.task', '.rgen', '.rmiss', '.rchit', '.rahit', '.rint', '.rcall')
$script:DualLanguageHeaders = @('shaderlayouts.h', 'shaderlayoutsbase.h')
$script:CppClasses = @('cpp', 'dual-language-header')
$script:ClassNames = @('dual-language-header', 'glsl', 'cpp', 'skill', 'plan', 'script', 'vcxproj', 'doc', 'binary', 'other')
$script:ManifestModes = @('100644', '100755')
# EXCLUDED in .agents/skills/code-quality-metrics/scripts/Analyze-CodeQualityMetrics.py is the source of
# truth for the analyzer corpus: a target path the analyzer finds in neither corpus fails there with exit
# 2, so a path under one of these first path components is never emitted.
$script:ManifestExcludedRoots = @('ThirdParty', '.agents', '.claude', 'Temp')
$script:ZeroOid = '0000000000000000000000000000000000000000'

$script:Root = $null
$script:BlobHashes = @{}
$script:Utf8 = [Text.UTF8Encoding]::new($false)

$result = [ordered]@{
	schemaVersion = 'broken-engine-session-change-inventory/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Session change inventory did not run.'
	baselineSha = $null
	headSha = $null
	entries = @()
	counts = $null
	triggers = $null
	regions = $null
	landing = $null
	truncated = $false
	truncation = $null
}

function Write-InventoryStream([bool] $Error, [string] $Text) {
	$stream = if ($Error) { [Console]::OpenStandardError() } else { [Console]::OpenStandardOutput() }
	$bytes = $script:Utf8.GetBytes($Text)
	$stream.Write($bytes, 0, $bytes.Length)
	$stream.Flush()
}

function Complete-SessionChangeInventory([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = if ($Message.Length -gt $script:MaximumMessageLength) { $Message.Substring(0, $script:MaximumMessageLength) } else { $Message }
	if ($EmitTargets) {
		# The targets consumer reads raw stdout: a non-pass run leaves stdout empty and reports
		# the structured outcome on stderr, and a pass has already written the targets bytes.
		if ($ExitCode -ne 0) { Write-InventoryStream $true (($result | ConvertTo-Json -Depth 32 -Compress) + "`n") }
		exit $ExitCode
	}
	Write-InventoryStream $false ($result | ConvertTo-Json -Depth 32 -Compress)
	exit $ExitCode
}

function Invoke-InventoryGit([string[]] $Arguments, [bool] $AllowFailure = $false) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = 'git'
	$start.WorkingDirectory = $script:Root
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = $script:Utf8
	$start.StandardErrorEncoding = $script:Utf8
	$start.Environment['GIT_OPTIONAL_LOCKS'] = '0'
	foreach ($argument in @('-C', $script:Root, '--no-pager') + $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start git for: $($Arguments -join ' ')" }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$run = [pscustomobject] @{
		ExitCode = $process.ExitCode
		Stdout = $stdoutTask.GetAwaiter().GetResult()
		Stderr = $stderrTask.GetAwaiter().GetResult()
	}
	$process.Dispose()
	if (-not $AllowFailure -and $run.ExitCode -ne 0) { throw "git $($Arguments -join ' ') failed with exit $($run.ExitCode): $($run.Stderr.Trim())" }
	return $run
}

function Get-InventoryHex([byte[]] $Hash) {
	return [Convert]::ToHexString($Hash).ToLowerInvariant()
}

function Get-InventoryBlobSha256([string] $Oid) {
	if ($script:BlobHashes.ContainsKey($Oid)) { return $script:BlobHashes[$Oid] }
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = 'git'
	$start.WorkingDirectory = $script:Root
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.Environment['GIT_OPTIONAL_LOCKS'] = '0'
	foreach ($argument in @('-C', $script:Root, '--no-pager', 'cat-file', 'blob', $Oid)) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start git cat-file for blob $Oid." }
	$hash = [Security.Cryptography.SHA256]::HashData($process.StandardOutput.BaseStream)
	$process.WaitForExit()
	$exitCode = $process.ExitCode
	$process.Dispose()
	if ($exitCode -ne 0) { throw "git cat-file blob $Oid failed with exit $exitCode." }
	$script:BlobHashes[$Oid] = Get-InventoryHex $hash
	return $script:BlobHashes[$Oid]
}

function Get-InventoryFileSha256([string] $RelativePath) {
	$full = Join-Path $script:Root ($RelativePath -replace '/', [IO.Path]::DirectorySeparatorChar)
	$stream = [IO.File]::Open($full, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
	try { return Get-InventoryHex ([Security.Cryptography.SHA256]::HashData($stream)) }
	finally { $stream.Dispose() }
}

function Get-InventoryWorkingMode([string] $RelativePath) {
	$full = Join-Path $script:Root ($RelativePath -replace '/', [IO.Path]::DirectorySeparatorChar)
	$item = Get-Item -LiteralPath $full -Force -ErrorAction Stop
	if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { return '120000' }
	return '100644'
}

function Get-InventoryIdentity([string] $Mode, [string] $Oid, [string] $RelativePath) {
	if ($Mode -ceq '000000') { return $null }
	if ($Mode -ceq '160000') {
		# A gitlink's identity is the SHA-256 over the ASCII bytes of its 40-character commit hex.
		$commit = $Oid
		if ($commit -ceq $script:ZeroOid) {
			$submodule = Invoke-InventoryGit @('-C', $RelativePath, 'rev-parse', 'HEAD') $true
			$commit = if ($submodule.ExitCode -eq 0) { $submodule.Stdout.Trim() } else { $null }
		}
		$sha256 = if ($null -eq $commit) { $null } else { Get-InventoryHex ([Security.Cryptography.SHA256]::HashData([Text.Encoding]::ASCII.GetBytes($commit))) }
		return [ordered]@{ mode = $Mode; sha256 = $sha256 }
	}
	$sha256 = if ($Oid -ceq $script:ZeroOid) { Get-InventoryFileSha256 $RelativePath } else { Get-InventoryBlobSha256 $Oid }
	return [ordered]@{ mode = $Mode; sha256 = $sha256 }
}

function Get-PathClass([string] $Path, [string] $Mode, [bool] $IsBinary) {
	# A gitlink is never a source artifact even when its path resembles one.
	if ($Mode -ceq '160000') { return 'other' }
	$leaf = $Path.Substring($Path.LastIndexOf('/') + 1)
	$extension = if ($leaf.Contains('.')) { $leaf.Substring($leaf.LastIndexOf('.')).ToLowerInvariant() } else { '' }
	$components = $Path -split '/'
	if ($script:DualLanguageHeaders -ccontains $leaf.ToLowerInvariant()) { return 'dual-language-header' }
	$underShaders = $false
	for ($i = 0; $i -lt $components.Count - 2; $i++) {
		if ($components[$i] -ceq 'Data' -and $components[$i + 1] -ceq 'Shaders') { $underShaders = $true; break }
	}
	# The directory clause covers headers and shader sources only: other files under
	# Data/Shaders (AGENTS.md, tables) fall through to the later rules.
	if ($script:ShaderExtensions -ccontains $extension) { return 'glsl' }
	if ($underShaders -and $extension -ceq '.h') { return 'glsl' }
	if ($extension -cin @('.cpp', '.h', '.inl')) { return 'cpp' }
	if ($leaf -ceq 'SKILL.md' -and $components.Count -eq 4 -and $components[0] -ceq '.agents' -and $components[1] -ceq 'skills') { return 'skill' }
	if ($extension -ceq '.md' -and $components.Count -gt 2 -and $components[0] -ceq 'Documents' -and $components[1] -ceq 'Plans' -and $leaf -cne 'AGENTS.md' -and $leaf -cne 'CLAUDE.md') { return 'plan' }
	if ($extension -cin @('.ps1', '.psm1', '.py', '.sh')) { return 'script' }
	if ($extension -ceq '.vcxproj' -or $leaf.ToLowerInvariant().EndsWith('.vcxproj.filters')) { return 'vcxproj' }
	if ($extension -cin @('.md', '.txt')) { return 'doc' }
	if ($IsBinary) { return 'binary' }
	return 'other'
}

function Get-InventoryEntry([hashtable] $Row, [hashtable] $BinaryPaths) {
	# One raw-diff row becomes one entry: both sides' identities, and both sides' classes whenever the
	# row has a baseline side, so a rename crossing a class boundary and a type change between an
	# ordinary file and a gitlink both stay visible to routing and to target eligibility.
	$path = $Row.Path
	$oldPath = $Row.OldPath
	$isBinary = $BinaryPaths.ContainsKey($path) -or ($null -ne $oldPath -and $BinaryPaths.ContainsKey($oldPath))
	$baselinePath = if ($null -ne $oldPath) { $oldPath } else { $path }
	$currentMode = if ($Row.DestinationMode -cne '000000') { $Row.DestinationMode } else { $Row.SourceMode }
	$baseline = Get-InventoryIdentity $Row.SourceMode $Row.SourceOid $baselinePath
	$current = Get-InventoryIdentity $Row.DestinationMode $Row.DestinationOid $path
	$class = Get-PathClass $path $currentMode $isBinary
	$oldClass = if ($Row.SourceMode -ceq '000000') { $null } else { Get-PathClass $baselinePath $Row.SourceMode $isBinary }
	return [pscustomobject] @{
		Status = $Row.Status
		Path = $path
		OldPath = $oldPath
		Baseline = $baseline
		Current = $current
		Class = $class
		OldClass = $oldClass
		Untracked = $Row.Untracked
	}
}

function Get-RoutingTrigger([object[]] $Entries) {
	$classes = [Collections.Generic.HashSet[string]]::new([string[]] @())
	$membershipClasses = [Collections.Generic.HashSet[string]]::new([string[]] @())
	foreach ($entry in $Entries) {
		[void] $classes.Add($entry.Class)
		if ($null -ne $entry.OldClass) { [void] $classes.Add($entry.OldClass) }
		if ($entry.Status -cin @('A', 'D', 'R', 'T')) {
			[void] $membershipClasses.Add($entry.Class)
			if ($null -ne $entry.OldClass) { [void] $membershipClasses.Add($entry.OldClass) }
		}
	}
	$cpp = $classes.Contains('cpp') -or $classes.Contains('dual-language-header')
	$glsl = $classes.Contains('glsl') -or $classes.Contains('dual-language-header')
	$sourceMembership = $membershipClasses.Contains('cpp') -or $membershipClasses.Contains('dual-language-header') -or $membershipClasses.Contains('glsl')
	return [ordered]@{
		repoCodeReview = $cpp
		glslReview = $glsl
		codeStyleReview = $cpp
		updateAffectedCode = $cpp -or $glsl
		updateVcxproj = $sourceMembership
		updateClaudeDocs = $cpp -or $glsl
		validateSkill = $classes.Contains('skill')
		planTouched = $classes.Contains('plan')
	}
}

function Get-InventoryCount([object[]] $Entries, [int] $UnlistedUntracked) {
	$counts = [ordered]@{ total = $Entries.Count }
	foreach ($name in $script:ClassNames) { $counts[$name] = @($Entries | Where-Object { $_.Class -ceq $name }).Count }
	$counts['unlistedUntracked'] = $UnlistedUntracked
	return $counts
}

function Get-InventoryDiffArgument([string] $BaselineSha, [string] $HeadSha) {
	if ([string]::IsNullOrEmpty($HeadSha)) { return @($BaselineSha) }
	return @($BaselineSha, $HeadSha)
}

function Get-InventoryRawRow([string] $BaselineSha, [string] $HeadSha) {
	$arguments = @('diff', '--raw', '--abbrev=40', '-M', '-z', '--no-color', '--no-ext-diff') + (Get-InventoryDiffArgument $BaselineSha $HeadSha) + @('--')
	$tokens = (Invoke-InventoryGit $arguments).Stdout -split "`0"
	$rows = [Collections.Generic.List[hashtable]]::new()
	$index = 0
	while ($index -lt $tokens.Count) {
		$token = $tokens[$index]
		if ([string]::IsNullOrEmpty($token)) { $index++; continue }
		$fields = $token.TrimStart(':') -split ' '
		$status = $fields[4].Substring(0, 1)
		$row = @{
			SourceMode = $fields[0]
			DestinationMode = $fields[1]
			SourceOid = $fields[2]
			DestinationOid = $fields[3]
			Status = $status
			OldPath = $null
			Untracked = $false
		}
		if ($status -ceq 'R' -or $status -ceq 'C') {
			$row.OldPath = $tokens[$index + 1]
			$row.Path = $tokens[$index + 2]
			$index += 3
		}
		else {
			$row.Path = $tokens[$index + 1]
			$index += 2
		}
		$rows.Add($row)
	}
	return $rows
}

function Get-InventoryBinaryPath([string] $BaselineSha, [string] $HeadSha) {
	# Git's own numstat decides binary, so .gitattributes settings are honoured.
	$arguments = @('diff', '--numstat', '-M', '-z', '--no-color', '--no-ext-diff') + (Get-InventoryDiffArgument $BaselineSha $HeadSha) + @('--')
	$tokens = (Invoke-InventoryGit $arguments).Stdout -split "`0"
	$binary = @{}
	$index = 0
	while ($index -lt $tokens.Count) {
		$token = $tokens[$index]
		if ([string]::IsNullOrEmpty($token)) { $index++; continue }
		$fields = $token -split "`t"
		$isBinary = $fields[0] -ceq '-' -and $fields[1] -ceq '-'
		if ($fields.Count -ge 3 -and -not [string]::IsNullOrEmpty($fields[2])) {
			if ($isBinary) { $binary[$fields[2]] = $true }
			$index++
			continue
		}
		if ($isBinary) {
			$binary[$tokens[$index + 1]] = $true
			$binary[$tokens[$index + 2]] = $true
		}
		$index += 3
	}
	return $binary
}

function Test-InventoryUntrackedBinary([string] $RelativePath) {
	$nullDevice = if ($IsWindows) { 'NUL' } else { '/dev/null' }
	$run = Invoke-InventoryGit @('diff', '--no-index', '--numstat', '--no-color', '--no-ext-diff', '--', $nullDevice, $RelativePath) $true
	foreach ($line in ($run.Stdout -split "`n")) {
		if ($line.StartsWith("-`t-`t")) { return $true }
	}
	return $false
}

function Get-InventoryUntrackedEntry([string[]] $Listed, [ref] $UnlistedCount) {
	$tracked = @((Invoke-InventoryGit @('ls-files', '--others', '--exclude-standard', '-z')).Stdout -split "`0" | Where-Object { -not [string]::IsNullOrEmpty($_) })
	$listedSet = [Collections.Generic.HashSet[string]]::new([string[]] $Listed)
	$entries = [Collections.Generic.List[object]]::new()
	$unlisted = 0
	foreach ($path in $tracked) {
		if (-not $listedSet.Contains($path)) { $unlisted++; continue }
		$mode = Get-InventoryWorkingMode $path
		# The binary rule is ordered after every extension rule, so the per-file Git probe only
		# runs for a path no earlier rule already claimed.
		$class = Get-PathClass $path $mode $false
		if ($class -ceq 'other' -and (Test-InventoryUntrackedBinary $path)) { $class = 'binary' }
		$entries.Add([pscustomobject] @{
			Status = 'A'
			Path = $path
			OldPath = $null
			Baseline = $null
			Current = [ordered]@{ mode = $mode; sha256 = Get-InventoryFileSha256 $path }
			Class = $class
			OldClass = $null
			Untracked = $true
		})
	}
	$UnlistedCount.Value = $unlisted
	return $entries
}

function Get-RegionTable([string] $BaselineSha, [string] $HeadSha, [object[]] $UntrackedEntries) {
	$arguments = @('-c', 'core.quotepath=false', 'diff', '-U0', '-M', '--no-color', '--no-ext-diff', '--no-textconv') + (Get-InventoryDiffArgument $BaselineSha $HeadSha) + @('--')
	$lines = @((Invoke-InventoryGit $arguments).Stdout -split "`n")
	foreach ($entry in $UntrackedEntries) {
		$nullDevice = if ($IsWindows) { 'NUL' } else { '/dev/null' }
		$run = Invoke-InventoryGit @('-c', 'core.quotepath=false', 'diff', '--no-index', '-U0', '--no-color', '--no-ext-diff', '--no-textconv', '--', $nullDevice, $entry.Path) $true
		$lines += @($run.Stdout -split "`n")
	}
	$regions = [Collections.Generic.List[object]]::new()
	$oldPath = $null
	$newPath = $null
	foreach ($line in $lines) {
		if ($line.StartsWith('--- ')) {
			$value = $line.Substring(4).TrimEnd("`r")
			$oldPath = if ($value -ceq '/dev/null' -or $value -ceq 'NUL') { $null } else { $value.Substring(2) }
			continue
		}
		if ($line.StartsWith('+++ ')) {
			$value = $line.Substring(4).TrimEnd("`r")
			$newPath = if ($value -ceq '/dev/null' -or $value -ceq 'NUL') { $null } else { $value.Substring(2) }
			continue
		}
		if (-not $line.StartsWith('@@ ')) { continue }
		if ($line -cnotmatch '^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@ ?(.*)$') { continue }
		$oldStart = [int] $Matches[1]
		$oldCount = if ([string]::IsNullOrEmpty($Matches[2])) { 1 } else { [int] $Matches[2] }
		$newStart = [int] $Matches[3]
		$newCount = if ([string]::IsNullOrEmpty($Matches[4])) { 1 } else { [int] $Matches[4] }
		$symbol = $Matches[5].TrimEnd("`r").Trim()
		if ($symbol.Length -gt $script:MaximumSymbolLength) { $symbol = $symbol.Substring(0, $script:MaximumSymbolLength) }
		$kind = if ($oldCount -eq 0) { 'added' } elseif ($newCount -eq 0) { 'deleted' } else { 'modified' }
		$regions.Add([ordered]@{
			path = if ($null -ne $newPath) { $newPath } else { $oldPath }
			kind = $kind
			startLine = if ($newCount -eq 0) { $null } else { $newStart }
			endLine = if ($newCount -eq 0) { $null } else { $newStart + $newCount - 1 }
			oldStartLine = if ($oldCount -eq 0) { $null } else { $oldStart }
			oldEndLine = if ($oldCount -eq 0) { $null } else { $oldStart + $oldCount - 1 }
			addedLines = $newCount
			deletedLines = $oldCount
			symbol = if ([string]::IsNullOrEmpty($symbol)) { $null } else { $symbol }
		})
	}
	# Wrap so a zero- or one-element table stays a collection instead of unrolling (see
	# Find-SessionDebugResidue.ps1 Get-NewSideLine for the same pattern).
	return , $regions
}

function Get-LandingState([string] $BaselineSha, [string] $HeadSha, [ref] $Truncation) {
	$reviewed = [Collections.Generic.List[object]]::new()
	$arguments = @('diff', '--raw', '--abbrev=40', '-M', '-z', '--no-color', '--no-ext-diff', "$BaselineSha...$HeadSha", '--')
	$tokens = (Invoke-InventoryGit $arguments).Stdout -split "`0"
	$index = 0
	while ($index -lt $tokens.Count) {
		$token = $tokens[$index]
		if ([string]::IsNullOrEmpty($token)) { $index++; continue }
		$fields = $token.TrimStart(':') -split ' '
		$status = $fields[4].Substring(0, 1)
		$old = $null
		if ($status -ceq 'R' -or $status -ceq 'C') { $old = $tokens[$index + 1]; $path = $tokens[$index + 2]; $index += 3 }
		else { $path = $tokens[$index + 1]; $index += 2 }
		$reviewed.Add([ordered]@{
			status = $status
			path = $path
			oldPath = $old
			baselineMode = $fields[0]
			currentMode = $fields[1]
		})
	}
	$porcelain = @((Invoke-InventoryGit @('status', '--porcelain=v2', '--untracked-files=all', '--ignored=matching')).Stdout -split "`n" | ForEach-Object { $_.TrimEnd("`r") } | Where-Object { -not [string]::IsNullOrEmpty($_) })
	# Uninitialized submodules are normal provisioning state, reported verbatim and never drift;
	# a repository whose gitlinks have no .gitmodules entry simply reports none.
	$submoduleRun = Invoke-InventoryGit @('submodule', 'status') $true
	$submodules = @()
	if ($submoduleRun.ExitCode -eq 0) { $submodules = @($submoduleRun.Stdout -split "`n" | ForEach-Object { $_.TrimEnd("`r") } | Where-Object { -not [string]::IsNullOrEmpty($_) }) }
	$planTouched = $false
	foreach ($row in $reviewed) {
		if ((Get-PathClass $row.path $row.currentMode $false) -ceq 'plan') { $planTouched = $true; break }
		if ($null -ne $row.oldPath -and (Get-PathClass $row.oldPath $row.baselineMode $false) -ceq 'plan') { $planTouched = $true; break }
	}
	$landingTruncation = [ordered]@{
		reviewed = [ordered]@{ full = $reviewed.Count; emitted = [Math]::Min($reviewed.Count, $script:MaximumEntries) }
		porcelain = [ordered]@{ full = $porcelain.Count; emitted = [Math]::Min($porcelain.Count, $script:MaximumEntries) }
		submodules = [ordered]@{ full = $submodules.Count; emitted = [Math]::Min($submodules.Count, $script:MaximumEntries) }
	}
	$Truncation.Value = $landingTruncation
	return [ordered]@{
		reviewed = [object[]] @($reviewed | Select-Object -First $script:MaximumEntries)
		porcelain = [string[]] @($porcelain | Select-Object -First $script:MaximumEntries)
		submodules = [string[]] @($submodules | Select-Object -First $script:MaximumEntries)
		planTouched = $planTouched
	}
}

function Test-ManifestCorpusPath([string] $Path) {
	return $script:ManifestExcludedRoots -cnotcontains $Path.Split('/')[0]
}

function Write-SessionTargets([object[]] $Entries) {
	# Each side qualifies on its own class, mode, and corpus membership, so a rename that crosses a class
	# or corpus boundary and a type change between an ordinary file and a gitlink contribute only the
	# eligible side's path. A side whose mode is not an ordinary file is never analyzable C++, so it is
	# skipped rather than blocked; the analyzer derives pairing and renames from the emitted paths.
	$paths = [Collections.Generic.HashSet[string]]::new([string[]] @(), [StringComparer]::Ordinal)
	foreach ($entry in $Entries) {
		$baselinePath = if ($null -ne $entry.OldPath) { $entry.OldPath } else { $entry.Path }
		$baselineClass = if ($null -ne $entry.OldClass) { $entry.OldClass } else { $entry.Class }
		if ($null -ne $entry.Baseline -and $script:CppClasses -ccontains $baselineClass -and (Test-ManifestCorpusPath $baselinePath) -and $script:ManifestModes -ccontains $entry.Baseline.mode) {
			[void] $paths.Add($baselinePath)
		}
		if ($null -ne $entry.Current -and $script:CppClasses -ccontains $entry.Class -and (Test-ManifestCorpusPath $entry.Path) -and $script:ManifestModes -ccontains $entry.Current.mode) {
			[void] $paths.Add($entry.Path)
		}
	}
	if ($paths.Count -gt $script:MaximumEntries) {
		Complete-SessionChangeInventory 2 'blocked' 'inventory.manifest-cap-exceeded' "The targets hold $($paths.Count) paths, above the cap of $($script:MaximumEntries); truncated targets would narrow an authorized review."
	}
	$ordered = [Collections.Generic.List[string]]::new($paths)
	$ordered.Sort([StringComparer]::Ordinal)
	$targets = [ordered]@{
		schemaVersion = 'broken-engine-code-quality-targets/v1'
		paths = [string[]] $ordered.ToArray()
	}
	$text = ($targets | ConvertTo-Json -Depth 32 -Compress) + "`n"
	if ($script:Utf8.GetByteCount($text) -gt $script:MaximumOutputBytes) {
		Complete-SessionChangeInventory 2 'blocked' 'inventory.manifest-cap-exceeded' "The targets are $($script:Utf8.GetByteCount($text)) bytes, above the stdout budget of $($script:MaximumOutputBytes)."
	}
	Write-InventoryStream $false $text
}

try {
	$modes = @(@($Regions.IsPresent, $Landing.IsPresent, $EmitTargets.IsPresent) | Where-Object { $_ })
	if ($modes.Count -gt 1) {
		Complete-SessionChangeInventory 2 'blocked' 'inventory.mode-conflict' 'Supply at most one of -Regions, -Landing, and -EmitTargets.'
	}
	$script:Root = Get-AgentCanonicalPath $RepositoryRoot
	if (-not [IO.Path]::IsPathRooted($RepositoryRoot) -or -not (Test-Path -LiteralPath $script:Root -PathType Container)) {
		Complete-SessionChangeInventory 2 'blocked' 'inventory.repository-root-invalid' "-RepositoryRoot must be an existing absolute directory: '$RepositoryRoot'."
	}
	$topLevel = Invoke-InventoryGit @('rev-parse', '--show-toplevel') $true
	if ($topLevel.ExitCode -ne 0) {
		Complete-SessionChangeInventory 2 'blocked' 'inventory.repository-root-invalid' "-RepositoryRoot is not inside a Git repository: '$RepositoryRoot'."
	}
	# Every diff below is relative to the repository root, so a subdirectory root would silently
	# report an inventory that omits everything outside it.
	$topLevelPath = Get-AgentCanonicalPath ($topLevel.Stdout.Trim())
	if (-not [string]::Equals($topLevelPath, $script:Root, [StringComparison]::OrdinalIgnoreCase)) {
		Complete-SessionChangeInventory 2 'blocked' 'inventory.root-not-toplevel' "-RepositoryRoot must be the repository toplevel '$topLevelPath', not '$RepositoryRoot'."
	}
	if ($Landing -and [string]::IsNullOrWhiteSpace($Head)) {
		Complete-SessionChangeInventory 2 'blocked' 'inventory.landing-requires-head' '-Landing compares the reviewed diff and requires a commit-valued -Head.'
	}
	if ($Baseline -cnotmatch '^[0-9a-fA-F]{40}$') {
		Complete-SessionChangeInventory 2 'blocked' 'inventory.baseline-unresolved' "-Baseline must be a full 40-character Git SHA: '$Baseline'."
	}
	$baselineRun = Invoke-InventoryGit @('rev-parse', '--verify', '--quiet', "$Baseline^{commit}") $true
	if ($baselineRun.ExitCode -ne 0) {
		Complete-SessionChangeInventory 2 'blocked' 'inventory.baseline-unresolved' "The baseline does not resolve to a commit in this repository: '$Baseline'."
	}
	$baselineSha = $baselineRun.Stdout.Trim()
	$result.baselineSha = $baselineSha
	$headSha = ''
	if (-not [string]::IsNullOrWhiteSpace($Head)) {
		$headRun = Invoke-InventoryGit @('rev-parse', '--verify', '--quiet', "$Head^{commit}") $true
		if ($headRun.ExitCode -ne 0) {
			Complete-SessionChangeInventory 2 'blocked' 'inventory.head-unresolved' "The head revision does not resolve to a commit in this repository: '$Head'."
		}
		$headSha = $headRun.Stdout.Trim()
		$result.headSha = $headSha
	}

	$listed = [Collections.Generic.List[string]]::new()
	# `pwsh -File` hands one literal string per token, so the documented comma-separated form is
	# split here rather than by the parameter binder.
	foreach ($path in @(@($IncludeUntracked) -split ',')) {
		if ([string]::IsNullOrWhiteSpace($path)) { continue }
		$normalized = $path.Replace('\', '/').Trim()
		if ([IO.Path]::IsPathRooted($normalized)) {
			$rootPosix = $script:Root.Replace('\', '/').TrimEnd('/') + '/'
			if ($normalized.StartsWith($rootPosix, [StringComparison]::OrdinalIgnoreCase)) { $normalized = $normalized.Substring($rootPosix.Length) }
		}
		if ($normalized.StartsWith('./')) { $normalized = $normalized.Substring(2) }
		$listed.Add($normalized)
	}

	$binaryPaths = Get-InventoryBinaryPath $baselineSha $headSha
	$entries = [Collections.Generic.List[object]]::new()
	foreach ($row in (Get-InventoryRawRow $baselineSha $headSha)) { $entries.Add((Get-InventoryEntry $row $binaryPaths)) }
	$unlistedUntracked = 0
	$untrackedEntries = @()
	if ([string]::IsNullOrEmpty($headSha)) {
		# Untracked paths exist only against the working tree; a commit-valued head has none.
		$unlistedReference = 0
		$untrackedEntries = @(Get-InventoryUntrackedEntry $listed.ToArray() ([ref] $unlistedReference))
		$unlistedUntracked = $unlistedReference
		foreach ($entry in $untrackedEntries) { $entries.Add($entry) }
	}
	$sorted = [Collections.Generic.List[object]]::new($entries)
	$sorted.Sort([Comparison[object]] {
		param($left, $right)
		return [string]::CompareOrdinal($left.Path, $right.Path)
	})

	if ($EmitTargets) {
		Write-SessionTargets ([object[]] $sorted.ToArray())
		Complete-SessionChangeInventory 0 'pass' 'ok' "Emitted targets for $($sorted.Count) inventoried change(s)."
	}

	# Counts and triggers always describe the complete inventory, never the truncated emission.
	$result.counts = Get-InventoryCount ([object[]] $sorted.ToArray()) $unlistedUntracked
	$result.triggers = Get-RoutingTrigger ([object[]] $sorted.ToArray())
	$emittedEntries = [Collections.Generic.List[object]]::new()
	foreach ($entry in ($sorted | Select-Object -First $script:MaximumEntries)) {
		$emittedEntries.Add([ordered]@{
			status = $entry.Status
			path = $entry.Path
			oldPath = $entry.OldPath
			baseline = $entry.Baseline
			current = $entry.Current
			class = $entry.Class
		})
	}
	$fullRegions = [Collections.Generic.List[object]]::new()
	if ($Regions) { $fullRegions = Get-RegionTable $baselineSha $headSha ([object[]] $untrackedEntries) }
	$emittedRegions = [Collections.Generic.List[object]]::new()
	foreach ($region in ($fullRegions | Select-Object -First $script:MaximumRegions)) { $emittedRegions.Add($region) }
	$landingTruncation = $null
	if ($Landing) {
		$landingReference = $null
		$result.landing = Get-LandingState $baselineSha $headSha ([ref] $landingReference)
		$landingTruncation = $landingReference
	}

	$truncation = [ordered]@{
		entries = [ordered]@{ full = $sorted.Count; emitted = $emittedEntries.Count }
		regions = [ordered]@{ full = $fullRegions.Count; emitted = $emittedRegions.Count }
		landing = $landingTruncation
		outputBytes = 0
	}
	$result.entries = [object[]] $emittedEntries.ToArray()
	if ($Regions) { $result.regions = [object[]] $emittedRegions.ToArray() }
	$result.truncation = $truncation
	$result.status = 'pass'
	$result.code = 'ok'
	$result.message = "Inventoried $($sorted.Count) change(s) against baseline $baselineSha."
	# The stdout budget is enforced last: regions shed before entries, then the landing arrays, and
	# every shed item stays visible in the reported full and emitted counts. Reporting the size of a
	# document that carries its own size needs a fixed point, so the measurement repeats until the
	# reported value equals the byte count of the document that actually carries it.
	while ($true) {
		$truncation.entries.emitted = $emittedEntries.Count
		$truncation.regions.emitted = $emittedRegions.Count
		$result.entries = [object[]] $emittedEntries.ToArray()
		if ($Regions) { $result.regions = [object[]] $emittedRegions.ToArray() }
		$result.truncated = $truncation.entries.emitted -lt $truncation.entries.full -or $truncation.regions.emitted -lt $truncation.regions.full
		if ($null -ne $landingTruncation) {
			foreach ($name in @('reviewed', 'porcelain', 'submodules')) {
				if ($landingTruncation[$name].emitted -lt $landingTruncation[$name].full) { $result.truncated = $true }
			}
		}
		$bytes = 0
		while ($true) {
			$bytes = $script:Utf8.GetByteCount(($result | ConvertTo-Json -Depth 32 -Compress))
			if ($bytes -eq $truncation.outputBytes) { break }
			$truncation.outputBytes = $bytes
		}
		if ($bytes -le $script:MaximumOutputBytes) { break }
		if ($emittedRegions.Count -gt 0) {
			$drop = [Math]::Max(1, [int] [Math]::Ceiling($emittedRegions.Count * 0.1))
			$emittedRegions.RemoveRange($emittedRegions.Count - $drop, $drop)
			continue
		}
		if ($emittedEntries.Count -gt 0) {
			$drop = [Math]::Max(1, [int] [Math]::Ceiling($emittedEntries.Count * 0.1))
			$emittedEntries.RemoveRange($emittedEntries.Count - $drop, $drop)
			continue
		}
		$shedLanding = $false
		if ($null -ne $landingTruncation) {
			foreach ($name in @('reviewed', 'porcelain', 'submodules')) {
				$kept = $landingTruncation[$name].emitted
				if ($kept -le 0) { continue }
				$kept -= [Math]::Max(1, [int] [Math]::Ceiling($kept * 0.1))
				$landingTruncation[$name].emitted = $kept
				$shortened = @($result.landing[$name] | Select-Object -First $kept)
				$result.landing[$name] = if ($name -ceq 'reviewed') { [object[]] $shortened } else { [string[]] $shortened }
				$shedLanding = $true
			}
		}
		if ($shedLanding) { continue }
		break
	}
	Complete-SessionChangeInventory 0 'pass' 'ok' $result.message
}
catch {
	Complete-SessionChangeInventory 1 'error' 'internal.error' $_.Exception.Message
}
