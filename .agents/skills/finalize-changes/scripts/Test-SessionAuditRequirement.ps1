# Read-only decision sidecar for the post-reconciliation session-audit gate.
# The caller supplies immutable lifecycle, verification, coverage, and overlap
# evidence in one broken-engine-session-audit-input/v1 JSON object. This script
# independently normalizes the pre/post-rebase commit deltas and emits exactly
# one broken-engine-session-audit-decision/v1 JSON object. Ambiguous, missing,
# or malformed evidence requires an audit; that decision is still a successful
# evaluation and exits 0.
[CmdletBinding()]
param(
	[string] $InputJson
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$result = [ordered]@{
	schemaVersion = 'broken-engine-session-audit-decision/v1'
	required = $true
	reasons = @()
	message = $null
	repositoryRoot = $null
	commits = [ordered]@{ preRebaseSession = $null; rebased = $null; preRebaseParent = $null; rebasedParent = $null }
	digests = [ordered]@{ before = $null; after = $null; verified = $null }
}
$reasons = [Collections.Generic.List[string]]::new()

function Add-Reason([string] $Reason) {
	if (-not $reasons.Contains($Reason)) { $reasons.Add($Reason) }
}

function Complete-Decision {
	$result.required = $reasons.Count -ne 0
	$result.reasons = $reasons.ToArray()
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 8 -Compress))
	exit 0
}

function Invoke-NativeText([string] $Executable, [string[]] $Arguments, [string] $WorkingDirectory) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $Executable
	$start.WorkingDirectory = $WorkingDirectory
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false, $true)
	$start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false, $true)
	foreach ($argument in $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw "Could not start '$Executable'." }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$response = [pscustomobject]@{
		ExitCode = $process.ExitCode
		Stdout = $stdoutTask.GetAwaiter().GetResult()
		Stderr = $stderrTask.GetAwaiter().GetResult()
	}
	$process.Dispose()
	return $response
}

function Invoke-Git([string] $RepositoryRoot, [string[]] $Arguments) {
	$response = Invoke-NativeText 'git.exe' (@('-C', $RepositoryRoot) + $Arguments) $RepositoryRoot
	if ($response.ExitCode -ne 0) { throw "git $($Arguments -join ' ') failed: $($response.Stderr.Trim())." }
	return $response.Stdout
}

function Get-RequiredProperty($Object, [string] $Name, [string] $Context) {
	if ($null -eq $Object -or $null -eq $Object.PSObject.Properties[$Name]) { throw "$Context is missing '$Name'." }
	$value = $Object.$Name
	if ($value -is [Array]) { Write-Output -NoEnumerate $value }
	else { return $value }
}

function Get-RequiredString($Object, [string] $Name, [string] $Context) {
	$value = Get-RequiredProperty $Object $Name $Context
	if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value)) { throw "$Context.$Name must be a non-empty string." }
	return [string] $value
}

function Get-RequiredBoolean($Object, [string] $Name, [string] $Context) {
	$value = Get-RequiredProperty $Object $Name $Context
	if ($value -isnot [bool]) { throw "$Context.$Name must be a Boolean." }
	return [bool] $value
}

function ConvertTo-NormalizedPath([string] $Path, [string] $Context) {
	$normalized = $Path.Replace('\', '/')
	if ([string]::IsNullOrWhiteSpace($normalized) -or [IO.Path]::IsPathRooted($normalized) -or $normalized.StartsWith('../', [StringComparison]::Ordinal) -or
		$normalized.Contains('/../', [StringComparison]::Ordinal) -or $normalized.EndsWith('/..', [StringComparison]::Ordinal) -or $normalized.Contains([char]0)) {
		throw "$Context has an invalid repository-relative path '$Path'."
	}
	return $normalized
}

function Get-ContentIdentity([string] $RepositoryRoot, [string] $Mode, [string] $ObjectId) {
	switch ($Mode) {
		'100644' { return $ObjectId }
		'100755' { return $ObjectId }
		'120000' { return Invoke-Git $RepositoryRoot @('cat-file', 'blob', $ObjectId) }
		'160000' { return $ObjectId }
		default { throw "Unsupported destination mode '$Mode'." }
	}
}

function Get-SubmoduleStatus([string] $RepositoryRoot, [string] $Path) {
	$status = (Invoke-Git $RepositoryRoot @('submodule', 'status', '--', $Path)).TrimEnd("`r", "`n")
	if ([string]::IsNullOrWhiteSpace($status) -or $status.Contains("`n")) { throw "Git returned malformed submodule status for '$Path'." }
	return $status
}

function Get-GitlinkStatusDetails([string] $ContentIdentity) {
	$match = [regex]::Match($ContentIdentity, '^(?<state>[ +\-U])(?<objectId>[0-9a-f]{40}) (?<path>.+?)(?: \([^)]*\))?$')
	if (-not $match.Success) { return $null }
	return [pscustomobject]@{
		State = $match.Groups['state'].Value
		ObjectId = $match.Groups['objectId'].Value
		Path = $match.Groups['path'].Value
	}
}

function Test-VerifiedGitlinkStatus([string] $VerifiedStatus, [string] $CurrentStatus, [string] $Path) {
	$verified = Get-GitlinkStatusDetails $VerifiedStatus
	$current = Get-GitlinkStatusDetails $CurrentStatus
	if ($null -eq $verified -or $null -eq $current -or $verified.Path -cne $Path -or $current.Path -cne $Path) { return $false }
	if ($verified.State -in @('-', 'U') -or $current.State -in @('-', 'U')) { return $false }
	if ($VerifiedStatus -ceq $CurrentStatus) { return $true }
	# Committing a verified dirty pointer clears only its leading '+' marker.
	return $verified.State -ceq '+' -and $current.State -ceq ' ' -and $VerifiedStatus.Substring(1) -ceq $CurrentStatus.Substring(1)
}

function Get-TreeDelta([string] $RepositoryRoot, [string] $BeforeTree, [string] $AfterTree) {
	$raw = Invoke-Git $RepositoryRoot @('diff-tree', '--no-commit-id', '-r', '-M', '--raw', '--abbrev=40', '-z', $BeforeTree, $AfterTree, '--')
	$tokens = @($raw -split [char]0)
	$entries = [Collections.Generic.List[object]]::new()
	$index = 0
	while ($index -lt $tokens.Count) {
		$metadata = $tokens[$index]
		$index++
		if ([string]::IsNullOrEmpty($metadata)) { continue }
		$match = [regex]::Match($metadata, '^:([0-7]{6}) ([0-7]{6}) ([0-9a-f]{40}) ([0-9a-f]{40}) ([A-Z])([0-9]*)$')
		if (-not $match.Success) { throw "Git returned malformed raw delta metadata '$metadata'." }
		if ($index -ge $tokens.Count) { throw 'Git returned raw delta metadata without a path.' }
		$firstPath = ConvertTo-NormalizedPath $tokens[$index] 'Git delta'
		$index++
		$status = $match.Groups[5].Value
		$sourcePath = $null
		$path = $firstPath
		if ($status -in @('R', 'C')) {
			if ($index -ge $tokens.Count) { throw 'Git returned rename/copy metadata without a destination path.' }
			$sourcePath = $firstPath
			$path = ConvertTo-NormalizedPath $tokens[$index] 'Git delta'
			$index++
		}
		if ($status -notin @('A', 'M', 'D', 'R', 'T')) { throw "Unsupported Git delta status '$status'." }
		$newMode = $match.Groups[2].Value
		$newObject = $match.Groups[4].Value
		$mode = if ($status -eq 'D') { '-' } else { $newMode }
		$contentIdentity = if ($status -eq 'D') { '-' } else { Get-ContentIdentity $RepositoryRoot $newMode $newObject }
		$entries.Add([pscustomobject][ordered]@{
			path = $path
			status = $status
			sourcePath = $sourcePath
			mode = $mode
			contentIdentity = $contentIdentity
		})
	}
	return @($entries | Sort-Object @{ Expression = { if ($null -eq $_.sourcePath) { '' } else { $_.sourcePath } } }, path, status)
}

function ConvertTo-VerifiedEntries($Manifest) {
	if ($Manifest -isnot [Collections.IEnumerable] -or $Manifest -is [string]) { throw 'input.verifiedManifest must be an array.' }
	$entries = [Collections.Generic.List[object]]::new()
	foreach ($entry in @($Manifest)) {
		$path = ConvertTo-NormalizedPath (Get-RequiredString $entry 'path' 'input.verifiedManifest[]') 'input.verifiedManifest[]'
		$status = Get-RequiredString $entry 'status' "input.verifiedManifest[$path]"
		if ($status -notin @('A', 'M', 'D', 'R', 'T', 'untracked', '??')) { throw "input.verifiedManifest[$path].status is invalid." }
		$sourcePath = $null
		if ($status -eq 'R') { $sourcePath = ConvertTo-NormalizedPath (Get-RequiredString $entry 'sourcePath' "input.verifiedManifest[$path]") "input.verifiedManifest[$path]" }
		elseif ($null -ne $entry.PSObject.Properties['sourcePath'] -and $null -ne $entry.sourcePath) { throw "input.verifiedManifest[$path].sourcePath is allowed only for status R." }
		$mode = Get-RequiredString $entry 'mode' "input.verifiedManifest[$path]"
		$contentIdentity = Get-RequiredString $entry 'contentIdentity' "input.verifiedManifest[$path]"
		if ($status -eq 'D') {
			if ($mode -cne '-' -or $contentIdentity -cne '-') { throw "input.verifiedManifest[$path] deletion identity must be '-'." }
		}
		elseif ($mode -notin @('100644', '100755', '120000', '160000')) { throw "input.verifiedManifest[$path].mode is invalid." }
		$entries.Add([pscustomobject][ordered]@{ path = $path; status = $status; sourcePath = $sourcePath; mode = $mode; contentIdentity = $contentIdentity })
	}
	$normalized = @($entries | Sort-Object @{ Expression = { if ($null -eq $_.sourcePath) { '' } else { $_.sourcePath } } }, path, status)
	$keys = @($normalized | ForEach-Object { "$($_.sourcePath)`0$($_.path)" })
	if ($keys.Count -ne @($keys | Sort-Object -Unique).Count) { throw 'input.verifiedManifest contains duplicate path identities.' }
	return $normalized
}

function Bind-VerifiedGitlinkIdentities([string] $RepositoryRoot, $Entries) {
	$boundEntries = [Collections.Generic.List[object]]::new()
	$currentStatusMatches = $true
	foreach ($entry in @($Entries)) {
		$contentIdentity = $entry.contentIdentity
		if ($entry.mode -ceq '160000') {
			$verifiedStatus = Get-GitlinkStatusDetails $contentIdentity
			$currentStatus = Get-SubmoduleStatus $RepositoryRoot $entry.path
			if ($null -eq $verifiedStatus -or -not (Test-VerifiedGitlinkStatus $contentIdentity $currentStatus $entry.path)) {
				$currentStatusMatches = $false
			}
			else { $contentIdentity = $verifiedStatus.ObjectId }
		}
		$boundEntries.Add([pscustomobject][ordered]@{
			path = $entry.path
			status = $entry.status
			sourcePath = $entry.sourcePath
			mode = $entry.mode
			contentIdentity = $contentIdentity
		})
	}
	return [pscustomobject]@{ Entries = @($boundEntries); CurrentStatusMatches = $currentStatusMatches }
}

function Get-CanonicalEntryLines($Entries, [bool] $NormalizeUntracked) {
	return @($Entries | ForEach-Object {
		$status = if ($NormalizeUntracked -and $_.status -in @('untracked', '??')) { 'A' } else { $_.status }
		@($status, $(if ($null -eq $_.sourcePath) { '' } else { $_.sourcePath }), $_.path, $_.mode, $_.contentIdentity) | ConvertTo-Json -Compress
	})
}

function Get-Digest($Entries, [bool] $NormalizeUntracked = $false) {
	$canonical = (Get-CanonicalEntryLines $Entries $NormalizeUntracked) -join "`n"
	$sha256 = [Security.Cryptography.SHA256]::Create()
	try { $bytes = $sha256.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical)) } finally { $sha256.Dispose() }
	return [BitConverter]::ToString($bytes).Replace('-', '').ToLowerInvariant()
}

function Get-AffectedPaths($Entries) {
	return @($Entries | ForEach-Object { if ($null -ne $_.sourcePath) { $_.sourcePath }; $_.path } | Sort-Object -Unique)
}

function ConvertTo-EvidencePaths($Value, [string] $Context) {
	if ($null -eq $Value) { return @() }
	if ($Value -is [string]) { $Value = @($Value) }
	elseif ($Value -isnot [Collections.IEnumerable]) { throw "$Context must be an array." }
	return @(@($Value) | ForEach-Object {
		if ($_ -isnot [string]) { throw "$Context entries must be strings." }
		ConvertTo-NormalizedPath $_ $Context
	} | Sort-Object -Unique)
}

function Test-OrdinalSequenceEqual($Left, $Right) {
	$leftArray = @($Left)
	$rightArray = @($Right)
	if ($leftArray.Count -ne $rightArray.Count) { return $false }
	for ($index = 0; $index -lt $leftArray.Count; $index++) {
		if ([string] $leftArray[$index] -cne [string] $rightArray[$index]) { return $false }
	}
	return $true
}

try {
	if ([string]::IsNullOrWhiteSpace($InputJson)) { throw 'InputJson is missing.' }
	try { $inputObject = $InputJson | ConvertFrom-Json -Depth 100 -ErrorAction Stop }
	catch { throw "InputJson is malformed: $($_.Exception.Message)" }
	if ((Get-RequiredString $inputObject 'schemaVersion' 'input') -cne 'broken-engine-session-audit-input/v1') { throw 'input.schemaVersion is unsupported.' }

	$repositoryRootInput = Get-RequiredString $inputObject 'repositoryRoot' 'input'
	$repositoryRoot = [IO.Path]::GetFullPath($repositoryRootInput).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
	if (-not (Test-Path -LiteralPath $repositoryRoot -PathType Container)) { throw 'input.repositoryRoot does not exist.' }
	$gitTop = [IO.Path]::GetFullPath((Invoke-Git $repositoryRoot @('rev-parse', '--show-toplevel')).Trim()).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
	if (-not $repositoryRoot.Equals($gitTop, [StringComparison]::OrdinalIgnoreCase)) { throw 'input.repositoryRoot is not the Git top-level.' }
	$result.repositoryRoot = $repositoryRoot

	$preRebaseCommit = Get-RequiredString $inputObject 'preRebaseSessionCommit' 'input'
	$rebasedCommit = Get-RequiredString $inputObject 'rebasedCommit' 'input'
	foreach ($commit in @($preRebaseCommit, $rebasedCommit)) {
		if ($commit -cnotmatch '^[0-9a-f]{40}$') { throw "Commit '$commit' is not an exact lowercase object identity." }
		$resolved = (Invoke-Git $repositoryRoot @('rev-parse', '--verify', "$commit^{commit}")).Trim()
		if ($resolved -cne $commit) { throw "Commit '$commit' did not resolve exactly." }
	}
	$preParents = @((Invoke-Git $repositoryRoot @('show', '-s', '--format=%P', $preRebaseCommit)).Trim() -split ' ' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
	$rebasedParents = @((Invoke-Git $repositoryRoot @('show', '-s', '--format=%P', $rebasedCommit)).Trim() -split ' ' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
	if ($preParents.Count -ne 1 -or $rebasedParents.Count -ne 1) { throw 'Session audit reconciliation comparison requires two single-parent commits.' }
	$preParent = $preParents[0]
	$rebasedParent = $rebasedParents[0]
	$result.commits.preRebaseSession = $preRebaseCommit
	$result.commits.rebased = $rebasedCommit
	$result.commits.preRebaseParent = $preParent
	$result.commits.rebasedParent = $rebasedParent

	$before = @(Get-TreeDelta $repositoryRoot $preParent $preRebaseCommit)
	$after = @(Get-TreeDelta $repositoryRoot $rebasedParent $rebasedCommit)
	$verified = @(ConvertTo-VerifiedEntries (Get-RequiredProperty $inputObject 'verifiedManifest' 'input'))
	$verifiedBinding = Bind-VerifiedGitlinkIdentities $repositoryRoot $verified
	$verified = @($verifiedBinding.Entries)
	$result.digests.before = Get-Digest $before
	$result.digests.after = Get-Digest $after
	$result.digests.verified = Get-Digest $verified $true

	if (-not (Test-OrdinalSequenceEqual (Get-CanonicalEntryLines $before $false) (Get-CanonicalEntryLines $after $false))) { Add-Reason 'reconciliation.delta-changed' }
	if (-not $verifiedBinding.CurrentStatusMatches -or -not (Test-OrdinalSequenceEqual (Get-CanonicalEntryLines $before $false) (Get-CanonicalEntryLines $verified $true))) { Add-Reason 'verification.manifest-mismatch' }

	[void] (Get-RequiredProperty $inputObject 'dependencyOverlapEvidence' 'input')
	$overlapEvidence = $inputObject.PSObject.Properties['dependencyOverlapEvidence'].Value
	$evidencePreParent = Get-RequiredString $overlapEvidence 'preRebaseParent' 'input.dependencyOverlapEvidence'
	$evidenceRebasedParent = Get-RequiredString $overlapEvidence 'rebasedParent' 'input.dependencyOverlapEvidence'
	$changedPathsProperty = $overlapEvidence.PSObject.Properties['changedPaths']
	$directDependencyPathsProperty = $overlapEvidence.PSObject.Properties['directDependencyPaths']
	$overlappingPathsProperty = $overlapEvidence.PSObject.Properties['overlappingPaths']
	if ($null -eq $changedPathsProperty -or $null -eq $directDependencyPathsProperty -or $null -eq $overlappingPathsProperty -or
		$changedPathsProperty.Value -isnot [Array] -or $directDependencyPathsProperty.Value -isnot [Array] -or $overlappingPathsProperty.Value -isnot [Array]) {
		throw 'input.dependencyOverlapEvidence path collections must be arrays.'
	}
	$evidenceChangedPaths = @(ConvertTo-EvidencePaths $changedPathsProperty.Value 'input.dependencyOverlapEvidence.changedPaths')
	$directDependencyPaths = @(ConvertTo-EvidencePaths $directDependencyPathsProperty.Value 'input.dependencyOverlapEvidence.directDependencyPaths')
	$evidenceOverlappingPaths = @(ConvertTo-EvidencePaths $overlappingPathsProperty.Value 'input.dependencyOverlapEvidence.overlappingPaths')
	$primaryDelta = @(Get-TreeDelta $repositoryRoot $preParent $rebasedParent)
	$actualChangedPaths = @(Get-AffectedPaths $primaryDelta)
	$sessionPaths = @(Get-AffectedPaths $before)
	$overlapCandidates = @(($sessionPaths + $directDependencyPaths) | Sort-Object -Unique)
	$actualOverlappingPaths = @($actualChangedPaths | Where-Object { $overlapCandidates -ccontains $_ } | Sort-Object -Unique)
	if ($evidencePreParent -cne $preParent -or $evidenceRebasedParent -cne $rebasedParent -or
		-not (Test-OrdinalSequenceEqual $evidenceChangedPaths $actualChangedPaths) -or
		-not (Test-OrdinalSequenceEqual $evidenceOverlappingPaths $actualOverlappingPaths)) {
		Add-Reason 'dependency-overlap.evidence-mismatch'
	}
	if ($actualOverlappingPaths.Count -ne 0) { Add-Reason 'dependency-overlap.detected' }

	if (-not (Get-RequiredBoolean $inputObject 'conflictFree' 'input')) { Add-Reason 'reconciliation.not-conflict-free' }
	if (-not (Get-RequiredBoolean $inputObject 'requiredDomainCoverage' 'input')) { Add-Reason 'coverage.domain-incomplete' }
	if (-not (Get-RequiredBoolean $inputObject 'requiredAdversarialCoverage' 'input')) { Add-Reason 'coverage.adversarial-incomplete' }
	if (Get-RequiredBoolean $inputObject 'lateSemanticFixes' 'input') { Add-Reason 'late-semantic-fixes.present' }
	if (Get-RequiredBoolean $inputObject 'manualResolution' 'input') { Add-Reason 'manual-resolution.present' }
	if (Get-RequiredBoolean $inputObject 'invalidatedAssumptions' 'input') { Add-Reason 'invalidated-assumptions.present' }
	if (Get-RequiredBoolean $inputObject 'unseenContractSignificantRegions' 'input') { Add-Reason 'unseen-contract-significant-regions.present' }
	if (Get-RequiredBoolean $inputObject 'explicitUserRequest' 'input') { Add-Reason 'explicit-user-request' }
}
catch {
	$result.message = $_.Exception.Message
	Add-Reason 'evidence.missing-malformed-or-unproven'
}

Complete-Decision
