[CmdletBinding()]
param(
	[Parameter(Mandatory = $true, Position = 0)]
	[ValidateNotNullOrEmpty()]
	[string[]] $Path,

	[ValidateRange(1, [int]::MaxValue)]
	[int] $StartLine,

	[ValidateRange(1, [int]::MaxValue)]
	[int] $EndLine,

	[switch] $Json
)

$ErrorActionPreference = 'Stop'

try
{
	$hasStartLine = $PSBoundParameters.ContainsKey('StartLine')
	$hasEndLine = $PSBoundParameters.ContainsKey('EndLine')
	if (($hasStartLine -or $hasEndLine) -and $Path.Count -ne 1)
	{
		throw '-StartLine and -EndLine may only be used with one path.'
	}

	if ($hasStartLine -and $hasEndLine -and $StartLine -gt $EndLine)
	{
		throw '-StartLine must be less than or equal to -EndLine.'
	}

	$strictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)
	$normalizedUtf8 = [System.Text.UTF8Encoding]::new($false)
	$results = foreach ($inputPath in $Path)
	{
		$item = Get-Item -LiteralPath $inputPath
		if ($item.PSIsContainer)
		{
			throw "Path is not a file: $inputPath"
		}

		$rawBytes = [System.IO.File]::ReadAllBytes($item.FullName)
		$offset = 0
		if ($rawBytes.Length -ge 3 -and $rawBytes[0] -eq 0xEF -and $rawBytes[1] -eq 0xBB -and $rawBytes[2] -eq 0xBF)
		{
			$offset = 3
		}

		$text = $strictUtf8.GetString($rawBytes, $offset, $rawBytes.Length - $offset)
		$text = $text.Replace("`r`n", "`n").Replace("`r", "`n")
		$selectedStartLine = $null
		$selectedEndLine = $null
		$lineCount = if ($text.Length -eq 0)
		{
			0
		}
		else
		{
			$splitCount = $text.Split("`n").Count
			$splitCount - [int]($text.EndsWith("`n", [System.StringComparison]::Ordinal))
		}

		if ($hasStartLine -or $hasEndLine)
		{
			[string[]] $lines = if ($lineCount -eq 0) { @() } else { $text.Split("`n")[0..($lineCount - 1)] }
			$first = if ($hasStartLine) { $StartLine } else { 1 }
			$last = if ($hasEndLine) { $EndLine } else { $lineCount }
			if ($first -gt $lineCount -or $last -gt $lineCount)
			{
				throw "Requested line range $first-$last exceeds $lineCount lines in $inputPath."
			}

			$text = [string]::Join("`n", $lines[($first - 1)..($last - 1)])
			$lineCount = $last - $first + 1
			$selectedStartLine = $first
			$selectedEndLine = $last
		}

		$byteCount = $normalizedUtf8.GetByteCount($text)
		$tokenCount = [int64](($byteCount + 3) -shr 2)
		[pscustomobject][ordered]@{
			Metric = 'bt-token-v1'
			Path = $item.FullName
			StartLine = $selectedStartLine
			EndLine = $selectedEndLine
			Lines = $lineCount
			Bytes = $byteCount
			Tokens = $tokenCount
		}
	}

	if ($Json)
	{
		if ($results.Count -eq 1)
		{
			$results[0] | ConvertTo-Json -Depth 3
		}
		else
		{
			[pscustomobject][ordered]@{
				Metric = 'bt-token-v1'
				Files = @($results)
				TotalLines = [int64](($results | Measure-Object -Property Lines -Sum).Sum)
				TotalBytes = [int64](($results | Measure-Object -Property Bytes -Sum).Sum)
				TotalTokens = [int64](($results | Measure-Object -Property Tokens -Sum).Sum)
			} | ConvertTo-Json -Depth 4
		}
	}
	else
	{
		$results
	}
}
catch
{
	Write-Error $_
	exit 1
}
