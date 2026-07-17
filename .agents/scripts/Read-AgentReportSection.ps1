[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $ReportPath,
	[Parameter(Mandatory)][string] $ExpectedSha256,
	[Parameter(Mandatory)][string] $Range
)

$ErrorActionPreference = 'Stop'
$workflowModule = Join-Path $PSScriptRoot 'FinalizeWorkflowCommon.psm1'
$artifactStore = Join-Path $PSScriptRoot 'AgentArtifactStore.psm1'
Import-Module $workflowModule -Force
Import-Module $artifactStore -Force -DisableNameChecking

function Assert-NoReparsePoint([string] $Root, [string] $Leaf) {
	$current = $Leaf
	while (-not $current.Equals($Root, [StringComparison]::OrdinalIgnoreCase)) {
		$item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
		if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
			throw "Reparse points are not allowed in report paths: '$current'."
		}
		$parent = Split-Path -Parent $current
		if ([string]::IsNullOrEmpty($parent) -or $parent.Equals($current, [StringComparison]::OrdinalIgnoreCase)) {
			throw "Report path escaped the worktree trust root while checking reparse points: '$Leaf'."
		}
		$current = $parent
	}
}

if ($ExpectedSha256 -cnotmatch '^[0-9a-f]{64}$') {
	throw 'ExpectedSha256 must be exactly 64 lowercase hexadecimal characters.'
}

$match = [regex]::Match($Range, '^L([1-9][0-9]*)-L([1-9][0-9]*)$')
if (-not $match.Success) { throw "Range must use exact grammar L<start>-L<end>: '$Range'." }
$startLine = [int64]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
$endLine = [int64]::Parse($match.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
if ($startLine -gt [int]::MaxValue -or $endLine -gt [int]::MaxValue -or $startLine -gt $endLine) {
	throw "Range is invalid or unsupported: '$Range'."
}

$worktree = (Get-FinalizeGitIdentity (Get-Location).Path 'Report reader worktree').Worktree
$canonicalReport = Assert-AgentArtifactPath -Worktree $worktree -Path $ReportPath
if (-not (Test-Path -LiteralPath $canonicalReport -PathType Leaf)) {
	throw "Report does not exist as a file: '$canonicalReport'."
}
Assert-NoReparsePoint -Root ([IO.Path]::GetDirectoryName($canonicalReport)) -Leaf $canonicalReport

$bytes = [IO.File]::ReadAllBytes($canonicalReport)
$actualHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
if (-not $actualHash.Equals($ExpectedSha256, [StringComparison]::Ordinal)) {
	throw "Report SHA-256 mismatch for '$canonicalReport': expected $ExpectedSha256, actual $actualHash."
}

$utf8 = [Text.UTF8Encoding]::new($false, $true)
try { $text = $utf8.GetString($bytes) }
catch [Text.DecoderFallbackException] { throw "Report is not strict UTF-8: '$canonicalReport'." }

$lineStarts = [Collections.Generic.List[int]]::new()
if ($text.Length -ne 0) {
	$lineStarts.Add(0)
	for ($index = 0; $index -lt $text.Length; ++$index) {
		if ($text[$index] -eq "`r") {
			if (($index + 1) -lt $text.Length -and $text[$index + 1] -eq "`n") { ++$index }
			if (($index + 1) -lt $text.Length) { $lineStarts.Add($index + 1) }
		}
		elseif ($text[$index] -eq "`n" -and ($index + 1) -lt $text.Length) {
			$lineStarts.Add($index + 1)
		}
	}
}

if ($endLine -gt $lineStarts.Count) {
	throw "Range '$Range' exceeds report line count $($lineStarts.Count)."
}
$startOffset = $lineStarts[[int]$startLine - 1]
$endOffset = if ($endLine -lt $lineStarts.Count) { $lineStarts[[int]$endLine] } else { $text.Length }
$section = $text.Substring($startOffset, $endOffset - $startOffset)
$sectionBytes = $utf8.GetByteCount($section)
if ($sectionBytes -gt 16384) {
	throw "Range '$Range' is $sectionBytes UTF-8 bytes; the per-range maximum is 16384 and output is never truncated."
}

Write-Output $section
