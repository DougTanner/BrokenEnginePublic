<#
.SYNOPSIS
Validates one repository skill package or one disposable fixture.

.DESCRIPTION
Pass exactly one -Path followed by a skill directory or SKILL.md. Add -Fixture
for a deliberately disposable target outside .agents/skills. Both option names
are case-insensitive; other options and duplicate options are setup errors.

.OUTPUTS
VALID/0, INVALID/1, or SETUP_ERROR/2. Run Get-Help -Full for this contract.
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[Parameter(ValueFromRemainingArguments = $true)]
	[object[]] $InvocationArguments
)

$ErrorActionPreference = 'Stop'
$scriptBoundParameters = @($PSBoundParameters.Keys)

function Write-SetupError
{
	param([string] $Code, [string] $Message)

	Write-Output "SETUP_ERROR ${Code}: $Message"
	exit 2
}

function Read-Invocation
{
	if ($scriptBoundParameters | Where-Object { $_ -cne 'InvocationArguments' })
	{
		Write-SetupError 'INVOCATION' 'provide exactly one -Path target'
	}

	$pathValues = [System.Collections.Generic.List[string]]::new()
	$fixtureCount = 0
	for ($i = 0; $i -lt $InvocationArguments.Count; $i++)
	{
		$argument = [string] $InvocationArguments[$i]
		if ($argument -ieq '-Path')
		{
			if (++$i -ge $InvocationArguments.Count -or ([string] $InvocationArguments[$i]).StartsWith('-'))
			{
				Write-SetupError 'INVOCATION' 'provide exactly one -Path target'
			}
			$pathValues.Add([string] $InvocationArguments[$i])
			continue
		}
		if ($argument -ieq '-Fixture')
		{
			$fixtureCount++
			continue
		}
		Write-SetupError 'INVOCATION' 'provide exactly one -Path target'
	}

	if ($pathValues.Count -ne 1 -or $fixtureCount -gt 1)
	{
		Write-SetupError 'INVOCATION' 'provide exactly one -Path target'
	}

	return [pscustomobject] @{ Path = $pathValues[0]; Fixture = $fixtureCount -eq 1 }
}

function Test-LexicalParentEscape
{
	param([string] $RelativePath)

	if ([System.IO.Path]::IsPathRooted($RelativePath))
	{
		return $true
	}
	$depth = 0
	foreach ($part in ($RelativePath -split '[\\/]'))
	{
		if ($part.Length -eq 0 -or $part -eq '.')
		{
			continue
		}
		if ($part -eq '..')
		{
			if ($depth -eq 0)
			{
				return $true
			}
			$depth--
			continue
		}
		$depth++
	}
	return $false
}

function Test-ReparseComponent
{
	param([string] $Root, [string] $Candidate)

	$rootPath = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
	$current = [System.IO.Path]::GetFullPath($Candidate).TrimEnd('\', '/')
	$relative = [System.IO.Path]::GetRelativePath($rootPath, $current)
	if (Test-LexicalParentEscape $relative)
	{
		return $true
	}
	while ($true)
	{
		$item = Get-Item -LiteralPath $current -Force -ErrorAction SilentlyContinue
		if ($null -ne $item -and ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint))
		{
			return $true
		}
		if ($current.Equals($rootPath, [System.StringComparison]::OrdinalIgnoreCase))
		{
			break
		}
		$parent = Split-Path -Parent $current
		if ([string]::IsNullOrWhiteSpace($parent) -or $parent -ceq $current)
		{
			return $true
		}
		$current = $parent
	}
	return $false
}

function Get-CharacterCount
{
	param([string] $Value)

	$count = 0
	foreach ($rune in $Value.EnumerateRunes())
	{
		$count++
	}
	return $count
}

function ConvertFrom-QuotedScalar
{
	param([string] $Value)

	if ($Value.Length -lt 2)
	{
		throw 'quoted scalar is not closed'
	}

	$quote = $Value[0]
	if ($Value[$Value.Length - 1] -ne $quote)
	{
		throw 'quoted scalar is not closed'
	}

	$inner = $Value.Substring(1, $Value.Length - 2)
	if ($quote -eq "'")
	{
		$remaining = $inner -replace "''", ''
		if ($remaining.Contains("'"))
		{
			throw 'single-quoted scalar contains an unescaped quote'
		}
		return $inner -replace "''", "'"
	}

	$builder = [System.Text.StringBuilder]::new()
	for ($i = 0; $i -lt $inner.Length; $i++)
	{
		$character = $inner[$i]
		if ($character -eq '"')
		{
			throw 'double-quoted scalar contains an unescaped quote'
		}
		if ($character -ne '\')
		{
			[void] $builder.Append($character)
			continue
		}

		if (++$i -ge $inner.Length)
		{
			throw 'double-quoted scalar ends with an incomplete escape'
		}

		switch ($inner[$i])
		{
			'"' { [void] $builder.Append('"') }
			'\' { [void] $builder.Append('\') }
			'n' { [void] $builder.Append("`n") }
			'r' { [void] $builder.Append("`r") }
			't' { [void] $builder.Append("`t") }
			'u'
			{
				if ($i + 4 -ge $inner.Length)
				{
					throw 'double-quoted scalar has an incomplete Unicode escape'
				}
				$digits = $inner.Substring($i + 1, 4)
				if ($digits -notmatch '^[0-9A-Fa-f]{4}$')
				{
					throw 'double-quoted scalar has an invalid Unicode escape'
				}
				[void] $builder.Append([char] [Convert]::ToInt32($digits, 16))
				$i += 4
			}
			default { throw "double-quoted scalar has unsupported escape \$($inner[$i])" }
		}
	}
	return $builder.ToString()
}

function ConvertFrom-TextScalar
{
	param([string] $Value, [switch] $AllowBrackets)

	$value = $Value.Trim()
	if ($value.Length -eq 0)
	{
		throw 'value must not be empty'
	}
	if ($value[0] -eq "'" -or $value[0] -eq '"')
	{
		return ConvertFrom-QuotedScalar $value
	}
	if ((-not $AllowBrackets -and $value -match '^[\[\]]') -or $value -match '^[{},&*!|>@`]' -or $value -match '^[-?:](?:\s|$)')
	{
		throw 'plain scalar starts with unsupported YAML syntax'
	}
	if ($value -match ':(?:\s|$)')
	{
		throw 'plain scalar contains unsupported YAML mapping syntax'
	}
	if ($value -match '(^|\s)#')
	{
		throw 'plain scalar contains unsupported YAML comment syntax'
	}
	return $value
}

function ConvertFrom-FoldedScalar
{
	param([System.Collections.Generic.List[string]] $Lines)

	$builder = [System.Text.StringBuilder]::new()
	$blankLineCount = 0
	foreach ($rawLine in $Lines)
	{
		$line = $rawLine.Trim()
		if ($line.Length -eq 0)
		{
			$blankLineCount++
			continue
		}
		if ($builder.Length -gt 0)
		{
			[void] $builder.Append($(if ($blankLineCount -gt 0) { "`n" * $blankLineCount } else { ' ' }))
		}
		[void] $builder.Append($line)
		$blankLineCount = 0
	}
	return $builder.ToString()
}

function Get-MarkdownSearchText
{
	param([string] $Body)

	$characters = $Body.ToCharArray()
	$inFence = $false
	$fenceCharacter = [char] 0
	$fenceLength = 0
	$offset = 0
	while ($offset -lt $Body.Length)
	{
		$newline = $Body.IndexOf("`n", $offset)
		$lineEnd = if ($newline -lt 0) { $Body.Length } else { $newline }
		$line = $Body.Substring($offset, $lineEnd - $offset)
		$maskLine = $inFence
		if ($inFence)
		{
			$closingPattern = '^ {0,3}{0}{{{1},}}[ \t]*$' -f [regex]::Escape([string] $fenceCharacter), $fenceLength
			if ($line -match $closingPattern)
			{
				$inFence = $false
			}
		}
		else
		{
			$fenceMatch = [regex]::Match($line, '^ {0,3}(?<fence>`{3,}|~{3,})')
			if ($fenceMatch.Success)
			{
				$fence = $fenceMatch.Groups['fence'].Value
				$fenceCharacter = $fence[0]
				$fenceLength = $fence.Length
				$inFence = $true
				$maskLine = $true
			}
		}
		if ($maskLine)
		{
			for ($i = $offset; $i -lt $lineEnd; $i++)
			{
				$characters[$i] = ' '
			}
		}
		$offset = if ($newline -lt 0) { $Body.Length } else { $newline + 1 }
	}

	for ($i = 0; $i -lt $characters.Length; $i++)
	{
		if ($characters[$i] -ne '`')
		{
			continue
		}
		$openingStart = $i
		while ($i -lt $characters.Length -and $characters[$i] -eq '`')
		{
			$i++
		}
		$openingLength = $i - $openingStart
		$closingStart = -1
		for ($candidate = $i; $candidate -lt $characters.Length; $candidate++)
		{
			if ($characters[$candidate] -ne '`')
			{
				continue
			}
			$runStart = $candidate
			while ($candidate -lt $characters.Length -and $characters[$candidate] -eq '`')
			{
				$candidate++
			}
			if ($candidate - $runStart -eq $openingLength)
			{
				$closingStart = $runStart
				break
			}
			$candidate--
		}
		if ($closingStart -lt 0)
		{
			$i--
			continue
		}
		$closingEnd = $closingStart + $openingLength
		for ($masked = $openingStart; $masked -lt $closingEnd; $masked++)
		{
			if ($characters[$masked] -ne "`n")
			{
				$characters[$masked] = ' '
			}
		}
		$i = $closingEnd - 1
	}
	return [string]::new($characters)
}

function ConvertFrom-MarkdownDestination
{
	param([string] $Value)

	$builder = [System.Text.StringBuilder]::new()
	for ($i = 0; $i -lt $Value.Length; $i++)
	{
		$character = $Value[$i]
		if ($character -eq '\' -and $i + 1 -lt $Value.Length)
		{
			$nextCode = [int] $Value[$i + 1]
			if (($nextCode -ge 0x21 -and $nextCode -le 0x2f) -or
				($nextCode -ge 0x3a -and $nextCode -le 0x40) -or
				($nextCode -ge 0x5b -and $nextCode -le 0x60) -or
				($nextCode -ge 0x7b -and $nextCode -le 0x7e))
			{
				$i++
				$character = $Value[$i]
			}
		}
		[void] $builder.Append($character)
	}
	return $builder.ToString()
}

function Get-MarkdownDestinations
{
	param([string] $Body)

	$searchBody = Get-MarkdownSearchText $Body
	$destinations = [System.Collections.Generic.List[object]]::new()
	$inlineStartPattern = '!?\[[^\]\r\n]*\]\('
	foreach ($match in [regex]::Matches($searchBody, $inlineStartPattern))
	{
		$start = $match.Index + $match.Length
		if ($start -ge $searchBody.Length)
		{
			continue
		}
		if ($searchBody[$start] -eq '<')
		{
			$end = $searchBody.IndexOf('>', $start + 1)
			if ($end -lt 0 -or $searchBody.Substring($start + 1, $end - $start - 1).Contains("`n"))
			{
				continue
			}
			$closure = $searchBody.Substring($end + 1)
			if ($closure -notmatch '^(?:\s+["''][^"'']*["''])?\)')
			{
				continue
			}
			$destinations.Add([pscustomobject] @{ Index = $match.Index; Target = $Body.Substring($start + 1, $end - $start - 1) })
			continue
		}

		$depth = 0
		for ($i = $start; $i -lt $searchBody.Length; $i++)
		{
			$character = $searchBody[$i]
			if ($character -eq '\')
			{
				$i++
				continue
			}
			if ($character -eq '(')
			{
				$depth++
				continue
			}
			if ($character -eq ')')
			{
				if ($depth -gt 0)
				{
					$depth--
					continue
				}
				$destinations.Add([pscustomobject] @{ Index = $match.Index; Target = $Body.Substring($start, $i - $start) })
				break
			}
			if ([char]::IsWhiteSpace($character) -and $depth -eq 0)
			{
				$closure = $searchBody.Substring($i)
				if ($closure -match '^\s+["''][^"'']*["'']\)')
				{
					$destinations.Add([pscustomobject] @{ Index = $match.Index; Target = $Body.Substring($start, $i - $start) })
				}
				break
			}
		}
	}
	$definitionPattern = '(?m)^\s{0,3}\[[^\]]+\]:\s*(?<destination><[^>]*>|\S+)'
	foreach ($match in [regex]::Matches($searchBody, $definitionPattern))
	{
		$destinations.Add([pscustomobject] @{ Index = $match.Index; Target = $match.Groups['destination'].Value.Trim('<', '>') })
	}
	return $destinations
}

function ConvertFrom-FlowList
{
	param([string] $Value)

	$value = $Value.Trim()
	if ($value.Length -lt 2 -or $value[0] -ne '[' -or $value[$value.Length - 1] -ne ']')
	{
		throw 'value must be a flow list enclosed in brackets'
	}

	$inner = $value.Substring(1, $value.Length - 2)
	if ($inner.Trim().Length -eq 0)
	{
		return , [string[]] @()
	}

	$items = [System.Collections.Generic.List[string]]::new()
	$start = 0
	$quote = [char] 0
	$escaped = $false
	for ($i = 0; $i -lt $inner.Length; $i++)
	{
		$character = $inner[$i]
		if ($quote -ne [char] 0)
		{
			if ($quote -eq '"' -and -not $escaped -and $character -eq '\')
			{
				$escaped = $true
				continue
			}
			if (-not $escaped -and $character -eq $quote)
			{
				$quote = [char] 0
			}
			$escaped = $false
			continue
		}

		if ($character -eq "'" -or $character -eq '"')
		{
			$quote = $character
			continue
		}
		if ($character -eq '[' -or $character -eq ']' -or $character -eq '{' -or $character -eq '}')
		{
			throw 'nested flow collections are not supported'
		}
		if ($character -eq ',')
		{
			$rawItem = $inner.Substring($start, $i - $start)
			$items.Add((ConvertFrom-TextScalar $rawItem))
			$start = $i + 1
		}
	}

	if ($quote -ne [char] 0)
	{
		throw 'flow list contains an unclosed quoted item'
	}
	$items.Add((ConvertFrom-TextScalar $inner.Substring($start)))
	return , $items.ToArray()
}

function ConvertFrom-OpenAiString
{
	param([string] $Value)

	$value = $Value.Trim()
	if ($value.Length -lt 2 -or $value[0] -notin @("'", '"'))
	{
		throw 'value must be a quoted string'
	}
	$result = ConvertFrom-QuotedScalar $value
	if ([string]::IsNullOrWhiteSpace($result))
	{
		throw 'value must not be empty'
	}
	return $result
}

function Read-OpenAiSidecar
{
	param([string] $SidecarPath, [string] $SidecarDisplayPath, [string] $SkillName, [string] $SkillDirectory)

	try
	{
		$sidecarBytes = [System.IO.File]::ReadAllBytes($SidecarPath)
	}
	catch
	{
		Write-SetupError 'READ' "cannot read Codex companion file: $($_.Exception.Message)"
	}
	try
	{
		$sidecarText = [System.Text.UTF8Encoding]::new($false, $true).GetString($sidecarBytes)
	}
	catch [System.Text.DecoderFallbackException]
	{
		Add-Diagnostic 1 'OPENAI001' 'agents/openai.yaml is not valid UTF-8' $SidecarDisplayPath 1
		return
	}
	if ($sidecarText.Length -gt 0 -and $sidecarText[0] -eq [char] 0xFEFF)
	{
		$sidecarText = $sidecarText.Substring(1)
	}
	$bareCarriageReturn = [regex]::Match($sidecarText, "`r(?!`n)")
	if ($bareCarriageReturn.Success)
	{
		$line = ([regex]::Matches($sidecarText.Substring(0, $bareCarriageReturn.Index), "`n")).Count + 1
		Add-Diagnostic $line 'OPENAI002' 'bare carriage return is not a supported line ending' $SidecarDisplayPath 1
	}

	$sidecarLines = $sidecarText.Replace("`r`n", "`n").Split("`n")
	$sections = @{}
	$sectionLines = @{}
	$section = ''
	$interface = @{}
	$interfaceLines = @{}
	$policy = @{}
	$policyLines = @{}
	$toolsSeen = $false
	$dependencyTools = [System.Collections.Generic.List[object]]::new()
	$currentTool = $null
	for ($i = 0; $i -lt $sidecarLines.Count; $i++)
	{
		$lineNumber = $i + 1
		$lineText = $sidecarLines[$i]
		if ([string]::IsNullOrWhiteSpace($lineText))
		{
			continue
		}
		if ($lineText.Contains("`t") -or $lineText.TrimStart().StartsWith('#'))
		{
			Add-Diagnostic $lineNumber 'OPENAI003' 'tabs and YAML comments are not supported' $SidecarDisplayPath 1
			continue
		}
		if ($lineText -cmatch '^(interface|policy|dependencies):$')
		{
			$section = $Matches[1]
			if ($sections.ContainsKey($section))
			{
				Add-Diagnostic $lineNumber 'OPENAI004' "duplicate top-level object: $section" $SidecarDisplayPath 1
			}
			else
			{
				$sections[$section] = $true
				$sectionLines[$section] = $lineNumber
			}
			$currentTool = $null
			continue
		}
		if ($lineText -cmatch '^\S')
		{
			Add-Diagnostic $lineNumber 'OPENAI005' 'unknown top-level field or malformed object' $SidecarDisplayPath 1
			continue
		}

		switch ($section)
		{
			'interface'
			{
				if ($lineText -cnotmatch '^  ([a-z_]+): (.+)$')
				{
					Add-Diagnostic $lineNumber 'OPENAI006' 'interface fields use two spaces and key: quoted-value' $SidecarDisplayPath 1
					continue
				}
				$key = $Matches[1]
				if ($key -notin @('display_name', 'short_description', 'icon_small', 'icon_large', 'brand_color', 'default_prompt'))
				{
					Add-Diagnostic $lineNumber 'OPENAI007' "unknown interface field: $key" $SidecarDisplayPath 1
					continue
				}
				if ($interface.ContainsKey($key))
				{
					Add-Diagnostic $lineNumber 'OPENAI008' "duplicate interface field: $key" $SidecarDisplayPath 1
					continue
				}
				try { $interface[$key] = ConvertFrom-OpenAiString $Matches[2] }
				catch { Add-Diagnostic $lineNumber 'OPENAI009' "interface.$key $($_.Exception.Message)" $SidecarDisplayPath 1 }
				$interfaceLines[$key] = $lineNumber
			}
			'policy'
			{
				if ($lineText -cnotmatch '^  allow_implicit_invocation: (true|false)$')
				{
					Add-Diagnostic $lineNumber 'OPENAI010' 'policy accepts only allow_implicit_invocation: true|false' $SidecarDisplayPath 1
					continue
				}
				if ($policy.ContainsKey('allow_implicit_invocation'))
				{
					Add-Diagnostic $lineNumber 'OPENAI011' 'duplicate policy field: allow_implicit_invocation' $SidecarDisplayPath 1
					continue
				}
				$policy['allow_implicit_invocation'] = $Matches[1] -ceq 'true'
				$policyLines['allow_implicit_invocation'] = $lineNumber
			}
			'dependencies'
			{
				if ($lineText -ceq '  tools:')
				{
					if ($toolsSeen) { Add-Diagnostic $lineNumber 'OPENAI012' 'duplicate dependencies.tools field' $SidecarDisplayPath 1 }
					$toolsSeen = $true
					continue
				}
				if ($lineText -cmatch '^    - type: (.+)$')
				{
					if (-not $toolsSeen)
					{
						Add-Diagnostic $lineNumber 'OPENAI016' 'dependencies tool entries must follow tools:' $SidecarDisplayPath 1
						continue
					}
					try { $type = ConvertFrom-OpenAiString $Matches[1] }
					catch { Add-Diagnostic $lineNumber 'OPENAI013' "dependencies.tools[].type $($_.Exception.Message)" $SidecarDisplayPath 1; $type = '' }
					$currentTool = [ordered] @{ type = $type; line = $lineNumber }
					$dependencyTools.Add($currentTool)
					continue
				}
				if ($lineText -cmatch '^      (value|description|transport|url): (.+)$' -and $null -ne $currentTool)
				{
					$key = $Matches[1]
					if ($currentTool.Contains($key)) { Add-Diagnostic $lineNumber 'OPENAI014' "duplicate dependencies.tools[].$key field" $SidecarDisplayPath 1; continue }
					try { $currentTool[$key] = ConvertFrom-OpenAiString $Matches[2] }
					catch { Add-Diagnostic $lineNumber 'OPENAI015' "dependencies.tools[].$key $($_.Exception.Message)" $SidecarDisplayPath 1 }
					continue
				}
				Add-Diagnostic $lineNumber 'OPENAI016' 'dependencies accepts a tools list of quoted mcp fields' $SidecarDisplayPath 1
			}
			default { Add-Diagnostic $lineNumber 'OPENAI005' 'field appears before a supported top-level object' $SidecarDisplayPath 1 }
		}
	}

	if ($sections.Count -eq 0)
	{
		Add-Diagnostic 1 'OPENAI017' 'agents/openai.yaml must declare interface, policy, or dependencies' $SidecarDisplayPath 1
	}
	if ($sections.ContainsKey('interface'))
	{
		foreach ($key in @('display_name', 'short_description'))
		{
			if (-not $interface.ContainsKey($key)) { Add-Diagnostic $sectionLines['interface'] 'OPENAI018' "interface requires $key" $SidecarDisplayPath 1 }
		}
		if ($interface.ContainsKey('short_description'))
		{
			$length = Get-CharacterCount ([string] $interface['short_description'])
			if ($length -lt 25 -or $length -gt 64) { Add-Diagnostic $interfaceLines['short_description'] 'OPENAI019' 'interface.short_description must be 25-64 characters' $SidecarDisplayPath 1 }
		}
		if ($interface.ContainsKey('brand_color') -and $interface['brand_color'] -cnotmatch '^#[0-9A-Fa-f]{6}$')
		{
			Add-Diagnostic $interfaceLines['brand_color'] 'OPENAI020' 'interface.brand_color must use #RRGGBB' $SidecarDisplayPath 1
		}
		if ($interface.ContainsKey('default_prompt') -and $interface['default_prompt'] -cnotmatch [regex]::Escape("`$$SkillName"))
		{
			Add-Diagnostic $interfaceLines['default_prompt'] 'OPENAI021' "interface.default_prompt must mention `$$SkillName" $SidecarDisplayPath 1
		}
		foreach ($key in @('icon_small', 'icon_large'))
		{
			if (-not $interface.ContainsKey($key)) { continue }
			$assetPath = [string] $interface[$key]
			try
			{
				if (Test-LexicalParentEscape $assetPath) { throw 'path escapes skill directory' }
				$candidate = [System.IO.Path]::GetFullPath((Join-Path $SkillDirectory $assetPath))
				$relativeCandidate = [System.IO.Path]::GetRelativePath($SkillDirectory, $candidate)
				if (Test-LexicalParentEscape $relativeCandidate) { throw 'path escapes skill directory' }
				if (-not [System.IO.File]::Exists($candidate)) { throw 'file does not exist' }
				if (Test-ReparseComponent $SkillDirectory $candidate) { throw 'path contains a reparse point' }
			}
			catch { Add-Diagnostic $interfaceLines[$key] 'OPENAI022' "interface.$key does not resolve inside skill directory: $($_.Exception.Message)" $SidecarDisplayPath 1 }
		}
	}
	if ($sections.ContainsKey('policy') -and -not $policy.ContainsKey('allow_implicit_invocation'))
	{
		Add-Diagnostic $sectionLines['policy'] 'OPENAI023' 'policy requires allow_implicit_invocation' $SidecarDisplayPath 1
	}
	if ($sections.ContainsKey('dependencies'))
	{
		if (-not $toolsSeen -or $dependencyTools.Count -eq 0) { Add-Diagnostic $sectionLines['dependencies'] 'OPENAI024' 'dependencies requires a nonempty tools list' $SidecarDisplayPath 1 }
		foreach ($tool in $dependencyTools)
		{
			if ($tool.type -cne 'mcp') { Add-Diagnostic $tool.line 'OPENAI025' 'dependencies.tools[].type must be mcp' $SidecarDisplayPath 1 }
			foreach ($key in @('value', 'description'))
			{
				if (-not $tool.Contains($key)) { Add-Diagnostic $tool.line 'OPENAI026' "dependencies.tools[] requires $key" $SidecarDisplayPath 1 }
			}
			if ($tool.Contains('url') -and $tool.url -cnotmatch '^https://') { Add-Diagnostic $tool.line 'OPENAI027' 'dependencies.tools[].url must use https' $SidecarDisplayPath 1 }
		}
	}
}

try
{
	$invocation = Read-Invocation
	$Path = $invocation.Path
	$Fixture = $invocation.Fixture

	$scriptDirectory = Split-Path -Parent $PSCommandPath
	$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDirectory '../../../..'))
	$skillsRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot '.agents/skills'))

	try
	{
		$resolvedTarget = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
	}
	catch
	{
		Write-SetupError 'PATH' "target does not exist: $Path"
	}

	if ([System.IO.Directory]::Exists($resolvedTarget))
	{
		$skillFile = Join-Path $resolvedTarget 'SKILL.md'
		if (-not [System.IO.File]::Exists($skillFile))
		{
			Write-SetupError 'PATH' "target directory has no SKILL.md: $resolvedTarget"
		}
	}
	elseif ([System.IO.File]::Exists($resolvedTarget) -and [System.IO.Path]::GetFileName($resolvedTarget) -ceq 'SKILL.md')
	{
		$skillFile = $resolvedTarget
	}
	else
	{
		Write-SetupError 'PATH' 'target must be a skill directory or a file named SKILL.md'
	}

	$skillFile = [System.IO.Path]::GetFullPath($skillFile)
	$skillDirectory = Split-Path -Parent $skillFile
	if (-not $Fixture)
	{
		$relativeToSkills = [System.IO.Path]::GetRelativePath($skillsRoot, $skillFile)
		$relativeParts = $relativeToSkills -split '[\\/]'
		if ((Test-LexicalParentEscape $relativeToSkills) -or $relativeParts.Count -ne 2 -or $relativeParts[1] -cne 'SKILL.md')
		{
			Write-SetupError 'SCOPE' 'repository validation targets must match .agents/skills/<name>/SKILL.md; use -Fixture for disposable fixtures'
		}
	}

	$relativeDisplay = [System.IO.Path]::GetRelativePath($repositoryRoot, $skillFile)
	if (Test-LexicalParentEscape $relativeDisplay)
	{
		$displayPath = $skillFile.Replace('\', '/')
	}
	else
	{
		$displayPath = $relativeDisplay.Replace('\', '/')
	}

	try
	{
		$bytes = [System.IO.File]::ReadAllBytes($skillFile)
	}
	catch
	{
		Write-SetupError 'READ' "cannot read target: $($_.Exception.Message)"
	}

	$diagnostics = [System.Collections.Generic.List[object]]::new()
	$diagnosticSequence = 0
	function Add-Diagnostic
	{
		param(
			[int] $Line,
			[string] $Code,
			[string] $Message,
			[string] $DiagnosticPath = $displayPath,
			[int] $SourceOrder = 0
		)

		$script:diagnosticSequence++
		$diagnostics.Add([pscustomobject] @{ Path = $DiagnosticPath; SourceOrder = $SourceOrder; Line = $Line; Code = $Code; Message = $Message; Sequence = $script:diagnosticSequence })
	}

	try
	{
		$text = [System.Text.UTF8Encoding]::new($false, $true).GetString($bytes)
	}
	catch [System.Text.DecoderFallbackException]
	{
		Add-Diagnostic 1 'ENCODING001' 'file is not valid UTF-8'
		$text = ''
	}

	if ($text.Length -gt 0 -and $text[0] -eq [char] 0xFEFF)
	{
		$text = $text.Substring(1)
	}
	$bareCarriageReturn = [regex]::Match($text, "`r(?!`n)")
	if ($bareCarriageReturn.Success)
	{
		$line = ([regex]::Matches($text.Substring(0, $bareCarriageReturn.Index), "`n")).Count + 1
		Add-Diagnostic $line 'ENCODING002' 'bare carriage return is not a supported line ending'
	}

	$normalized = $text.Replace("`r`n", "`n")
	$lines = $normalized.Split("`n")
	$fields = @{}
	$fieldLines = @{}
	$closingLineIndex = -1

	if ($lines.Count -eq 0 -or $lines[0] -cne '---')
	{
		Add-Diagnostic 1 'FRONTMATTER001' 'line 1 must be the exact opening delimiter ---'
	}
	else
	{
		$allowedKeys = @('name', 'description', 'when_to_use', 'allowed-tools', 'paths', 'argument-hint', 'disable-model-invocation', 'user-invocable', 'context', 'agent', 'model', 'effort', 'shell')
		$textKeys = @('name', 'description', 'when_to_use', 'argument-hint', 'agent', 'model')
		$listKeys = @('allowed-tools', 'paths')
		$booleanKeys = @('disable-model-invocation', 'user-invocable')
		for ($i = 1; $i -lt $lines.Count; $i++)
		{
			$lineNumber = $i + 1
			$lineText = $lines[$i]
			if ($lineText -ceq '---')
			{
				$closingLineIndex = $i
				break
			}
			if ($lineText.Length -eq 0)
			{
				Add-Diagnostic $lineNumber 'FRONTMATTER002' 'blank top-level frontmatter lines are not supported'
				continue
			}
			if ($lineText.Contains("`t"))
			{
				Add-Diagnostic $lineNumber 'FRONTMATTER003' 'tabs are not supported in frontmatter'
				continue
			}
			if ($lineText -notmatch '^([a-z][a-z0-9_-]*):(?:[ ](.*))?$')
			{
				Add-Diagnostic $lineNumber 'FRONTMATTER004' 'expected a top-level key: value entry'
				continue
			}

			$key = $Matches[1]
			$rawValue = if ($null -eq $Matches[2]) { '' } else { $Matches[2] }
			if ($allowedKeys -cnotcontains $key)
			{
				Add-Diagnostic $lineNumber 'SCHEMA001' "unknown or unsupported key: $key"
				continue
			}
			if ($fields.ContainsKey($key))
			{
				Add-Diagnostic $lineNumber 'SCHEMA002' "duplicate key: $key"
				continue
			}

			$fieldLines[$key] = $lineNumber
			try
			{
				if ($rawValue -ceq '>-')
				{
					if ($textKeys -cnotcontains $key -or $key -in @('name', 'argument-hint', 'agent', 'model'))
					{
						throw 'folded text is not accepted for this field'
					}
					$foldedLines = [System.Collections.Generic.List[string]]::new()
					while ($i + 1 -lt $lines.Count)
					{
						$next = $lines[$i + 1]
						if ($next -ceq '---' -or $next -match '^[a-z][a-z0-9_-]*:')
						{
							break
						}
						$i++
						if ($next.Length -gt 0 -and $next[0] -notin @(' ', "`t"))
						{
							Add-Diagnostic ($i + 1) 'FRONTMATTER005' 'folded text content must be indented'
							continue
						}
						$foldedLines.Add($next.TrimStart(' '))
					}
					$fields[$key] = ConvertFrom-FoldedScalar $foldedLines
				}
				elseif ($listKeys -ccontains $key)
				{
					$fields[$key] = ConvertFrom-FlowList $rawValue
				}
				elseif ($booleanKeys -ccontains $key)
				{
					if ($rawValue -cnotin @('true', 'false'))
					{
						throw 'value must be exact lowercase true or false'
					}
					$fields[$key] = $rawValue -ceq 'true'
				}
				else
				{
					$fields[$key] = ConvertFrom-TextScalar $rawValue -AllowBrackets:($key -ceq 'argument-hint')
				}
			}
			catch
			{
				Add-Diagnostic $lineNumber 'TYPE001' "$key $($_.Exception.Message)"
			}
		}
	}

	if ($lines.Count -gt 0 -and $lines[0] -ceq '---' -and $closingLineIndex -lt 0)
	{
		Add-Diagnostic $lines.Count 'FRONTMATTER006' 'closing frontmatter delimiter --- is missing'
	}

	foreach ($requiredKey in @('name', 'description'))
	{
		if (-not $fields.ContainsKey($requiredKey))
		{
			Add-Diagnostic 1 'SCHEMA003' "required key is missing or invalid: $requiredKey"
		}
	}

	if ($fields.ContainsKey('name'))
	{
		$name = [string] $fields['name']
		$nameLine = $fieldLines['name']
		if ($name -cnotmatch '^[a-z0-9]+(?:-[a-z0-9]+)*$')
		{
			Add-Diagnostic $nameLine 'NAME001' 'name must be kebab-case without edge or consecutive hyphens'
		}
		if ((Get-CharacterCount $name) -gt 64)
		{
			Add-Diagnostic $nameLine 'NAME002' 'name must be at most 64 characters'
		}
		if ($name -cne (Split-Path -Leaf $skillDirectory))
		{
			Add-Diagnostic $nameLine 'NAME003' 'name must exactly match the parent directory name'
		}
	}

	if ($fields.ContainsKey('description'))
	{
		$description = [string] $fields['description']
		$descriptionLine = $fieldLines['description']
		if ([string]::IsNullOrWhiteSpace($description))
		{
			Add-Diagnostic $descriptionLine 'DESCRIPTION001' 'description must not be empty'
		}
		if ((Get-CharacterCount $description) -gt 1024)
		{
			Add-Diagnostic $descriptionLine 'DESCRIPTION002' 'description must be at most 1024 characters'
		}
		$whenToUse = if ($fields.ContainsKey('when_to_use')) { [string] $fields['when_to_use'] } else { '' }
		if ((Get-CharacterCount ($description + $whenToUse)) -gt 1536)
		{
			Add-Diagnostic $descriptionLine 'DESCRIPTION003' 'description plus when_to_use must be at most 1536 characters'
		}
	}

	foreach ($key in @('when_to_use', 'argument-hint', 'agent', 'model'))
	{
		if ($fields.ContainsKey($key) -and [string]::IsNullOrWhiteSpace([string] $fields[$key]))
		{
			Add-Diagnostic $fieldLines[$key] 'VALUE001' "$key must not be empty"
		}
	}
	if ($fields.ContainsKey('paths'))
	{
		foreach ($item in [string[]] $fields['paths'])
		{
			if ([string]::IsNullOrWhiteSpace($item))
			{
				Add-Diagnostic $fieldLines['paths'] 'VALUE002' 'paths items must not be empty'
			}
		}
	}

	if ($fields.ContainsKey('context') -and $fields['context'] -cne 'fork')
	{
		Add-Diagnostic $fieldLines['context'] 'ENUM001' 'context must be fork'
	}
	if ($fields.ContainsKey('effort') -and $fields['effort'] -cnotin @('low', 'medium', 'high', 'xhigh', 'max'))
	{
		Add-Diagnostic $fieldLines['effort'] 'ENUM002' 'effort must be low, medium, high, xhigh, or max'
	}
	if ($fields.ContainsKey('shell') -and $fields['shell'] -cnotin @('bash', 'powershell'))
	{
		Add-Diagnostic $fieldLines['shell'] 'ENUM003' 'shell must be bash or powershell'
	}
	if ($fields.ContainsKey('context') -xor $fields.ContainsKey('agent'))
	{
		$relationshipLine = if ($fields.ContainsKey('context')) { $fieldLines['context'] } else { $fieldLines['agent'] }
		Add-Diagnostic $relationshipLine 'RELATIONSHIP001' 'context: fork and agent must be declared together'
	}

	if ($closingLineIndex -ge 0)
	{
		$bodyLines = if ($closingLineIndex + 1 -lt $lines.Count) { $lines[($closingLineIndex + 1)..($lines.Count - 1)] } else { @() }
		$body = $bodyLines -join "`n"
		if ([string]::IsNullOrWhiteSpace($body))
		{
			Add-Diagnostic ($closingLineIndex + 2) 'BODY001' 'Markdown body must not be empty'
		}
		else
		{
			foreach ($linkMatch in (Get-MarkdownDestinations $body))
			{
				$target = $linkMatch.Target
				$pathPart = ($target -split '[?#]', 2)[0]
				try
				{
					$pathPart = ConvertFrom-MarkdownDestination $pathPart
					$pathPart = [System.Uri]::UnescapeDataString($pathPart)
					$normalizedTarget = $pathPart.Replace('\', '/')
				}
				catch
				{
					$linkLine = $closingLineIndex + 2 + ([regex]::Matches($body.Substring(0, $linkMatch.Index), "`n")).Count
					Add-Diagnostic $linkLine 'LINK001' "bundled relative link is malformed: $target"
					continue
				}
				if ($normalizedTarget -match '^(references|scripts|assets)/')
				{
					$linkLine = $closingLineIndex + 2 + ([regex]::Matches($body.Substring(0, $linkMatch.Index), "`n")).Count
					try
					{
						$candidate = [System.IO.Path]::GetFullPath((Join-Path $skillDirectory $pathPart))
						$relativeCandidate = [System.IO.Path]::GetRelativePath($skillDirectory, $candidate)
						$invalidLink = (Test-LexicalParentEscape $relativeCandidate) -or -not [System.IO.File]::Exists($candidate) -or
							(Test-ReparseComponent $skillDirectory $candidate)
					}
					catch
					{
						$invalidLink = $true
					}
					if ($invalidLink)
					{
						Add-Diagnostic $linkLine 'LINK001' "bundled relative link does not resolve: $target"
					}
				}
			}
		}
	}

	$openAiSidecar = Join-Path $skillDirectory 'agents/openai.yaml'
	if ([System.IO.File]::Exists($openAiSidecar))
	{
		$openAiRelativeDisplay = [System.IO.Path]::GetRelativePath($repositoryRoot, $openAiSidecar)
		$openAiDisplayPath = if (Test-LexicalParentEscape $openAiRelativeDisplay) { $openAiSidecar.Replace('\', '/') } else { $openAiRelativeDisplay.Replace('\', '/') }
		if (Test-ReparseComponent $skillDirectory $openAiSidecar)
		{
			Add-Diagnostic 1 'OPENAI001' 'agents/openai.yaml path contains a reparse point' $openAiDisplayPath 1
		}
		else
		{
			Read-OpenAiSidecar $openAiSidecar $openAiDisplayPath ([string] $fields['name']) $skillDirectory
		}
	}

	if ($diagnostics.Count -gt 0)
	{
		$diagnostics | Sort-Object SourceOrder, Line, Sequence | ForEach-Object {
			Write-Output "INVALID $($_.Path):$($_.Line) $($_.Code): $($_.Message)"
		}
		exit 1
	}

	Write-Output "VALID $displayPath"
	exit 0
}
catch
{
	Write-SetupError 'INTERNAL' $_.Exception.Message
}
