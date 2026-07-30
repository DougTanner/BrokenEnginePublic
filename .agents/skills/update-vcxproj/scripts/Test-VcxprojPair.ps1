[CmdletBinding()]
param(
	[string[]]$ProjectPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:MsbuildNamespace = 'http://schemas.microsoft.com/developer/msbuild/2003'
$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$script:MaximumPairs = 8
$script:MaximumViolations = 16
$script:MaximumTextLength = 96
$script:MaximumMessageLength = 256
$script:MaximumOutputBytes = 8192

function Get-CanonicalPath
{
	param([string]$Path)

	$fullPath = [IO.Path]::GetFullPath($Path)
	$rootWithSeparator = $script:RepoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
	if ($fullPath.StartsWith($rootWithSeparator, [StringComparison]::OrdinalIgnoreCase))
	{
		return $fullPath.Substring($rootWithSeparator.Length).Replace([IO.Path]::DirectorySeparatorChar, '/')
	}

	return $fullPath.Replace([IO.Path]::DirectorySeparatorChar, '/')
}

function Get-Sha256
{
	param([string]$Value)

	$bytes = [Text.Encoding]::UTF8.GetBytes($Value)
	$hash = [Security.Cryptography.SHA256]::HashData($bytes)
	return [Convert]::ToHexString($hash).ToLowerInvariant()
}

function New-ReportedText
{
	param([string]$Value, [switch]$Identity)

	if ($Value.Length -le $script:MaximumTextLength)
	{
		return [pscustomobject][ordered]@{
			text = $Value
			truncated = $false
			sha256 = $null
		}
	}

	return [pscustomobject][ordered]@{
		text = $Value.Substring(0, $script:MaximumTextLength)
		truncated = $true
		sha256 = if ($Identity) { Get-Sha256 $Value } else { $null }
	}
}

function Add-Violation
{
	param(
		[System.Collections.Generic.List[object]]$Violations,
		[string]$PairPath,
		[string]$Code,
		[string]$Identity
	)

	foreach ($existing in $Violations)
	{
		if ($existing.PairPath -ceq $PairPath -and $existing.Code -ceq $Code -and $existing.Identity -ceq $Identity)
		{
			return
		}
	}

	$null = $Violations.Add([pscustomobject][ordered]@{
		PairPath = $PairPath
		Code = $Code
		Identity = $Identity
	})
}

function Get-InputPath
{
	param([string]$InputPath)

	if ([string]::IsNullOrWhiteSpace($InputPath))
	{
		return [pscustomobject]@{ code = 'input.project-invalid'; message = 'ProjectPath contains a blank path.' }
	}

	try
	{
		$fullPath = if ([IO.Path]::IsPathFullyQualified($InputPath))
		{
			[IO.Path]::GetFullPath($InputPath)
		}
		else
		{
			[IO.Path]::GetFullPath((Join-Path $script:RepoRoot $InputPath))
		}
	}
	catch
	{
		return [pscustomobject]@{ code = 'input.project-invalid'; message = "Project path cannot be resolved: $InputPath" }
	}

	if (-not ([IO.Path]::GetExtension($fullPath) -ieq '.vcxproj'))
	{
		return [pscustomobject]@{ code = 'input.extension-invalid'; message = "Project path must end in .vcxproj: $InputPath" }
	}

	if (-not [IO.File]::Exists($fullPath))
	{
		return [pscustomobject]@{ code = 'input.project-missing'; message = "Project file does not exist: $InputPath" }
	}

	$filtersPath = "$fullPath.filters"
	if (-not [IO.File]::Exists($filtersPath))
	{
		return [pscustomobject]@{ code = 'input.filters-missing'; message = "Adjacent filters file does not exist: $filtersPath" }
	}

	return [pscustomobject]@{ path = $fullPath; filtersPath = $filtersPath; code = $null; message = $null }
}

function Read-ProjectXml
{
	param([string]$Path)

	$reader = $null
	$stream = $null
	try
	{
		$bytes = [IO.File]::ReadAllBytes($Path)
	}
	catch
	{
		return [pscustomobject]@{ document = $null; readFailed = $true }
	}

	try
	{
		$settings = [Xml.XmlReaderSettings]::new()
		$settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
		$settings.XmlResolver = $null
		$stream = [IO.MemoryStream]::new($bytes, $false)
		$reader = [Xml.XmlReader]::Create($stream, $settings)
		$document = [Xml.XmlDocument]::new()
		$document.XmlResolver = $null
		$document.Load($reader)
		$reader.Dispose()
		$stream.Dispose()
		return [pscustomobject]@{ document = $document; readFailed = $false }
	}
	catch
	{
		if ($null -ne $reader)
		{
			$reader.Dispose()
		}
		if ($null -ne $stream)
		{
			$stream.Dispose()
		}
		return [pscustomobject]@{ document = $null; readFailed = $false }
	}
}

function Test-MsbuildNamespace
{
	param([Xml.XmlDocument]$Document)

	return $null -ne $Document.DocumentElement -and $Document.DocumentElement.NamespaceURI -ceq $script:MsbuildNamespace
}

function Get-ItemIdentityKey
{
	param([string]$Type, [string]$Include)

	return "$($Type.Length):$Type$($Include.Length):$Include"
}

function Get-CandidateItems
{
	param([Xml.XmlDocument]$Document)

	$items = [System.Collections.Generic.List[object]]::new()
	foreach ($group in $Document.DocumentElement.ChildNodes)
	{
		if ($group.NodeType -ne [Xml.XmlNodeType]::Element -or $group.NamespaceURI -cne $script:MsbuildNamespace -or $group.LocalName -cne 'ItemGroup')
		{
			continue
		}

		foreach ($element in $group.ChildNodes)
		{
			if ($element.NodeType -ne [Xml.XmlNodeType]::Element -or $element.NamespaceURI -cne $script:MsbuildNamespace -or -not $element.HasAttribute('Include'))
			{
				continue
			}

			if ($element.LocalName -ceq 'ClCompile' -or $element.LocalName -ceq 'ClInclude' -or $element.LocalName -ceq 'None')
			{
				$include = $element.GetAttribute('Include')
				$null = $items.Add([pscustomobject][ordered]@{
					key = Get-ItemIdentityKey $element.LocalName $include
					identity = "$($element.LocalName)|$include"
					element = $element
				})
			}
		}
	}

	return ,$items
}

function New-IdentityIndex
{
	param([System.Collections.Generic.List[object]]$Items)

	$index = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[object]]]::new([StringComparer]::Ordinal)
	foreach ($item in $Items)
	{
		if (-not $index.ContainsKey($item.key))
		{
			$index.Add($item.key, [System.Collections.Generic.List[object]]::new())
		}
		$null = $index[$item.key].Add($item)
	}

	return ,$index
}

function Get-FilterAssignments
{
	param(
		[System.Collections.Generic.List[object]]$FilterItems,
		[string]$PairPath,
		[System.Collections.Generic.List[object]]$Violations
	)

	$usedFilters = [System.Collections.Generic.List[string]]::new()
	foreach ($item in $FilterItems)
	{
		$assignments = [System.Collections.Generic.List[Xml.XmlElement]]::new()
		foreach ($child in $item.element.ChildNodes)
		{
			if ($child.NodeType -eq [Xml.XmlNodeType]::Element -and $child.NamespaceURI -ceq $script:MsbuildNamespace -and $child.LocalName -ceq 'Filter')
			{
				$null = $assignments.Add($child)
			}
		}

		if ($assignments.Count -eq 0)
		{
			Add-Violation $Violations $PairPath 'filter.assignment-missing' $item.identity
			continue
		}
		if ($assignments.Count -ne 1)
		{
			Add-Violation $Violations $PairPath 'filter.assignment-duplicate' $item.identity
			continue
		}

		$value = $assignments[0].InnerText
		if ([string]::IsNullOrWhiteSpace($value))
		{
			Add-Violation $Violations $PairPath 'filter.assignment-blank' $item.identity
			continue
		}

		$null = $usedFilters.Add($value)
	}

	return ,$usedFilters
}

function Get-FilterDeclarations
{
	param([Xml.XmlDocument]$Document)

	$declarations = [System.Collections.Generic.List[object]]::new()
	foreach ($group in $Document.DocumentElement.ChildNodes)
	{
		if ($group.NodeType -ne [Xml.XmlNodeType]::Element -or $group.NamespaceURI -cne $script:MsbuildNamespace -or $group.LocalName -cne 'ItemGroup')
		{
			continue
		}

		foreach ($element in $group.ChildNodes)
		{
			if ($element.NodeType -eq [Xml.XmlNodeType]::Element -and $element.NamespaceURI -ceq $script:MsbuildNamespace -and $element.LocalName -ceq 'Filter')
			{
				$null = $declarations.Add([pscustomobject][ordered]@{
					name = $element.GetAttribute('Include')
					element = $element
				})
			}
		}
	}

	return ,$declarations
}

function Get-FilterDeclarationIndex
{
	param([System.Collections.Generic.List[object]]$Declarations)

	$index = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[object]]]::new([StringComparer]::Ordinal)
	foreach ($declaration in $Declarations)
	{
		if (-not $index.ContainsKey($declaration.name))
		{
			$index.Add($declaration.name, [System.Collections.Generic.List[object]]::new())
		}
		$null = $index[$declaration.name].Add($declaration)
	}

	return ,$index
}

function Get-DirectGuidElements
{
	param([Xml.XmlElement]$Declaration)

	$guids = [System.Collections.Generic.List[Xml.XmlElement]]::new()
	foreach ($child in $Declaration.ChildNodes)
	{
		if ($child.NodeType -eq [Xml.XmlNodeType]::Element -and $child.NamespaceURI -ceq $script:MsbuildNamespace -and $child.LocalName -ceq 'UniqueIdentifier')
		{
			$null = $guids.Add($child)
		}
	}

	return ,$guids
}

function Get-ParentFilterNames
{
	param([string]$FilterName)

	$parents = [System.Collections.Generic.List[string]]::new()
	$position = $FilterName.LastIndexOf('\', [StringComparison]::Ordinal)
	while ($position -gt 0)
	{
		$null = $parents.Add($FilterName.Substring(0, $position))
		$position = $FilterName.LastIndexOf('\', $position - 1, [StringComparison]::Ordinal)
	}

	return ,$parents
}

function Test-Pair
{
	param(
		[string]$ProjectFile,
		[string]$FiltersFile,
		[System.Collections.Generic.List[object]]$PairRecords,
		[System.Collections.Generic.List[object]]$Violations
	)

	$pairPath = Get-CanonicalPath $ProjectFile
	$projectRead = Read-ProjectXml $ProjectFile
	$filtersRead = Read-ProjectXml $FiltersFile
	if ($projectRead.readFailed -or $filtersRead.readFailed)
	{
		throw 'input.read-failed'
	}

	$record = [pscustomobject][ordered]@{
		CanonicalPath = $pairPath
		ProjectPath = Get-CanonicalPath $ProjectFile
		FiltersPath = Get-CanonicalPath $FiltersFile
		ProjectItemCount = $null
		FilterItemCount = $null
		FilterDeclarationCount = $null
	}
	$null = $PairRecords.Add($record)

	if ($null -eq $projectRead.document)
	{
		Add-Violation $Violations $pairPath 'xml.parse-failed' 'project'
	}
	if ($null -eq $filtersRead.document)
	{
		Add-Violation $Violations $pairPath 'xml.parse-failed' 'filters'
	}
	if ($null -eq $projectRead.document -or $null -eq $filtersRead.document)
	{
		return
	}

	if (-not (Test-MsbuildNamespace $projectRead.document))
	{
		Add-Violation $Violations $pairPath 'xml.namespace-invalid' 'project'
	}
	if (-not (Test-MsbuildNamespace $filtersRead.document))
	{
		Add-Violation $Violations $pairPath 'xml.namespace-invalid' 'filters'
	}
	if (-not (Test-MsbuildNamespace $projectRead.document) -or -not (Test-MsbuildNamespace $filtersRead.document))
	{
		return
	}

	$projectItems = Get-CandidateItems $projectRead.document
	$filterItems = Get-CandidateItems $filtersRead.document
	$declarations = Get-FilterDeclarations $filtersRead.document
	$record.ProjectItemCount = $projectItems.Count
	$record.FilterItemCount = $filterItems.Count
	$record.FilterDeclarationCount = $declarations.Count

	$projectIndex = New-IdentityIndex $projectItems
	$filterIndex = New-IdentityIndex $filterItems
	foreach ($key in $projectIndex.Keys)
	{
		$projectEntries = $projectIndex[$key]
		if ($projectEntries.Count -gt 1)
		{
			Add-Violation $Violations $pairPath 'item.duplicate-project' $projectEntries[0].identity
		}
		if (-not $filterIndex.ContainsKey($key))
		{
			Add-Violation $Violations $pairPath 'item.missing-mirror' $projectEntries[0].identity
		}
	}
	foreach ($key in $filterIndex.Keys)
	{
		$filterEntries = $filterIndex[$key]
		if ($filterEntries.Count -gt 1)
		{
			Add-Violation $Violations $pairPath 'item.duplicate-filter' $filterEntries[0].identity
		}
		if (-not $projectIndex.ContainsKey($key))
		{
			Add-Violation $Violations $pairPath 'item.extra-filter' $filterEntries[0].identity
		}
	}

	$usedFilters = Get-FilterAssignments $filterItems $pairPath $Violations
	$declarationIndex = Get-FilterDeclarationIndex $declarations
	foreach ($declaration in $declarations)
	{
		if ([string]::IsNullOrWhiteSpace($declaration.name))
		{
			Add-Violation $Violations $pairPath 'filter.declaration-blank' $declaration.name
		}
		if ($declarationIndex[$declaration.name].Count -gt 1)
		{
			Add-Violation $Violations $pairPath 'filter.declaration-duplicate' $declaration.name
		}

		$guidElements = Get-DirectGuidElements $declaration.element
		if ($guidElements.Count -eq 0)
		{
			Add-Violation $Violations $pairPath 'filter.guid-missing' $declaration.name
			continue
		}
		if ($guidElements.Count -ne 1)
		{
			Add-Violation $Violations $pairPath 'filter.guid-duplicate-field' $declaration.name
			continue
		}
		$guidValue = $guidElements[0].InnerText
		if ($guidValue -notmatch '^\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}$')
		{
			Add-Violation $Violations $pairPath 'filter.guid-invalid' $declaration.name
		}
	}

	$guidIndex = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[object]]]::new([StringComparer]::OrdinalIgnoreCase)
	foreach ($declaration in $declarations)
	{
		$guidElements = Get-DirectGuidElements $declaration.element
		if ($guidElements.Count -eq 1)
		{
			$guidValue = $guidElements[0].InnerText
			if ($guidValue -match '^\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\}$')
			{
				if (-not $guidIndex.ContainsKey($guidValue))
				{
					$guidIndex.Add($guidValue, [System.Collections.Generic.List[object]]::new())
				}
				$null = $guidIndex[$guidValue].Add($declaration)
			}
		}
	}
	foreach ($guidValue in $guidIndex.Keys)
	{
		if ($guidIndex[$guidValue].Count -gt 1)
		{
			Add-Violation $Violations $pairPath 'filter.guid-duplicate' $guidValue
		}
	}

	foreach ($usedFilter in $usedFilters)
	{
		if (-not $declarationIndex.ContainsKey($usedFilter))
		{
			Add-Violation $Violations $pairPath 'filter.declaration-missing' $usedFilter
		}
		foreach ($parent in (Get-ParentFilterNames $usedFilter))
		{
			if (-not $declarationIndex.ContainsKey($parent))
			{
				Add-Violation $Violations $pairPath 'filter.ancestor-missing' $parent
			}
		}
	}
}

function Convert-ToOutputPair
{
	param([object]$Record)

	$projectPath = New-ReportedText $Record.ProjectPath -Identity
	$filtersPath = New-ReportedText $Record.FiltersPath -Identity
	$result = [ordered]@{
		projectPath = $projectPath.text
		filtersPath = $filtersPath.text
		projectItemCount = $Record.ProjectItemCount
		filterItemCount = $Record.FilterItemCount
		filterDeclarationCount = $Record.FilterDeclarationCount
	}
	if ($projectPath.truncated)
	{
		$result.projectPathSha256 = $projectPath.sha256
	}
	$result.projectPathTruncated = $projectPath.truncated
	if ($filtersPath.truncated)
	{
		$result.filtersPathSha256 = $filtersPath.sha256
	}
	$result.filtersPathTruncated = $filtersPath.truncated
	return [pscustomobject]$result
}

function Convert-ToOutputViolation
{
	param([object]$Violation)

	$pairPath = New-ReportedText $Violation.PairPath -Identity
	$identity = New-ReportedText $Violation.Identity -Identity
	$result = [ordered]@{
		pairPath = $pairPath.text
		code = $Violation.Code
		identity = $identity.text
	}
	if ($identity.truncated)
	{
		$result.identitySha256 = $identity.sha256
	}
	$result.identityTruncated = $identity.truncated
	if ($pairPath.truncated)
	{
		$result.pairPathSha256 = $pairPath.sha256
	}
	$result.pairPathTruncated = $pairPath.truncated
	return [pscustomobject]$result
}

function New-TerminalResult
{
	param(
		[string]$Status,
		[string]$Code,
		[System.Collections.Generic.List[object]]$PairRecords,
		[System.Collections.Generic.List[object]]$Violations,
		[string]$Message
	)

	$pairComparison = [System.Comparison[object]]{
		param($left, $right)
		return [StringComparer]::Ordinal.Compare([string]$left.CanonicalPath, [string]$right.CanonicalPath)
	}
	$PairRecords.Sort($pairComparison)
	$violationComparison = [System.Comparison[object]]{
		param($left, $right)
		$result = [StringComparer]::Ordinal.Compare([string]$left.PairPath, [string]$right.PairPath)
		if ($result -ne 0) { return $result }
		$result = [StringComparer]::Ordinal.Compare([string]$left.Code, [string]$right.Code)
		if ($result -ne 0) { return $result }
		return [StringComparer]::Ordinal.Compare([string]$left.Identity, [string]$right.Identity)
	}
	$Violations.Sort($violationComparison)

	$identityTruncatedCount = @($Violations | Where-Object { $_.Identity.Length -gt $script:MaximumTextLength }).Count
	$pairItems = [System.Collections.Generic.List[object]]::new()
	$violationItems = [System.Collections.Generic.List[object]]::new()
	foreach ($record in $PairRecords | Select-Object -First $script:MaximumPairs)
	{
		$null = $pairItems.Add((Convert-ToOutputPair $record))
	}
	foreach ($violation in $Violations | Select-Object -First $script:MaximumViolations)
	{
		$null = $violationItems.Add((Convert-ToOutputViolation $violation))
	}

	if ($Message.Length -gt $script:MaximumMessageLength)
	{
		$Message = $Message.Substring(0, $script:MaximumMessageLength)
	}

	while ($true)
	{
		$result = [ordered]@{
			schemaVersion = 'broken-engine-vcxproj-validation/v1'
			status = $Status
			code = $Code
			pairs = [ordered]@{
				items = @($pairItems)
				totalCount = $PairRecords.Count
				omittedCount = $PairRecords.Count - $pairItems.Count
			}
			violations = [ordered]@{
				items = @($violationItems)
				totalCount = $Violations.Count
				omittedCount = $Violations.Count - $violationItems.Count
			}
		}
		if (-not [string]::IsNullOrEmpty($Message))
		{
			$result.message = $Message
		}
		$result.identityTruncatedCount = $identityTruncatedCount
		$json = $result | ConvertTo-Json -Compress -Depth 8
		if ([Text.Encoding]::UTF8.GetByteCount($json) -le $script:MaximumOutputBytes)
		{
			return $json
		}
		if ($violationItems.Count -gt 0)
		{
			$violationItems.RemoveAt($violationItems.Count - 1)
			continue
		}
		if ($pairItems.Count -gt 0)
		{
			$pairItems.RemoveAt($pairItems.Count - 1)
			continue
		}
		if ($Message.Length -gt 0)
		{
			$Message = ''
			continue
		}
		return $json
	}
}

[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$pairRecords = [System.Collections.Generic.List[object]]::new()
$violations = [System.Collections.Generic.List[object]]::new()
$status = 'pass'
$code = 'ok'
$message = ''
$exitCode = 0

try
{
	if ($null -eq $ProjectPath -or $ProjectPath.Count -eq 0)
	{
		$status = 'error'
		$code = 'input.project-invalid'
		$message = 'ProjectPath must contain at least one path.'
		$exitCode = 1
	}
	foreach ($inputPath in $ProjectPath)
	{
		if ($exitCode -ne 0)
		{
			break
		}
		$inputResult = Get-InputPath $inputPath
		if ($null -ne $inputResult.code)
		{
			$status = 'error'
			$code = $inputResult.code
			$message = $inputResult.message
			$exitCode = 1
			break
		}
		Test-Pair $inputResult.path $inputResult.filtersPath $pairRecords $violations
	}
	if ($exitCode -eq 0 -and $violations.Count -gt 0)
	{
		$status = 'failed'
		$code = 'validation.failed'
		$exitCode = 2
	}
}
catch
{
	$status = 'error'
	$code = if ($_.Exception.Message -eq 'input.read-failed') { 'input.read-failed' } else { 'internal.error' }
	$message = if ($code -eq 'input.read-failed') { 'One or more input files could not be read.' } else { 'The validator encountered an internal error.' }
	$exitCode = 1
}

$json = New-TerminalResult $status $code $pairRecords $violations $message
[Console]::Out.Write($json)
exit $exitCode
