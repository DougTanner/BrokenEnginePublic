# Collection-layout auditor for Broken Engine SOA collection headers.
#
# Reports regularity violations as structured data. It is an auditor only: it never writes,
# generates, or repairs a header, and it never proposes tuple text. Tuple position and wire
# order are judgment calls this script cannot see; it checks membership, partitioning,
# subsets, guard parity, and Frame::kiVersion sum completeness.
#
# Exit codes: 0 pass, 1 violations or input/internal error, 2 blocked (an accessor shape, tuple
# entry, guard form, or version sum the parser cannot resolve confidently). `status` distinguishes
# `pass` / `failed` / `error` / `blocked`.
#
# Recognized declaration form: `TYPE* __restrict pName` or the C-array-of-pointer column
# `TYPE* pName[COUNT]`, each one tuple entry. Recognized guards: `#if defined(BT_CLIENT)` and
# `#if defined(BT_SERVER)` with optional `#else`.
[CmdletBinding()]
param(
	# One or more collection headers or directories; `pwsh -File` cannot pass an array, so several
	# paths travel as one ';'-separated value.
	[string[]] $Path,
	# Substitute source for the Frame::kiVersion sum. Supplying it also narrows the version check's
	# collection universe to -Path, which is what lets fixtures run against a synthetic sum.
	[string] $FrameSource
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:DefaultPaths = @('Engine/Source/Frame/Collections', 'Projects/BrokenEngineSandbox/Source/Frame/Collections')
$script:DefaultFrameSource = 'Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp'
$script:MaximumViolations = 32
$script:MaximumTextLength = 96
$script:MaximumMessageLength = 256
$script:MaximumOutputBytes = 8192

$script:Violations = [Collections.Generic.List[object]]::new()
$script:Blocked = $false

function Add-LayoutViolation
{
	param([string] $Path, [int] $Line, [string] $Collection, [string] $Member, [string] $Rule, [string] $Text)

	$null = $script:Violations.Add([pscustomobject][ordered]@{
		Path = $Path
		Line = $Line
		Collection = $Collection
		Member = $Member
		Rule = $Rule
		Text = $Text
	})
}

function Add-BlockedViolation
{
	param([string] $Path, [int] $Line, [string] $Collection, [string] $Member, [string] $Rule, [string] $Text)

	$script:Blocked = $true
	Add-LayoutViolation $Path $Line $Collection $Member $Rule $Text
}

function Get-RepositoryRelativePath
{
	param([string] $FullPath)

	$prefix = $script:RepoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
	if ($FullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase))
	{
		return $FullPath.Substring($prefix.Length).Replace([IO.Path]::DirectorySeparatorChar, '/')
	}

	return $FullPath.Replace([IO.Path]::DirectorySeparatorChar, '/')
}

function Resolve-InputPath
{
	param([string] $InputPath)

	$full = if ([IO.Path]::IsPathFullyQualified($InputPath)) { $InputPath } else { Join-Path $script:RepoRoot $InputPath }
	return Get-AgentCanonicalPath $full
}

# The client configuration is the superset: a guard is effective when BT_CLIENT is defined
# and BT_SERVER is not.
function Test-ClientActiveGuard
{
	param([string] $Guard)

	if ([string]::IsNullOrEmpty($Guard)) { return $true }
	foreach ($token in $Guard.Split('&&', [StringSplitOptions]::RemoveEmptyEntries))
	{
		if ($token -ceq 'BT_SERVER' -or $token -ceq '!BT_CLIENT') { return $false }
	}

	return $true
}

function Test-ClientOnlyGuard
{
	param([string] $Guard)

	if ([string]::IsNullOrEmpty($Guard)) { return $false }
	foreach ($token in $Guard.Split('&&', [StringSplitOptions]::RemoveEmptyEntries))
	{
		if ($token -ceq 'BT_CLIENT' -or $token -ceq '!BT_SERVER') { return $true }
	}

	return $false
}

function Get-GuardText
{
	param([Collections.Generic.List[object]] $Stack)

	$parts = @()
	foreach ($entry in $Stack)
	{
		$parts += $(if ($entry.InElse) { "!$($entry.Condition)" } else { $entry.Condition })
	}

	return ($parts -join '&&')
}

function Get-ExcerptText
{
	param([string] $Value)

	$trimmed = $Value.Trim()
	if ($trimmed.Length -gt $script:MaximumTextLength) { return $trimmed.Substring(0, $script:MaximumTextLength) }
	return $trimmed
}

function Add-AccessorEntry
{
	param([object] $Accessor, [string] $Text, [int] $Line, [string] $Guard, [string] $Raw)

	foreach ($match in [regex]::Matches($Text, 'rSelf\.(\w+)'))
	{
		$null = $Accessor.RawEntries.Add([pscustomobject]@{
			Member = $match.Groups[1].Value
			Line = $Line
			Guard = $Guard
			Text = Get-ExcerptText $Raw
			Active = Test-ClientActiveGuard $Guard
		})
	}
}

# Splits a std::tie argument list on top-level commas. Commas inside parentheses or subscripts
# stay with their argument so a malformed entry reaches the exact-form check intact.
function Split-TieArguments
{
	param([string] $Arguments)

	$result = [Collections.Generic.List[string]]::new()
	if ([string]::IsNullOrWhiteSpace($Arguments)) { return ,$result }

	$depth = 0
	$start = 0
	for ($index = 0; $index -lt $Arguments.Length; $index++)
	{
		$character = $Arguments[$index]
		if ($character -ceq '(' -or $character -ceq '[') { $depth++ }
		elseif ($character -ceq ')' -or $character -ceq ']') { $depth-- }
		elseif ($character -ceq ',' -and $depth -eq 0)
		{
			$null = $result.Add($Arguments.Substring($start, $index - $start).Trim())
			$start = $index + 1
		}
	}

	$null = $result.Add($Arguments.Substring($start).Trim())
	return ,$result
}

function Get-NetBraceDelta
{
	param([string] $Text)

	$delta = 0
	foreach ($character in $Text.ToCharArray())
	{
		if ($character -ceq '{') { $delta++ }
		elseif ($character -ceq '}') { $delta-- }
	}

	return $delta
}

# Parses one header into collection-struct records: declared SOA columns with their guards,
# accessor bodies with their entries, and kiVersion presence.
function Read-CollectionHeader
{
	param([string] $FullPath)

	$displayPath = Get-RepositoryRelativePath $FullPath
	$lines = [IO.File]::ReadAllLines($FullPath)
	$structs = [Collections.Generic.List[object]]::new()
	$guardStack = [Collections.Generic.List[object]]::new()
	$depth = 0
	$current = $null
	$structDepth = 0
	$structOpened = $false
	$accessor = $null
	$accessorDepth = 0
	$accessorOpened = $false
	$accessorText = ''

	for ($index = 0; $index -lt $lines.Count; $index++)
	{
		$lineNumber = $index + 1
		$raw = $lines[$index]
		$commentStart = $raw.IndexOf('//', [StringComparison]::Ordinal)
		$text = $(if ($commentStart -ge 0) { $raw.Substring(0, $commentStart) } else { $raw })
		$trimmed = $text.Trim()

		if ($trimmed.StartsWith('#', [StringComparison]::Ordinal))
		{
			if ($trimmed -cmatch '^#\s*if\s+defined\(\s*(BT_CLIENT|BT_SERVER)\s*\)\s*$')
			{
				$null = $guardStack.Add([pscustomobject]@{ Condition = $Matches[1]; InElse = $false })
			}
			elseif ($trimmed -cmatch '^#\s*else\b')
			{
				if ($guardStack.Count -eq 0)
				{
					Add-BlockedViolation $displayPath $lineNumber '' '' 'parse.unrecognized-guard' (Get-ExcerptText $raw)
				}
				else
				{
					$guardStack[$guardStack.Count - 1].InElse = $true
				}
			}
			elseif ($trimmed -cmatch '^#\s*endif\b')
			{
				if ($guardStack.Count -eq 0)
				{
					Add-BlockedViolation $displayPath $lineNumber '' '' 'parse.unrecognized-guard' (Get-ExcerptText $raw)
				}
				else
				{
					$guardStack.RemoveAt($guardStack.Count - 1)
				}
			}
			elseif ($trimmed -cmatch '^#\s*(if|ifdef|ifndef|elif)\b')
			{
				Add-BlockedViolation $displayPath $lineNumber '' '' 'parse.unrecognized-guard' (Get-ExcerptText $raw)
			}

			continue
		}

		$guard = Get-GuardText $guardStack
		$depthBefore = $depth
		$depth += Get-NetBraceDelta $text

		if ($null -ne $accessor)
		{
			if (Test-ClientActiveGuard $guard) { $accessorText += " $trimmed" }
			Add-AccessorEntry $accessor $trimmed $lineNumber $guard $raw
			if ($text.Contains('{')) { $accessorOpened = $true }
			if ($accessorOpened -and $depth -le $accessorDepth)
			{
				$accessor.Body = $accessorText
				$null = $current.Accessors.Add($accessor)
				$accessor = $null
			}

			continue
		}

		if ($null -eq $current)
		{
			if ($trimmed -cmatch '^struct\s+(\w+)\s*:\s*public\s+(?:engine::)?Collection\s*<')
			{
				$current = [pscustomobject]@{
					Name = $Matches[1]
					Path = $displayPath
					Line = $lineNumber
					HasVersion = $false
					Declarations = [Collections.Generic.List[object]]::new()
					Accessors = [Collections.Generic.List[object]]::new()
				}
				$structDepth = $depthBefore
				$structOpened = $false
			}

			continue
		}

		if ($depth -gt $structDepth) { $structOpened = $true }

		if ($structOpened -and $depthBefore -eq ($structDepth + 1))
		{
			if ($trimmed -cmatch '^static\s+constexpr\s+int64_t\s+kiVersion\b')
			{
				$current.HasVersion = $true
			}
			elseif ($trimmed -cmatch '^[A-Za-z_][\w:]*\s*\*\s*__restrict\s+(\w+)\s*[=;]' -or $trimmed -cmatch '^[A-Za-z_][\w:]*\s*\*\s+(\w+)\s*\[[^\]]+\]\s*[=;]')
			{
				$null = $current.Declarations.Add([pscustomobject]@{
					Member = $Matches[1]
					Line = $lineNumber
					Guard = $guard
					Text = Get-ExcerptText $raw
				})
			}
			elseif ($trimmed -cmatch '^auto\s+(Members|SharedMembers|ClientMembers|SharedCrcMembers|PersistentMembers)\s*\(')
			{
				$accessor = [pscustomobject]@{
					Name = $Matches[1]
					Line = $lineNumber
					Guard = $guard
					Text = Get-ExcerptText $raw
					Body = ''
					RawEntries = [Collections.Generic.List[object]]::new()
					Shape = ''
					Entries = @()
				}
				$accessorDepth = $depthBefore
				$accessorOpened = $text.Contains('{')
				$accessorText = $trimmed
				Add-AccessorEntry $accessor $trimmed $lineNumber $guard $raw
				if ($accessorOpened -and $depth -le $accessorDepth)
				{
					$accessor.Body = $accessorText
					$null = $current.Accessors.Add($accessor)
					$accessor = $null
				}
			}
		}

		if ($structOpened -and $depth -le $structDepth)
		{
			$null = $structs.Add($current)
			$current = $null
		}
	}

	if ($null -ne $current)
	{
		Add-BlockedViolation $displayPath $current.Line $current.Name '' 'parse.unterminated-struct' (Get-ExcerptText "Collection struct $($current.Name) did not close before end of file")
	}

	return $structs
}

# Resolves each accessor body to one of the three recognized shapes; anything else blocks.
function Resolve-AccessorShapes
{
	param([object] $Struct)

	foreach ($accessor in $Struct.Accessors)
	{
		$open = $accessor.Body.IndexOf('{', [StringComparison]::Ordinal)
		$close = $accessor.Body.LastIndexOf('}', [StringComparison]::Ordinal)
		if ($open -lt 0 -or $close -le $open)
		{
			Add-BlockedViolation $Struct.Path $accessor.Line $Struct.Name $accessor.Name 'parse.unrecognized-shape' $accessor.Text
			continue
		}

		$body = $accessor.Body.Substring($open + 1, $close - $open - 1)
		$body = ([regex]::Replace($body, '\s+', ' ')).Trim()
		if ($body -cmatch '^return std::tie\(\s*(.*?)\s*\)\s*;$')
		{
			# Every argument must name a whole column. An expression such as `rSelf.pColumn[0]` passes a
			# single element, not the column, so it is not an entry this auditor can reason about.
			$invalid = @(Split-TieArguments $Matches[1] | Where-Object { $_ -cnotmatch '^rSelf\.\w+$' })
			if ($invalid.Count -gt 0)
			{
				Add-BlockedViolation $Struct.Path $accessor.Line $Struct.Name $accessor.Name 'parse.unrecognized-entry' (Get-ExcerptText $invalid[0])
				continue
			}

			$accessor.Shape = 'tie'
			$accessor.Entries = @($accessor.RawEntries | Where-Object { $_.Active })
		}
		elseif ($body -ceq 'return std::tuple_cat(rSelf.SharedMembers(), rSelf.ClientMembers());')
		{
			$accessor.Shape = 'tuple-cat'
		}
		elseif ($body -ceq 'return rSelf.SharedMembers();')
		{
			$accessor.Shape = 'shared-forward'
		}
		else
		{
			Add-BlockedViolation $Struct.Path $accessor.Line $Struct.Name $accessor.Name 'parse.unrecognized-shape' $accessor.Text
		}
	}
}

function Get-Accessor
{
	param([object] $Struct, [string] $Name)

	foreach ($accessor in $Struct.Accessors)
	{
		if ($accessor.Name -ceq $Name) { return $accessor }
	}

	return $null
}

# The effective Members() list under the client configuration, in tuple order.
function Get-EffectiveMembers
{
	param([object] $Struct)

	$members = Get-Accessor $Struct 'Members'
	if ($null -eq $members)
	{
		Add-BlockedViolation $Struct.Path $Struct.Line $Struct.Name 'Members' 'parse.members-absent' (Get-ExcerptText "Collection struct $($Struct.Name) declares no Members() accessor")
		return $null
	}

	# Every array return uses the comma operator: an unrolled empty tuple would otherwise arrive as
	# $null and silently skip the struct's checks.
	switch ($members.Shape)
	{
		'tie' { return ,@($members.Entries) }
		'shared-forward'
		{
			$shared = Get-Accessor $Struct 'SharedMembers'
			if ($null -eq $shared -or $shared.Shape -cne 'tie')
			{
				Add-BlockedViolation $Struct.Path $members.Line $Struct.Name 'Members' 'parse.unrecognized-shape' $members.Text
				return $null
			}
			return ,@($shared.Entries)
		}
		'tuple-cat'
		{
			$shared = Get-Accessor $Struct 'SharedMembers'
			$client = Get-Accessor $Struct 'ClientMembers'
			if ($null -eq $shared -or $shared.Shape -cne 'tie' -or $null -eq $client -or $client.Shape -cne 'tie')
			{
				Add-BlockedViolation $Struct.Path $members.Line $Struct.Name 'Members' 'parse.unrecognized-shape' $members.Text
				return $null
			}
			$combined = @($shared.Entries) + @($client.Entries)
			return ,$combined
		}
	}

	return $null
}

function Test-MembersExactlyOnce
{
	param([object] $Struct, [object[]] $Effective)

	$names = @($Effective | ForEach-Object { $_.Member })
	foreach ($declaration in $Struct.Declarations)
	{
		if (-not (Test-ClientActiveGuard $declaration.Guard)) { continue }
		if ($names -cnotcontains $declaration.Member)
		{
			Add-LayoutViolation $Struct.Path $declaration.Line $Struct.Name $declaration.Member 'members.missing' $declaration.Text
		}
	}

	# The reverse direction: an entry left behind after its column was removed names nothing.
	$declaredNames = @($Struct.Declarations | ForEach-Object { $_.Member })
	$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($entry in $Effective)
	{
		if ($declaredNames -cnotcontains $entry.Member)
		{
			Add-LayoutViolation $Struct.Path $entry.Line $Struct.Name $entry.Member 'members.unresolved' $entry.Text
		}

		if (-not $seen.Add($entry.Member))
		{
			Add-LayoutViolation $Struct.Path $entry.Line $Struct.Name $entry.Member 'members.duplicate' $entry.Text
		}
	}
}

function Test-AccessorPartition
{
	param([object] $Struct, [object[]] $Effective)

	$shared = Get-Accessor $Struct 'SharedMembers'
	if ($null -eq $shared -or $shared.Shape -cne 'tie') { return }

	$client = Get-Accessor $Struct 'ClientMembers'
	$sharedNames = @($shared.Entries | ForEach-Object { $_.Member })
	$clientEntries = @()
	if ($null -ne $client -and $client.Shape -ceq 'tie') { $clientEntries = @($client.Entries) }

	foreach ($entry in $clientEntries)
	{
		if ($sharedNames -ccontains $entry.Member)
		{
			Add-LayoutViolation $Struct.Path $entry.Line $Struct.Name $entry.Member 'partition.overlap' $entry.Text
		}
	}

	# Multiset equality, not containment: a column the split accessors list twice while the effective
	# tuple lists it once is still a partition mismatch. One violation per distinct column.
	$unionEntries = @($shared.Entries) + $clientEntries
	$reported = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($entry in (@($Effective) + $unionEntries))
	{
		if (-not $reported.Add($entry.Member)) { continue }
		$unionCount = @($unionEntries | Where-Object { $_.Member -ceq $entry.Member }).Count
		$effectiveCount = @($Effective | Where-Object { $_.Member -ceq $entry.Member }).Count
		if ($unionCount -ne $effectiveCount)
		{
			Add-LayoutViolation $Struct.Path $entry.Line $Struct.Name $entry.Member 'partition.mismatch' $entry.Text
		}
	}
}

# SharedCrcMembers entries are expected to appear in SharedMembers as well, so membership in
# both lists is never a duplicate; only an entry absent from the shared set is a violation.
function Test-SharedCrcSubset
{
	param([object] $Struct, [object[]] $Effective)

	$crc = Get-Accessor $Struct 'SharedCrcMembers'
	if ($null -eq $crc -or $crc.Shape -cne 'tie') { return }

	$shared = Get-Accessor $Struct 'SharedMembers'
	$baseNames = $(if ($null -ne $shared -and $shared.Shape -ceq 'tie') { @($shared.Entries | ForEach-Object { $_.Member }) } else { @($Effective | ForEach-Object { $_.Member }) })
	foreach ($entry in $crc.Entries)
	{
		if ($baseNames -cnotcontains $entry.Member)
		{
			Add-LayoutViolation $Struct.Path $entry.Line $Struct.Name $entry.Member 'sharedcrc.not-subset' $entry.Text
		}
	}
}

function Test-PersistentSubset
{
	param([object] $Struct, [object[]] $Effective)

	$persistent = Get-Accessor $Struct 'PersistentMembers'
	if ($null -eq $persistent -or $persistent.Shape -cne 'tie') { return }

	$effectiveNames = @($Effective | ForEach-Object { $_.Member })
	foreach ($entry in $persistent.Entries)
	{
		if ($effectiveNames -cnotcontains $entry.Member)
		{
			Add-LayoutViolation $Struct.Path $entry.Line $Struct.Name $entry.Member 'persistent.not-subset' $entry.Text
		}
	}
}

function Test-SharedClientGuard
{
	param([object] $Struct)

	$shared = Get-Accessor $Struct 'SharedMembers'
	if ($null -eq $shared -or $shared.Shape -cne 'tie') { return }

	foreach ($entry in $shared.Entries)
	{
		foreach ($declaration in $Struct.Declarations)
		{
			if ($declaration.Member -cne $entry.Member) { continue }
			if (Test-ClientOnlyGuard $declaration.Guard)
			{
				Add-LayoutViolation $Struct.Path $entry.Line $Struct.Name $entry.Member 'shared.client-guarded' $entry.Text
			}
		}
	}
}

function Test-GuardParity
{
	param([object] $Struct)

	foreach ($accessor in $Struct.Accessors)
	{
		if ($accessor.Shape -cne 'tie') { continue }
		foreach ($entry in $accessor.Entries)
		{
			foreach ($declaration in $Struct.Declarations)
			{
				if ($declaration.Member -cne $entry.Member) { continue }
				if ($declaration.Guard -cne $entry.Guard)
				{
					Add-LayoutViolation $Struct.Path $entry.Line $Struct.Name $entry.Member 'guard.parity-mismatch' $entry.Text
				}
			}
		}
	}
}

# Both directions of the Frame::kiVersion sum. The literal base term and engine::kiNavDataVersion
# carry no `::kiVersion` suffix, so they are exempt by construction.
function Test-VersionSum
{
	param([string] $FrameSourcePath, [object[]] $VersionStructs)

	$displayPath = Get-RepositoryRelativePath $FrameSourcePath
	$lines = [IO.File]::ReadAllLines($FrameSourcePath)
	$sumLine = 0
	$sumText = ''
	for ($index = 0; $index -lt $lines.Count; $index++)
	{
		if ($lines[$index] -cmatch 'Frame::kiVersion\s*=\s*(.*?);')
		{
			$sumLine = $index + 1
			$sumText = $Matches[1]
			break
		}
	}

	if ($sumLine -eq 0)
	{
		Add-BlockedViolation $displayPath 0 '' '' 'version.sum-unparsed' (Get-ExcerptText "No Frame::kiVersion assignment found in $displayPath")
		return
	}

	$termNames = @([regex]::Matches($sumText, '([A-Za-z_]\w*)::kiVersion') | ForEach-Object { $_.Groups[1].Value })
	$declaringNames = @($VersionStructs | Where-Object { $_.HasVersion } | ForEach-Object { $_.Name })

	foreach ($struct in $VersionStructs)
	{
		if (-not $struct.HasVersion) { continue }
		if ($termNames -cnotcontains $struct.Name)
		{
			Add-LayoutViolation $struct.Path $struct.Line $struct.Name 'kiVersion' 'version.term-missing' (Get-ExcerptText "$($struct.Name)::kiVersion is absent from the Frame::kiVersion sum")
		}
	}

	foreach ($termName in $termNames)
	{
		if ($declaringNames -cnotcontains $termName)
		{
			Add-LayoutViolation $displayPath $sumLine $termName 'kiVersion' 'version.term-unresolved' (Get-ExcerptText "$($termName)::kiVersion has no live declaration")
		}
	}
}

function New-TerminalResult
{
	param([string] $Status, [string] $Code, [string] $Message)

	$comparison = [Comparison[object]] {
		param($left, $right)
		$result = [StringComparer]::Ordinal.Compare([string]$left.Path, [string]$right.Path)
		if ($result -ne 0) { return $result }
		$result = [int]$left.Line - [int]$right.Line
		if ($result -ne 0) { return $result }
		$result = [StringComparer]::Ordinal.Compare([string]$left.Rule, [string]$right.Rule)
		if ($result -ne 0) { return $result }
		return [StringComparer]::Ordinal.Compare([string]$left.Member, [string]$right.Member)
	}
	$script:Violations.Sort($comparison)

	$items = [Collections.Generic.List[object]]::new()
	foreach ($violation in $script:Violations | Select-Object -First $script:MaximumViolations)
	{
		$null = $items.Add([pscustomobject][ordered]@{
			path = $violation.Path
			line = $violation.Line
			collection = $violation.Collection
			member = $violation.Member
			rule = $violation.Rule
			text = $violation.Text
		})
	}

	if ($Message.Length -gt $script:MaximumMessageLength) { $Message = $Message.Substring(0, $script:MaximumMessageLength) }

	while ($true)
	{
		$result = [ordered]@{
			schemaVersion = 'broken-engine-collection-layout/v1'
			status = $Status
			code = $Code
			message = $Message
			violations = [ordered]@{
				items = @($items)
				totalCount = $script:Violations.Count
				omittedCount = $script:Violations.Count - $items.Count
				truncated = $items.Count -lt $script:Violations.Count
			}
		}
		$json = $result | ConvertTo-Json -Compress -Depth 8
		if ([Text.Encoding]::UTF8.GetByteCount($json) -le $script:MaximumOutputBytes) { return $json }
		if ($items.Count -gt 0) { $items.RemoveAt($items.Count - 1); continue }
		if ($Message.Length -gt 0) { $Message = ''; continue }
		return $json
	}
}

[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$status = 'error'
$code = 'internal.error'
$message = 'The collection-layout auditor did not run.'
$exitCode = 1

try
{
	# Imported inside the guarded body so even an import failure still emits the failing default.
	Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1') -Force
	$script:RepoRoot = Get-AgentCanonicalPath (Join-Path $PSScriptRoot '..\..')

	# `pwsh -File` cannot pass an array, so a single ';'-separated value carries several paths.
	$layoutInputs = @($(if ($null -eq $Path -or $Path.Count -eq 0) { $script:DefaultPaths } else { @($Path | ForEach-Object { $_.Split(';', [StringSplitOptions]::RemoveEmptyEntries) }) }))
	$layoutFiles = [Collections.Generic.List[string]]::new()
	# A -Path that flattens to nothing would otherwise audit nothing and still report a pass.
	$inputError = $(if ($layoutInputs.Count -eq 0) { 'No collection header or directory path was supplied.' } else { '' })
	foreach ($inputPath in $layoutInputs)
	{
		$full = Resolve-InputPath $inputPath
		if ([IO.Directory]::Exists($full))
		{
			foreach ($file in [IO.Directory]::GetFiles($full, '*.h', [IO.SearchOption]::AllDirectories)) { $null = $layoutFiles.Add($file) }
		}
		elseif ([IO.File]::Exists($full))
		{
			$null = $layoutFiles.Add($full)
		}
		else
		{
			$inputError = "Path does not exist: $inputPath"
			break
		}
	}

	if ([string]::IsNullOrEmpty($inputError))
	{
		# A substitute sum source implies a substitute collection universe, so the version check
		# then uses -Path; otherwise it always sweeps the full default collection directories, so a
		# narrowed -Path cannot fabricate a stale-term violation.
		$substituteFrameSource = -not [string]::IsNullOrWhiteSpace($FrameSource)
		$frameSourcePath = Resolve-InputPath $(if ($substituteFrameSource) { $FrameSource } else { $script:DefaultFrameSource })
		if (-not [IO.File]::Exists($frameSourcePath))
		{
			$inputError = "Frame source does not exist: $frameSourcePath"
		}
		else
		{
			$versionFiles = [Collections.Generic.List[string]]::new()
			if ($substituteFrameSource)
			{
				foreach ($file in $layoutFiles) { $null = $versionFiles.Add($file) }
			}
			else
			{
				foreach ($defaultPath in $script:DefaultPaths)
				{
					foreach ($file in [IO.Directory]::GetFiles((Resolve-InputPath $defaultPath), '*.h', [IO.SearchOption]::AllDirectories)) { $null = $versionFiles.Add($file) }
				}
			}

			$parsed = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
			foreach ($file in (@($layoutFiles) + @($versionFiles)))
			{
				if ($parsed.ContainsKey($file)) { continue }
				$parsed.Add($file, (Read-CollectionHeader $file))
			}

			foreach ($file in $layoutFiles)
			{
				foreach ($struct in $parsed[$file])
				{
					Resolve-AccessorShapes $struct
					$effective = Get-EffectiveMembers $struct
					if ($null -eq $effective) { continue }

					Test-MembersExactlyOnce $struct $effective
					Test-AccessorPartition $struct $effective
					Test-SharedCrcSubset $struct $effective
					Test-PersistentSubset $struct $effective
					Test-SharedClientGuard $struct
					Test-GuardParity $struct
				}
			}

			$versionStructs = [Collections.Generic.List[object]]::new()
			foreach ($file in $versionFiles)
			{
				foreach ($struct in $parsed[$file]) { $null = $versionStructs.Add($struct) }
			}
			Test-VersionSum $frameSourcePath $versionStructs
		}
	}

	if (-not [string]::IsNullOrEmpty($inputError))
	{
		$status = 'error'
		$code = 'input.path-invalid'
		$message = $inputError
		$exitCode = 1
	}
	elseif ($script:Blocked)
	{
		$status = 'blocked'
		$code = 'parse.blocked'
		$message = 'One or more collection headers could not be parsed confidently; see the parse.* violations.'
		$exitCode = 2
	}
	elseif ($script:Violations.Count -gt 0)
	{
		$status = 'failed'
		$code = 'layout.violations'
		$message = "Collection layout audit found $($script:Violations.Count) violation(s)."
		$exitCode = 1
	}
	else
	{
		$status = 'pass'
		$code = 'ok'
		$message = 'Collection layout audit found no violations.'
		$exitCode = 0
	}
}
catch
{
	$script:Violations.Clear()
	$status = 'error'
	$code = 'internal.error'
	$message = 'The collection-layout auditor encountered an internal error.'
	$exitCode = 1
	[Console]::Error.WriteLine($_.Exception.ToString())
}

[Console]::Out.Write((New-TerminalResult $status $code $message))
exit $exitCode
