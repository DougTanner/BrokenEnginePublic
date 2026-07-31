# Advisory citation lookup for one plan file. It renders no verdict: a record with
# pathExists or lineExists false is a lead for the auditor, never a finding.
# Runs inside a delegated reviewer under the Codex read-only sandbox, so it reads
# only and writes its whole result to stdout; stderr carries diagnostics only.
[CmdletBinding()]
param(
	[Parameter(Position = 0)][string]$PlanPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$script:CitationPattern = '^[A-Za-z0-9_./-]+\.(h|cpp|md|ps1|py|txt|glsl)(:[0-9]+(-[0-9]+)?)?$'
$script:MaximumCitations = 48
$script:MaximumTextLength = 160
$script:MaximumExcerptLength = 120
$script:MaximumMessageLength = 256
$script:MaximumOutputBytes = 32768

$script:Result = [ordered]@{
	schemaVersion = 'broken-engine-plan-citations/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Plan citation lookup did not run.'
	plan = $null
	headings = $null
	citations = $null
	truncated = $false
}

function Write-Diagnostic
{
	param([string]$Message)

	[Console]::Error.WriteLine("PlanCitations: $Message")
}

function New-CappedText
{
	param([string]$Value, [int]$Limit)

	if ($Value.Length -le $Limit)
	{
		return [pscustomobject]@{ text = $Value; truncated = $false }
	}

	return [pscustomobject]@{ text = $Value.Substring(0, $Limit); truncated = $true }
}

function Get-RepositoryRelativePath
{
	# Returns $null for any token that canonically resolves outside the worktree root,
	# so a traversal or rooted token is treated as a non-repository token and never read.
	param([string]$RelativePath)

	if ([IO.Path]::IsPathRooted($RelativePath))
	{
		return $null
	}

	try
	{
		$fullPath = [IO.Path]::GetFullPath((Join-Path $script:RepoRoot $RelativePath))
	}
	catch
	{
		return $null
	}

	$rootWithSeparator = $script:RepoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
	if (-not $fullPath.StartsWith($rootWithSeparator, [StringComparison]::OrdinalIgnoreCase))
	{
		return $null
	}

	return [pscustomobject]@{
		fullPath = $fullPath
		relativePath = $fullPath.Substring($rootWithSeparator.Length).Replace([IO.Path]::DirectorySeparatorChar, '/')
	}
}

function Get-FileLines
{
	param([Collections.Generic.Dictionary[string, object]]$Cache, [string]$FullPath)

	if (-not $Cache.ContainsKey($FullPath))
	{
		$lines = $null
		if ([IO.File]::Exists($FullPath))
		{
			try
			{
				$lines = [IO.File]::ReadAllLines($FullPath)
			}
			catch
			{
				Write-Diagnostic "Cited file could not be read: $FullPath"
				$lines = $null
			}
		}

		$Cache[$FullPath] = $lines
	}

	$cached = $Cache[$FullPath]
	if ($null -eq $cached)
	{
		return $null
	}

	# The unary comma keeps a one-line file's single-element array from unrolling to a bare string.
	return , $cached
}

function Get-CitationRecords
{
	param([string[]]$PlanLines)

	$records = [Collections.Generic.List[object]]::new()
	$cache = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
	for ($index = 0; $index -lt $PlanLines.Count; ++$index)
	{
		foreach ($match in [regex]::Matches($PlanLines[$index], '`([^`]+)`'))
		{
			$token = $match.Groups[1].Value
			if ($token -cnotmatch $script:CitationPattern)
			{
				continue
			}

			$separator = $token.IndexOf(':')
			$pathPart = if ($separator -lt 0) { $token } else { $token.Substring(0, $separator) }
			$resolved = Get-RepositoryRelativePath $pathPart
			if ($null -eq $resolved)
			{
				continue
			}

			# A range citation resolves at its start line.
			$lineNumber = $null
			if ($separator -ge 0)
			{
				$startText = $token.Substring($separator + 1).Split('-')[0]
				$parsed = 0
				if ([int]::TryParse($startText, [ref]$parsed))
				{
					$lineNumber = $parsed
				}
			}

			$lines = Get-FileLines $cache $resolved.fullPath
			$pathExists = $null -ne $lines -or [IO.File]::Exists($resolved.fullPath)
			$lineExists = $null
			$excerpt = $null
			if ($null -ne $lineNumber)
			{
				$lineExists = $null -ne $lines -and $lineNumber -ge 1 -and $lineNumber -le $lines.Count
				if ($lineExists)
				{
					$excerpt = $lines[$lineNumber - 1].Trim()
				}
			}

			$null = $records.Add([pscustomobject]@{
				citation = $token
				planLine = $index + 1
				path = $resolved.relativePath
				pathExists = $pathExists
				lineNumber = $lineNumber
				lineExists = $lineExists
				excerpt = $excerpt
			})
		}
	}

	return , $records
}

function Convert-ToOutputCitation
{
	param([object]$Record)

	$citation = New-CappedText $Record.citation $script:MaximumTextLength
	$path = New-CappedText $Record.path $script:MaximumTextLength
	$excerpt = if ($null -eq $Record.excerpt) { $null } else { New-CappedText $Record.excerpt $script:MaximumExcerptLength }
	return [pscustomobject][ordered]@{
		citation = $citation.text
		citationTruncated = $citation.truncated
		planLine = $Record.planLine
		path = $path.text
		pathTruncated = $path.truncated
		pathExists = $Record.pathExists
		lineNumber = $Record.lineNumber
		lineExists = $Record.lineExists
		excerpt = if ($null -eq $excerpt) { $null } else { $excerpt.text }
		excerptTruncated = if ($null -eq $excerpt) { $false } else { $excerpt.truncated }
	}
}

function Complete-PlanCitations
{
	param([int]$ExitCode, [string]$Status, [string]$Code, [string]$Message)

	$script:Result.status = $Status
	$script:Result.code = $Code
	$script:Result.message = (New-CappedText $Message $script:MaximumMessageLength).text
	[Console]::Out.Write(($script:Result | ConvertTo-Json -Depth 8 -Compress))
	exit $ExitCode
}

function Set-CitationPayload
{
	param([Collections.Generic.List[object]]$Records)

	$items = [Collections.Generic.List[object]]::new()
	foreach ($record in $Records | Select-Object -First $script:MaximumCitations)
	{
		$null = $items.Add((Convert-ToOutputCitation $record))
	}

	# Shed the lowest-priority records until the whole envelope fits the output cap.
	while ($true)
	{
		$script:Result.citations = [ordered]@{
			items = @($items)
			totalCount = $Records.Count
			omittedCount = $Records.Count - $items.Count
		}
		$script:Result.truncated = $items.Count -lt $Records.Count -or @($items | Where-Object { $_.citationTruncated -or $_.pathTruncated -or $_.excerptTruncated }).Count -gt 0
		$json = $script:Result | ConvertTo-Json -Depth 8 -Compress
		if ([Text.Encoding]::UTF8.GetByteCount($json) -le $script:MaximumOutputBytes -or $items.Count -eq 0)
		{
			return
		}
		$items.RemoveAt($items.Count - 1)
	}
}

[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)

try
{
	if ([string]::IsNullOrWhiteSpace($PlanPath))
	{
		Complete-PlanCitations 1 'error' 'input.plan-invalid' 'PlanPath must name one plan file.'
	}

	try
	{
		$planFullPath = if ([IO.Path]::IsPathFullyQualified($PlanPath))
		{
			[IO.Path]::GetFullPath($PlanPath)
		}
		else
		{
			[IO.Path]::GetFullPath((Join-Path $script:RepoRoot $PlanPath))
		}
	}
	catch
	{
		Complete-PlanCitations 1 'error' 'input.plan-invalid' "Plan path cannot be resolved: $PlanPath"
	}

	if (-not [IO.File]::Exists($planFullPath))
	{
		Complete-PlanCitations 1 'error' 'input.plan-missing' "Plan file does not exist: $PlanPath"
	}

	try
	{
		$planLines = [IO.File]::ReadAllLines($planFullPath)
	}
	catch
	{
		Complete-PlanCitations 1 'error' 'input.plan-read-failed' "Plan file could not be read: $PlanPath"
	}

	$planResolved = Get-RepositoryRelativePath ([IO.Path]::GetRelativePath($script:RepoRoot, $planFullPath))
	$planReportedPath = if ($null -eq $planResolved) { $planFullPath.Replace([IO.Path]::DirectorySeparatorChar, '/') } else { $planResolved.relativePath }
	$planReported = New-CappedText $planReportedPath $script:MaximumTextLength
	$script:Result.plan = [ordered]@{
		path = $planReported.text
		pathTruncated = $planReported.truncated
		lineCount = $planLines.Count
	}
	$script:Result.headings = [ordered]@{
		inScopePresent = @($planLines | Where-Object { $_ -cmatch '^##\s+In scope\s*$' }).Count -gt 0
		outOfScopePresent = @($planLines | Where-Object { $_ -cmatch '^##\s+Out of scope\s*$' }).Count -gt 0
	}

	Set-CitationPayload (Get-CitationRecords $planLines)
	Complete-PlanCitations 0 'pass' 'ok' 'Plan citation lookup completed.'
}
catch
{
	Write-Diagnostic $_.Exception.Message
	Complete-PlanCitations 1 'error' 'internal.error' 'The plan citation lookup encountered an internal error.'
}
