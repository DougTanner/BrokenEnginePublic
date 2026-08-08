# Produces an immutable identity receipt for one exact generated Data directory.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $DataRoot,
	[Parameter(Mandatory)][ValidateSet('Local', 'Shared')][string] $Mode,
	[Parameter(Mandatory)][string] $Baseline,
	[Parameter(Mandatory)][string] $ReceiptPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sharedScripts = Join-Path $PSScriptRoot '..\..\..\scripts'
if (-not (Test-Path -LiteralPath (Join-Path $sharedScripts 'AgentScriptCommon.psm1'))) {
	$sharedScripts = Join-Path $PSScriptRoot '..\..\..\..\.agents\scripts'
}
Import-Module (Join-Path $sharedScripts 'AgentScriptCommon.psm1') -Force
Import-Module (Join-Path $sharedScripts 'FinalizeWorkflowCommon.psm1') -Force

$script:ExpectedDataFiles = @(
	'Audio.h', 'Audio.manifest', 'Audio.pack',
	'Data.h', 'DataTypes.h',
	'Font.h', 'Font.manifest', 'Font.pack',
	'Islands.h', 'Islands.manifest', 'Islands.pack',
	'Model.h', 'Model.manifest', 'Model.pack',
	'Raw.h', 'Raw.manifest', 'Raw.pack',
	'Scene.h', 'Scene.manifest', 'Scene.pack',
	'Shader.h', 'Shader.manifest', 'Shader.pack',
	'Texture.h', 'Texture.manifest', 'Texture.pack'
)
[Array]::Sort($script:ExpectedDataFiles, [StringComparer]::Ordinal)
$script:AggregateFraming = 'broken-engine-data-oracle-aggregate-framing/v1'

function Get-LowerSha256([byte[]] $Bytes) {
	return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Get-OrdinaryDirectoryIdentity([string] $Path, [string] $Label) {
	if (-not [IO.Path]::IsPathFullyQualified($Path)) { throw "$Label must be an absolute path: '$Path'." }
	$canonical = Get-AgentCanonicalPath $Path
	$item = Get-Item -LiteralPath $canonical -Force -ErrorAction Stop
	if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		throw "$Label must be an ordinary non-reparse directory: '$canonical'."
	}
	$identity = Get-FinalizeExistingWindowsIdentity $canonical $Label
	if (-not $identity.Equals($canonical, [StringComparison]::OrdinalIgnoreCase)) {
		throw "$Label must not resolve through a reparse point: '$canonical' -> '$identity'."
	}
	return $canonical
}

function Assert-ExactInventory([string] $Root) {
	$items = @(Get-ChildItem -LiteralPath $Root -Force -ErrorAction Stop)
	if ($items.Count -ne $script:ExpectedDataFiles.Count) {
		throw "Data root must contain exactly $($script:ExpectedDataFiles.Count) entries; found $($items.Count)."
	}
	$actual = [Collections.Generic.Dictionary[string, IO.FileSystemInfo]]::new([StringComparer]::Ordinal)
	foreach ($item in $items) {
		if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
			throw "Data inventory entry must be an ordinary non-reparse file: '$($item.FullName)'."
		}
		if (-not $actual.TryAdd($item.Name, $item)) { throw "Duplicate Data inventory entry: '$($item.Name)'." }
	}
	foreach ($expected in $script:ExpectedDataFiles) {
		if (-not $actual.ContainsKey($expected)) { throw "Required Data inventory entry is missing: '$expected'." }
	}
	return $actual
}

function Get-DataEntries([string] $Root) {
	$inventory = Assert-ExactInventory $Root
	$openFiles = [Collections.Generic.List[IO.FileStream]]::new()
	try {
		foreach ($path in $script:ExpectedDataFiles) {
			$stream = [IO.File]::Open($inventory[$path].FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
			$openFiles.Add($stream)
		}
		# Existing entries cannot be replaced while their handles are open. Recheck
		# membership after all handles are held so add/remove races fail closed.
		[void](Assert-ExactInventory $Root)

		$entries = [Collections.Generic.List[object]]::new()
		for ($index = 0; $index -lt $script:ExpectedDataFiles.Count; ++$index) {
			$stream = $openFiles[$index]
			if ($stream.Length -le 0) { throw "Data inventory entry must not be empty: '$($script:ExpectedDataFiles[$index])'." }
			$stream.Position = 0
			$hasher = [Security.Cryptography.SHA256]::Create()
			try { $digest = $hasher.ComputeHash($stream) }
			finally { $hasher.Dispose() }
			$entries.Add([ordered]@{
				path = $script:ExpectedDataFiles[$index].Replace('\', '/')
				bytes = [int64]$stream.Length
				sha256 = [Convert]::ToHexString($digest).ToLowerInvariant()
			})
		}
		return @($entries)
	}
	finally {
		foreach ($stream in $openFiles) { $stream.Dispose() }
	}
}

function Get-BigEndianBytes([uint64] $Value, [int] $Width) {
	$bytes = if ($Width -eq 4) { [BitConverter]::GetBytes([uint32]$Value) } else { [BitConverter]::GetBytes($Value) }
	if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($bytes) }
	return $bytes
}

function Get-DataAggregate([object[]] $Entries) {
	$utf8 = [Text.UTF8Encoding]::new($false, $true)
	$hash = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
	try {
		# Framing: UTF-8 version tag, NUL, u32 entry count, then for each entry
		# u32 UTF-8 path length, path bytes, u64 byte size, and raw 32-byte SHA-256.
		$hash.AppendData($utf8.GetBytes($script:AggregateFraming))
		$hash.AppendData([byte[]]@(0))
		$hash.AppendData((Get-BigEndianBytes ([uint64]$Entries.Count) 4))
		foreach ($entry in $Entries) {
			$pathBytes = $utf8.GetBytes([string]$entry.path)
			$hash.AppendData((Get-BigEndianBytes ([uint64]$pathBytes.Length) 4))
			$hash.AppendData($pathBytes)
			$hash.AppendData((Get-BigEndianBytes ([uint64]$entry.bytes) 8))
			$hash.AppendData([Convert]::FromHexString([string]$entry.sha256))
		}
		return [Convert]::ToHexString($hash.GetHashAndReset()).ToLowerInvariant()
	}
	finally { $hash.Dispose() }
}

function Write-AtomicUtf8NoBom([string] $Path, [string] $Text) {
	if (-not [IO.Path]::IsPathFullyQualified($Path)) { throw "ReceiptPath must be an absolute path: '$Path'." }
	$canonical = Get-AgentCanonicalPath $Path
	$parent = Get-OrdinaryDirectoryIdentity ([IO.Path]::GetDirectoryName($canonical)) 'Receipt parent'
	if (-not ([IO.Path]::GetDirectoryName($canonical)).Equals($parent, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Receipt path escaped its canonical parent: '$canonical'."
	}
	$existing = Get-Item -LiteralPath $canonical -Force -ErrorAction SilentlyContinue
	if ($null -ne $existing) {
		if ($existing.PSIsContainer -or ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
			throw "Existing receipt must be an ordinary non-reparse file: '$canonical'."
		}
		$existingIdentity = Get-FinalizeExistingWindowsIdentity $canonical 'Existing receipt'
		if (-not $existingIdentity.Equals($canonical, [StringComparison]::OrdinalIgnoreCase)) {
			throw "Existing receipt must not resolve through a reparse point: '$canonical'."
		}
	}

	$temporary = Join-Path $parent ('.' + [IO.Path]::GetFileName($canonical) + '.' + [guid]::NewGuid().ToString('N') + '.tmp')
	$bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($Text)
	try {
		$stream = [IO.FileStream]::new($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None, 4096, [IO.FileOptions]::WriteThrough)
		try {
			$stream.Write($bytes, 0, $bytes.Length)
			$stream.Flush($true)
		}
		finally { $stream.Dispose() }
		if ($null -ne $existing) { [IO.File]::Replace($temporary, $canonical, [System.Management.Automation.Language.NullString]::Value, $true) }
		else { [IO.File]::Move($temporary, $canonical) }
	}
	finally {
		if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
	}
	return $canonical
}

$result = [ordered]@{
	schemaVersion = 'broken-engine-data-oracle-producer-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Data oracle receipt production did not run.'
	receiptPath = $null
	receiptSha256 = $null
	aggregateDigest = $null
}
$exitCode = 1
try {
	if ($Baseline -cnotmatch '^[0-9a-f]{40}$') { throw 'Baseline must be exactly 40 lowercase hexadecimal characters.' }
	$root = Get-OrdinaryDirectoryIdentity $DataRoot 'DataRoot'
	$entries = @(Get-DataEntries $root)
	$aggregate = Get-DataAggregate $entries
	$receipt = [ordered]@{
		schemaVersion = 'broken-engine-data-oracle/v1'
		dataRoot = $root
		mode = $Mode
		baseline = $Baseline
		createdUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffffffZ', [Globalization.CultureInfo]::InvariantCulture)
		aggregate = [ordered]@{ framing = $script:AggregateFraming; sha256 = $aggregate }
		entries = $entries
	}
	$json = $receipt | ConvertTo-Json -Depth 8 -Compress
	$writtenPath = Write-AtomicUtf8NoBom $ReceiptPath $json
	$receiptBytes = [IO.File]::ReadAllBytes($writtenPath)
	$result.status = 'pass'
	$result.code = 'ok'
	$result.message = 'Data oracle receipt produced.'
	$result.receiptPath = $writtenPath
	$result.receiptSha256 = Get-LowerSha256 $receiptBytes
	$result.aggregateDigest = $aggregate
	$exitCode = 0
}
catch {
	$result.message = $_.Exception.Message
}

[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 6 -Compress))
exit $exitCode
