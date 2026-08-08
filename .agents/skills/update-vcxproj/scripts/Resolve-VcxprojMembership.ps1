# Read-only membership resolver for /update-vcxproj: derives whole-file client/server
# affinity from a source file's bytes and the owning project/item/filter from its path.
# It opens no .vcxproj/.filters XML and writes nothing but stdout/stderr; every judgment
# call (authority conflicts, forced-include exceptions, XML edits) stays with the agent.
[CmdletBinding(PositionalBinding = $false)]
param(
	# Remaining arguments are collected so the documented 'pwsh -NoProfile -File' invocation
	# can pass several space-separated paths; -File hands each token over as a separate string.
	# Not mandatory: a binding-time failure would emit raw PowerShell errors instead of the envelope.
	[Parameter(ValueFromRemainingArguments)][string[]] $FilePath,
	[string] $RepositoryRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'AgentScriptCommon.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
Import-Module (Join-Path $sharedScripts 'AgentScriptCommon.psm1') -Force

$script:MaximumRecords = 16
$script:MaximumTextLength = 160
$script:MaximumMessageLength = 256
$script:MaximumOutputBytes = 8192

$script:ShaderStageExtensions = @('.vert', '.frag', '.comp', '.geom', '.tesc', '.tese', '.glsl')
$script:DocumentationExtensions = @('.md', '.txt')

$script:Records = [System.Collections.Generic.List[object]]::new()
$script:RequestedCount = 0

$result = [ordered]@{
	schemaVersion = 'broken-engine-vcxproj-membership/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Membership resolution did not run.'
	repositoryRoot = ''
	files = [ordered]@{
		items = @()
		totalCount = 0
		omittedCount = 0
		truncated = $false
	}
}

function Complete-VcxprojMembership
{
	param([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message)

	if ($Message.Length -gt $script:MaximumMessageLength)
	{
		$Message = $Message.Substring(0, $script:MaximumMessageLength)
	}

	$items = [System.Collections.Generic.List[object]]::new($script:Records)
	# Counts both the requests past the record cap and, when a whole-run error stops the batch
	# early, the requests never processed, so totalCount always reconciles with items + omitted.
	$omitted = $script:RequestedCount - $script:Records.Count
	while ($true)
	{
		$result.status = $Status
		$result.code = $Code
		$result.message = $Message
		$result.files.items = @($items)
		$result.files.totalCount = $script:RequestedCount
		$result.files.omittedCount = $omitted
		$result.files.truncated = $omitted -gt 0
		$json = $result | ConvertTo-Json -Depth 8 -Compress
		if ([Text.Encoding]::UTF8.GetByteCount($json) -le $script:MaximumOutputBytes -or $items.Count -eq 0)
		{
			break
		}

		$items.RemoveAt($items.Count - 1)
		$omitted++
	}

	if ($Status -cne 'pass')
	{
		[Console]::Error.WriteLine("Resolve-VcxprojMembership: $Code — $Message")
	}
	[Console]::Out.Write($json)
	exit $ExitCode
}

function Get-ReportedText
{
	param([string] $Value)

	if ($Value.Length -le $script:MaximumTextLength)
	{
		return $Value
	}

	return $Value.Substring(0, $script:MaximumTextLength)
}

# Strips comments so a line's remaining code text decides whether it is substantive.
# String and character literals are skipped whole so a literal '/*' cannot open a comment.
function Get-CodeText
{
	param([string] $Line, [ref] $InBlockComment)

	$builder = [Text.StringBuilder]::new()
	$index = 0
	while ($index -lt $Line.Length)
	{
		$character = $Line[$index]
		if ($InBlockComment.Value)
		{
			if ($character -eq '*' -and ($index + 1) -lt $Line.Length -and $Line[$index + 1] -eq '/')
			{
				$InBlockComment.Value = $false
				$index += 2
				continue
			}

			$index++
			continue
		}

		if ($character -eq '/' -and ($index + 1) -lt $Line.Length)
		{
			if ($Line[$index + 1] -eq '/')
			{
				break
			}
			if ($Line[$index + 1] -eq '*')
			{
				$InBlockComment.Value = $true
				$index += 2
				continue
			}
		}

		if ($character -eq '"' -or $character -eq "'")
		{
			$quote = $character
			$null = $builder.Append($character)
			$index++
			while ($index -lt $Line.Length)
			{
				$inner = $Line[$index]
				$null = $builder.Append($inner)
				$index++
				if ($inner -eq '\' -and $index -lt $Line.Length)
				{
					$null = $builder.Append($Line[$index])
					$index++
					continue
				}
				if ($inner -eq $quote)
				{
					break
				}
			}

			continue
		}

		$null = $builder.Append($character)
		$index++
	}

	return $builder.ToString()
}

# Implements the SKILL.md whole-file affinity rule literally: skip the BOM, whitespace,
# comment, '#pragma once' and include prologue; require the exact guard form as the first
# substantive directive; track nesting to find that guard's matching '#endif'; require no
# substantive content after it.
function Resolve-FileAffinity
{
	param([string] $Text)

	$lines = $Text -split "\r?\n"
	$codeLines = [string[]]::new($lines.Length)
	$inBlockComment = $false
	for ($index = 0; $index -lt $lines.Length; $index++)
	{
		$codeLines[$index] = (Get-CodeText $lines[$index] ([ref]$inBlockComment)).Trim()
	}

	if ($inBlockComment)
	{
		return [ordered]@{
			affinity = 'unprovable'
			code = 'affinity.malformed-conditional'
			detail = 'An unterminated block comment leaves the file text ambiguous.'
		}
	}

	# One entry per open conditional block, holding whether that block already saw its #else,
	# so a second #else or an #elif after #else is caught at any nesting depth.
	$elseSeen = [System.Collections.Generic.List[bool]]::new()
	foreach ($codeLine in $codeLines)
	{
		if ($codeLine -cmatch '^#\s*(if|ifdef|ifndef)\b')
		{
			$elseSeen.Add($false)
		}
		elseif ($codeLine -cmatch '^#\s*(else|elif)\b')
		{
			if ($elseSeen.Count -eq 0)
			{
				return [ordered]@{
					affinity = 'unprovable'
					code = 'affinity.unbalanced-conditional'
					detail = 'An #else or #elif appears outside any conditional block.'
				}
			}

			if ($elseSeen[$elseSeen.Count - 1])
			{
				return [ordered]@{
					affinity = 'unprovable'
					code = 'affinity.malformed-conditional'
					detail = 'A conditional block carries a second #else or an #elif after its #else.'
				}
			}

			if ($codeLine -cmatch '^#\s*else\b')
			{
				$elseSeen[$elseSeen.Count - 1] = $true
			}
		}
		elseif ($codeLine -cmatch '^#\s*endif\b')
		{
			if ($elseSeen.Count -eq 0)
			{
				return [ordered]@{
					affinity = 'unprovable'
					code = 'affinity.unbalanced-conditional'
					detail = 'An #endif appears with no matching #if.'
				}
			}

			$elseSeen.RemoveAt($elseSeen.Count - 1)
		}
	}

	if ($elseSeen.Count -ne 0)
	{
		return [ordered]@{
			affinity = 'unprovable'
			code = 'affinity.unbalanced-conditional'
			detail = "$($elseSeen.Count) conditional block(s) are never closed by a matching #endif."
		}
	}

	$firstSubstantive = -1
	for ($index = 0; $index -lt $codeLines.Length; $index++)
	{
		$codeLine = $codeLines[$index]
		if ($codeLine.Length -eq 0 -or $codeLine -cmatch '^#\s*pragma\s+once\b' -or $codeLine -cmatch '^#\s*include\b')
		{
			continue
		}

		$firstSubstantive = $index
		break
	}

	if ($firstSubstantive -lt 0)
	{
		return [ordered]@{
			affinity = 'shared'
			code = 'affinity.no-whole-file-guard'
			detail = 'The file holds no substantive directive after its prologue.'
		}
	}

	$guardLine = $codeLines[$firstSubstantive]
	$guarded = ''
	if ($guardLine -ceq '#if defined(BT_CLIENT)')
	{
		$guarded = 'client'
	}
	elseif ($guardLine -ceq '#if defined(BT_SERVER)')
	{
		$guarded = 'server'
	}
	else
	{
		return [ordered]@{
			affinity = 'shared'
			code = 'affinity.no-whole-file-guard'
			detail = 'The first substantive directive is not an exact BT_CLIENT/BT_SERVER guard.'
		}
	}

	$depth = 1
	$guardEnd = -1
	for ($index = $firstSubstantive + 1; $index -lt $codeLines.Length; $index++)
	{
		$codeLine = $codeLines[$index]
		if ($codeLine -cmatch '^#\s*(if|ifdef|ifndef)\b')
		{
			$depth++
		}
		elseif ($depth -eq 1 -and $codeLine -cmatch '^#\s*(else|elif)\b')
		{
			return [ordered]@{
				affinity = 'shared'
				code = 'affinity.guard-has-alternative-branch'
				detail = 'The leading guard carries an #else/#elif branch, so it excludes no whole file.'
			}
		}
		elseif ($codeLine -cmatch '^#\s*endif\b')
		{
			$depth--
			if ($depth -eq 0)
			{
				$guardEnd = $index
				break
			}
		}
	}

	for ($index = $guardEnd + 1; $index -lt $codeLines.Length; $index++)
	{
		if ($codeLines[$index].Length -gt 0)
		{
			return [ordered]@{
				affinity = 'shared'
				code = 'affinity.content-after-guard'
				detail = 'Substantive content follows the guard, so the guard does not enclose the whole file.'
			}
		}
	}

	return [ordered]@{
		affinity = $guarded
		code = "affinity.whole-file-$guarded-guard"
		detail = 'The exact guard is the first substantive directive and its matching #endif is last.'
	}
}

function Get-FilterPath
{
	param([string] $Root, [string] $RelativePath, [int] $TrimSegments)

	$segments = @($RelativePath -split '/')
	$directories = @($segments | Select-Object -Skip $TrimSegments)
	$directories = @($directories | Select-Object -First ([Math]::Max(0, $directories.Count - 1)))
	if ($directories.Count -eq 0)
	{
		return $Root
	}

	return ($Root + '\' + ($directories -join '\'))
}

function Get-AffinityProjects
{
	param([string] $Affinity)

	switch ($Affinity)
	{
		'client' { return @('client') }
		'server' { return @('server') }
		'shared' { return @('client', 'server') }
	}

	return @()
}

# Mirrors the SKILL.md ownership table in its stated precedence: special cases first,
# generic extension rules last. Never guesses a project for a path no row covers.
function Resolve-PathMapping
{
	param([string] $RelativePath, [string] $Affinity)

	$unresolved = [ordered]@{
		mapping = 'unresolved'
		code = 'mapping.no-owning-row'
		projects = @()
		itemType = $null
		filter = $null
		detail = 'No ownership-table row covers this path; the agent decides membership.'
	}

	if ([string]::IsNullOrEmpty($RelativePath))
	{
		$unresolved.code = 'mapping.outside-repository'
		$unresolved.detail = 'The path lies outside the repository root, so no ownership row applies.'
		return $unresolved
	}

	$extension = [IO.Path]::GetExtension($RelativePath).ToLowerInvariant()
	if ($script:DocumentationExtensions -contains $extension)
	{
		return [ordered]@{
			mapping = 'non-member'
			code = 'mapping.deliberate-non-member'
			projects = @()
			itemType = $null
			filter = $null
			detail = 'Documentation is deliberately never a project item; this is not a blocker.'
		}
	}

	$isShaderStage = $script:ShaderStageExtensions -contains $extension
	if (-not $isShaderStage -and $extension -ne '.cpp' -and $extension -ne '.h')
	{
		$unresolved.code = 'mapping.unsupported-extension'
		$unresolved.detail = "Extension '$extension' is neither a project item type nor documentation."
		return $unresolved
	}

	$itemType = if ($isShaderStage) { 'None' } elseif ($extension -eq '.cpp') { 'ClCompile' } else { 'ClInclude' }

	$engineShaderTree = $RelativePath -imatch '^Engine/Data/Shaders/'
	$gameShaderTree = $RelativePath -imatch '^Projects/[^/]+/Data/Shaders/'
	$shaderFilterRoot = if ($engineShaderTree) { 'Engine\Data\Shaders' } else { 'Game\Data\Shaders' }
	$shaderTrimSegments = if ($engineShaderTree) { 3 } else { 4 }

	# Row 1 — GLSL stages/includes under an engine/game Data/Shaders tree.
	if ($isShaderStage -and ($engineShaderTree -or $gameShaderTree))
	{
		return [ordered]@{
			mapping = 'resolved'
			code = 'mapping.shader-stage'
			projects = @('client')
			itemType = 'None'
			filter = (Get-FilterPath $shaderFilterRoot $RelativePath $shaderTrimSegments)
			detail = 'Shader sources are None items in the game client project only.'
		}
	}

	# Row 2 — generated $(GameDataDirectory)\*.h headers (Local default Output\Data).
	if ($extension -eq '.h' -and $RelativePath -imatch '(^|/)Output/Data/')
	{
		return [ordered]@{
			mapping = 'conditional'
			code = 'mapping.generated-data-header-conditional'
			projects = @()
			itemType = 'ClInclude'
			filter = 'DataFiles'
			detail = 'Scoped game targets, but the client-only generated Shader.h is omitted from the server.'
		}
	}

	# Row 3 — .h under a shader tree: scoped game affinity if C++ consumed, otherwise client.
	if ($extension -eq '.h' -and ($engineShaderTree -or $gameShaderTree))
	{
		return [ordered]@{
			mapping = 'conditional'
			code = 'mapping.shader-tree-header-conditional'
			projects = @()
			itemType = $null
			filter = (Get-FilterPath $shaderFilterRoot $RelativePath $shaderTrimSegments)
			detail = 'C++ consumption decides ClInclude in both projects versus a client-only None item.'
		}
	}

	if ($isShaderStage)
	{
		$unresolved.detail = 'A shader stage outside an engine/game Data/Shaders tree matches no row.'
		return $unresolved
	}

	# Rows 4-6 — tool source trees.
	foreach ($tool in @('ToolCommon', 'AgentHarness', 'WorktreeCli'))
	{
		if ($RelativePath -imatch "^Tools/$tool/")
		{
			$projects = if ($tool -ceq 'ToolCommon') { @('AgentHarness', 'WorktreeCli') } else { @($tool) }
			return [ordered]@{
				mapping = 'resolved'
				code = "mapping.tools-$($tool.ToLowerInvariant())"
				projects = $projects
				itemType = $itemType
				filter = (Get-FilterPath $tool $RelativePath 2)
				detail = 'Tool source membership lands through the AgentTools promotion route.'
			}
		}
	}

	# Row 7 — DataPacker source.
	if ($RelativePath -imatch '^DataPacker/Source/')
	{
		return [ordered]@{
			mapping = 'resolved'
			code = 'mapping.datapacker-source'
			projects = @('DataPacker')
			itemType = $itemType
			filter = (Get-FilterPath 'DataPacker' $RelativePath 2)
			detail = 'DataPacker owns its own source tree.'
		}
	}

	$affinityProjects = @(Get-AffinityProjects $Affinity)

	# Rows 8-9 — engine and game source, owned by structural affinity.
	$engineSource = $RelativePath -imatch '^Engine/Source/'
	$gameSource = $RelativePath -imatch '^Projects/[^/]+/Source/'
	if ($engineSource -or $gameSource)
	{
		$filterRoot = if ($engineSource) { 'Engine' } else { 'Game' }
		$trimSegments = if ($engineSource) { 2 } else { 3 }
		if ($affinityProjects.Count -eq 0)
		{
			return [ordered]@{
				mapping = 'unresolved'
				code = 'mapping.affinity-unprovable'
				projects = @()
				itemType = $itemType
				filter = (Get-FilterPath $filterRoot $RelativePath $trimSegments)
				detail = 'Affinity is unprovable, so the owning projects cannot be derived from it.'
			}
		}

		return [ordered]@{
			mapping = 'resolved'
			code = if ($engineSource) { 'mapping.engine-source' } else { 'mapping.game-source' }
			projects = $affinityProjects
			itemType = $itemType
			filter = (Get-FilterPath $filterRoot $RelativePath $trimSegments)
			detail = 'Owning projects follow the whole-file affinity above.'
		}
	}

	# Row 10 — Common: game client/server by affinity; DataPacker only by scoped authority.
	if ($RelativePath -imatch '^Common/')
	{
		if ($affinityProjects.Count -eq 0)
		{
			return [ordered]@{
				mapping = 'unresolved'
				code = 'mapping.affinity-unprovable'
				projects = @()
				itemType = $itemType
				filter = (Get-FilterPath 'Common' $RelativePath 1)
				detail = 'Affinity is unprovable, so the owning projects cannot be derived from it.'
			}
		}

		return [ordered]@{
			mapping = 'conditional'
			code = 'mapping.common-datapacker-conditional'
			projects = $affinityProjects
			itemType = $itemType
			filter = (Get-FilterPath 'Common' $RelativePath 1)
			detail = 'Game membership is determined; DataPacker membership is per-file scoped authority.'
		}
	}

	return $unresolved
}

try
{
	if ([string]::IsNullOrWhiteSpace($RepositoryRoot))
	{
		$RepositoryRoot = Join-Path $PSScriptRoot '..\..\..\..'
	}
	$rootFullPath = Get-AgentCanonicalPath $RepositoryRoot
	if (-not (Test-Path -LiteralPath $rootFullPath -PathType Container))
	{
		Complete-VcxprojMembership 1 'error' 'input.repository-root-missing' "Repository root is not a directory: '$rootFullPath'."
	}
	$result.repositoryRoot = $rootFullPath.Replace([IO.Path]::DirectorySeparatorChar, '/')
	$rootWithSeparator = $rootFullPath.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar

	if ($null -eq $FilePath -or $FilePath.Count -eq 0)
	{
		Complete-VcxprojMembership 1 'error' 'input.no-file-paths' 'No file path was supplied; pass at least one source path to resolve.'
	}

	$script:RequestedCount = $FilePath.Count
	$selected = @($FilePath | Select-Object -First $script:MaximumRecords)

	foreach ($requestedPath in $selected)
	{
		$absolutePath = if ([IO.Path]::IsPathRooted($requestedPath)) { $requestedPath } else { Join-Path $rootFullPath $requestedPath }
		$absolutePath = Get-AgentCanonicalPath $absolutePath

		$relativePath = $null
		if ($absolutePath.StartsWith($rootWithSeparator, [StringComparison]::OrdinalIgnoreCase))
		{
			$relativePath = $absolutePath.Substring($rootWithSeparator.Length).Replace([IO.Path]::DirectorySeparatorChar, '/')
		}
		$reportedPath = if ($null -ne $relativePath) { $relativePath } else { $absolutePath.Replace([IO.Path]::DirectorySeparatorChar, '/') }

		# Classified before anything is opened, so project XML is refused rather than read.
		$requestedExtension = [IO.Path]::GetExtension($absolutePath).ToLowerInvariant()
		if ($requestedExtension -eq '.vcxproj' -or $requestedExtension -eq '.filters')
		{
			$null = $script:Records.Add([ordered]@{
				path = (Get-ReportedText $reportedPath)
				affinity = 'unprovable'
				affinityCode = 'affinity.project-xml-input'
				affinityDetail = 'Project XML is never opened here, so no affinity is derived from it.'
				mapping = 'unresolved'
				mappingCode = 'mapping.project-xml-input'
				projects = @()
				itemType = $null
				filter = $null
				mappingDetail = 'Pass the source paths whose membership is in question, not the project/filters pair.'
			})
			continue
		}

		if (-not (Test-Path -LiteralPath $absolutePath -PathType Leaf))
		{
			Complete-VcxprojMembership 1 'error' 'input.file-unreadable' "Requested path is not a readable file: '$absolutePath'."
		}

		$text = ''
		try
		{
			$text = [IO.File]::ReadAllText($absolutePath)
		}
		catch
		{
			Complete-VcxprojMembership 1 'error' 'input.file-unreadable' "Could not read '$absolutePath': $($_.Exception.Message)"
		}

		$affinity = Resolve-FileAffinity $text
		$mapping = Resolve-PathMapping $relativePath $affinity.affinity
		$null = $script:Records.Add([ordered]@{
			path = (Get-ReportedText $reportedPath)
			affinity = $affinity.affinity
			affinityCode = $affinity.code
			affinityDetail = (Get-ReportedText $affinity.detail)
			mapping = $mapping.mapping
			mappingCode = $mapping.code
			projects = @($mapping.projects)
			itemType = $mapping.itemType
			filter = $mapping.filter
			mappingDetail = (Get-ReportedText $mapping.detail)
		})
	}

	Complete-VcxprojMembership 0 'pass' 'ok' 'Resolved affinity and ownership mapping for the requested files.'
}
catch
{
	Complete-VcxprojMembership 1 'error' 'internal.error' $_.Exception.Message
}
