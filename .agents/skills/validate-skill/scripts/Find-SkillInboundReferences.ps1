# Raw inbound-reference sweep for one skill name over the fixed root set in
# validate-skill's Workflow step 4. The script classifies nothing: it reports
# literal hits, and the validating agent decides which are invocation
# requirements and which are user-typed examples.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $SkillName
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$script:MaximumRecords = 150
$script:MaximumTextLength = 160
$script:MaximumMessageLength = 256
$script:MaximumOutputBytes = 32768

# The documented root set. Each tracked file is attributed to the first root it
# matches, so per-root counts never double-count a file.
$script:SweepRoots = @(
	[ordered]@{ name = 'AGENTS.md'; prefix = $null }
	[ordered]@{ name = '.agents/skills/'; prefix = '.agents/skills/' }
	[ordered]@{ name = '.agents/references/'; prefix = '.agents/references/' }
	[ordered]@{ name = 'Documents/'; prefix = 'Documents/' }
	[ordered]@{ name = '.claude/agents/'; prefix = '.claude/agents/' }
	[ordered]@{ name = '.codex/agents/'; prefix = '.codex/agents/' }
)

# Every root starts at zero so even an early failure envelope reports a count per
# documented root.
$script:InitialRootHitCounts = [ordered]@{}
foreach ($sweepRoot in $script:SweepRoots) { $script:InitialRootHitCounts[$sweepRoot.name] = 0 }

$result = [ordered]@{
	schemaVersion = 'broken-engine-skill-inbound-references/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Inbound reference sweep did not run.'
	skillName = $SkillName
	roots = @($script:SweepRoots | ForEach-Object { $_.name })
	rootHitCounts = $script:InitialRootHitCounts
	totalHits = 0
	recordLimit = $script:MaximumRecords
	truncated = $false
	records = @()
}

function Complete-InboundSweep([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message)
{
	$result.status = $Status
	$result.code = $Code
	$result.message = if ($Message.Length -gt $script:MaximumMessageLength) { $Message.Substring(0, $script:MaximumMessageLength) } else { $Message }
	$json = $result | ConvertTo-Json -Depth 32 -Compress
	while ([Text.Encoding]::UTF8.GetByteCount($json) -gt $script:MaximumOutputBytes -and $result.records.Count -gt 0)
	{
		$result.records = @($result.records | Select-Object -First ($result.records.Count - 1))
		$result.truncated = $true
		$json = $result | ConvertTo-Json -Depth 32 -Compress
	}

	[Console]::Out.Write($json)
	exit $ExitCode
}

function Get-SweepRootName([string] $RelativePath)
{
	foreach ($root in $script:SweepRoots)
	{
		if ($null -eq $root.prefix)
		{
			if ([IO.Path]::GetFileName($RelativePath) -ceq 'AGENTS.md') { return $root.name }
			continue
		}

		if ($RelativePath.StartsWith($root.prefix, [StringComparison]::Ordinal)) { return $root.name }
	}

	return $null
}

function Test-BinaryFile([string] $FullPath)
{
	$stream = [IO.File]::OpenRead($FullPath)
	try
	{
		$buffer = [byte[]]::new(8192)
		$read = $stream.Read($buffer, 0, $buffer.Length)
		for ($index = 0; $index -lt $read; ++$index)
		{
			if ($buffer[$index] -eq 0) { return $true }
		}
	}
	finally
	{
		$stream.Dispose()
	}

	return $false
}

try
{
	if ([string]::IsNullOrWhiteSpace($SkillName) -or $SkillName -cmatch '\s')
	{
		Complete-InboundSweep 1 'error' 'inbound-references.invalid-skill-name' "SkillName must be a single non-blank token: '$SkillName'."
	}

	Push-Location -LiteralPath $script:RepoRoot
	try
	{
		# git ls-files keeps the sweep to tracked files and does not descend into
		# the .claude/skills symlink, so hits are never reported twice.
		$tracked = @(& git ls-files) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
		if ($LASTEXITCODE -ne 0)
		{
			Complete-InboundSweep 1 'error' 'inbound-references.git-failed' 'git ls-files failed in the repository root.'
		}
	}
	finally
	{
		Pop-Location
	}

	$records = [Collections.Generic.List[object]]::new()
	foreach ($relative in $tracked)
	{
		$rootName = Get-SweepRootName $relative
		if ($null -eq $rootName) { continue }

		$fullPath = Join-Path $script:RepoRoot $relative
		if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { continue }
		if (Test-BinaryFile $fullPath) { continue }

		$lineNumber = 0
		foreach ($line in [IO.File]::ReadLines($fullPath))
		{
			++$lineNumber
			if ($line.IndexOf($SkillName, [StringComparison]::Ordinal) -lt 0) { continue }

			++$result.totalHits
			$result.rootHitCounts[$rootName] = $result.rootHitCounts[$rootName] + 1
			if ($records.Count -ge $script:MaximumRecords)
			{
				$result.truncated = $true
				continue
			}

			$text = $line.Trim()
			if ($text.Length -gt $script:MaximumTextLength) { $text = $text.Substring(0, $script:MaximumTextLength) }
			$records.Add([ordered]@{ path = $relative; line = $lineNumber; text = $text })
		}
	}

	$result.records = @($records)
	Complete-InboundSweep 0 'pass' 'ok' "Swept $($result.roots.Count) roots for '$SkillName': $($result.totalHits) hits."
}
catch
{
	Complete-InboundSweep 1 'error' 'internal.error' $_.Exception.Message
}
