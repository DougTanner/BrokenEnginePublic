# Verifies a Data oracle receipt and the exact Data directory it identifies.
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string] $ReceiptPath,
	[Parameter(Mandatory)][string] $ReceiptSha256,
	[Parameter(Mandatory)][string] $ExpectedDataRoot,
	[Parameter(Mandatory)][ValidateSet('Local', 'Shared')][string] $ExpectedMode,
	[Parameter(Mandatory)][string] $ExpectedBaseline
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

function Get-ReceiptBytes([string] $Path) {
	if (-not [IO.Path]::IsPathFullyQualified($Path)) { throw "ReceiptPath must be an absolute path: '$Path'." }
	$canonical = Get-AgentCanonicalPath $Path
	$parent = Get-OrdinaryDirectoryIdentity ([IO.Path]::GetDirectoryName($canonical)) 'Receipt parent'
	if (-not ([IO.Path]::GetDirectoryName($canonical)).Equals($parent, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Receipt path escaped its canonical parent: '$canonical'."
	}
	$item = Get-Item -LiteralPath $canonical -Force -ErrorAction Stop
	if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		throw "Receipt must be an ordinary non-reparse file: '$canonical'."
	}
	$identity = Get-FinalizeExistingWindowsIdentity $canonical 'Receipt'
	if (-not $identity.Equals($canonical, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Receipt must not resolve through a reparse point: '$canonical' -> '$identity'."
	}
	return [IO.File]::ReadAllBytes($canonical)
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

function Assert-ObjectShape([Text.Json.JsonElement] $Element, [string[]] $Expected, [string] $Label) {
	if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::Object) { throw "$Label must be a JSON object." }
	$properties = @($Element.EnumerateObject())
	if ($properties.Count -ne $Expected.Count) { throw "$Label has an invalid property count." }
	$names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($property in $properties) {
		if (-not $names.Add($property.Name)) { throw "$Label contains duplicate property '$($property.Name)'." }
	}
	foreach ($name in $Expected) {
		if (-not $names.Contains($name)) { throw "$Label is missing property '$name'." }
	}
}

function Get-RequiredString([Text.Json.JsonElement] $Element, [string] $Name, [string] $Label) {
	$value = $Element.GetProperty($Name)
	if ($value.ValueKind -ne [Text.Json.JsonValueKind]::String) { throw "$Label.$Name must be a JSON string." }
	return $value.GetString()
}

$result = [ordered]@{
	schemaVersion = 'broken-engine-data-oracle-verifier-result/v1'
	status = 'error'
	code = 'validation.failed'
	message = 'Data oracle verification did not run.'
	receiptPath = $null
	receiptSha256 = $null
	aggregateDigest = $null
}
$exitCode = 1
$document = $null
try {
	if ($ReceiptSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'ReceiptSha256 must be exactly 64 lowercase hexadecimal characters.' }
	if ($ExpectedBaseline -cnotmatch '^[0-9a-f]{40}$') { throw 'ExpectedBaseline must be exactly 40 lowercase hexadecimal characters.' }
	$expectedRoot = Get-OrdinaryDirectoryIdentity $ExpectedDataRoot 'ExpectedDataRoot'
	$canonicalReceipt = Get-AgentCanonicalPath $ReceiptPath
	$receiptBytes = Get-ReceiptBytes $ReceiptPath
	$actualReceiptSha256 = Get-LowerSha256 $receiptBytes
	if ($actualReceiptSha256 -cne $ReceiptSha256) {
		throw "Receipt SHA-256 mismatch: expected $ReceiptSha256, actual $actualReceiptSha256."
	}
	$utf8 = [Text.UTF8Encoding]::new($false, $true)
	try { $receiptText = $utf8.GetString($receiptBytes) }
	catch [Text.DecoderFallbackException] { throw 'Receipt is not strict UTF-8.' }
	$document = [Text.Json.JsonDocument]::Parse($receiptText)
	$rootElement = $document.RootElement
	Assert-ObjectShape $rootElement @('schemaVersion', 'dataRoot', 'mode', 'baseline', 'createdUtc', 'aggregate', 'entries') 'Receipt'

	$schemaVersion = Get-RequiredString $rootElement 'schemaVersion' 'Receipt'
	$receiptRoot = Get-RequiredString $rootElement 'dataRoot' 'Receipt'
	$receiptMode = Get-RequiredString $rootElement 'mode' 'Receipt'
	$receiptBaseline = Get-RequiredString $rootElement 'baseline' 'Receipt'
	$createdUtc = Get-RequiredString $rootElement 'createdUtc' 'Receipt'
	if ($schemaVersion -cne 'broken-engine-data-oracle/v1') { throw "Unsupported receipt schemaVersion '$schemaVersion'." }
	if ($receiptMode -cnotin @('Local', 'Shared')) { throw "Receipt mode is invalid: '$receiptMode'." }
	if ($receiptBaseline -cnotmatch '^[0-9a-f]{40}$') { throw 'Receipt baseline is not a lowercase 40-hex commit.' }
	if ($createdUtc -cnotmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{7}Z$') { throw 'Receipt createdUtc is not canonical UTC.' }
	[void][DateTime]::ParseExact($createdUtc, 'yyyy-MM-ddTHH:mm:ss.fffffffZ', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AssumeUniversal)
	if (-not [IO.Path]::IsPathFullyQualified($receiptRoot) -or (Get-AgentCanonicalPath $receiptRoot) -cne $receiptRoot) {
		throw "Receipt dataRoot is not canonical: '$receiptRoot'."
	}
	$canonicalRecordedRoot = Get-OrdinaryDirectoryIdentity $receiptRoot 'Receipt dataRoot'
	if (-not $canonicalRecordedRoot.Equals($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Receipt dataRoot does not match ExpectedDataRoot.' }
	if ($receiptMode -cne $ExpectedMode) { throw "Receipt mode '$receiptMode' does not match expected mode '$ExpectedMode'." }
	if ($receiptBaseline -cne $ExpectedBaseline) { throw 'Receipt baseline does not match ExpectedBaseline.' }

	$aggregateElement = $rootElement.GetProperty('aggregate')
	Assert-ObjectShape $aggregateElement @('framing', 'sha256') 'Receipt.aggregate'
	$framing = Get-RequiredString $aggregateElement 'framing' 'Receipt.aggregate'
	$recordedAggregate = Get-RequiredString $aggregateElement 'sha256' 'Receipt.aggregate'
	if ($framing -cne $script:AggregateFraming) { throw "Unsupported aggregate framing '$framing'." }
	if ($recordedAggregate -cnotmatch '^[0-9a-f]{64}$') { throw 'Receipt aggregate SHA-256 is invalid.' }

	$entryArray = $rootElement.GetProperty('entries')
	if ($entryArray.ValueKind -ne [Text.Json.JsonValueKind]::Array) { throw 'Receipt.entries must be a JSON array.' }
	if ($entryArray.GetArrayLength() -ne $script:ExpectedDataFiles.Count) { throw 'Receipt.entries does not contain exactly 26 entries.' }
	$recordedEntries = [Collections.Generic.List[object]]::new()
	$index = 0
	foreach ($entryElement in $entryArray.EnumerateArray()) {
		Assert-ObjectShape $entryElement @('path', 'bytes', 'sha256') "Receipt.entries[$index]"
		$path = Get-RequiredString $entryElement 'path' "Receipt.entries[$index]"
		$sha256 = Get-RequiredString $entryElement 'sha256' "Receipt.entries[$index]"
		$bytesElement = $entryElement.GetProperty('bytes')
		if ($bytesElement.ValueKind -ne [Text.Json.JsonValueKind]::Number) { throw "Receipt.entries[$index].bytes must be a JSON number." }
		$bytes = $bytesElement.GetInt64()
		if ($bytes -le 0) { throw "Receipt.entries[$index].bytes must be positive." }
		if ($path -cne $script:ExpectedDataFiles[$index]) { throw "Receipt entry path/order mismatch at index ${index}: '$path'." }
		if ($sha256 -cnotmatch '^[0-9a-f]{64}$') { throw "Receipt.entries[$index].sha256 is invalid." }
		$recordedEntries.Add([ordered]@{ path = $path; bytes = $bytes; sha256 = $sha256 })
		++$index
	}

	$actualEntries = @(Get-DataEntries $expectedRoot)
	for ($entryIndex = 0; $entryIndex -lt $actualEntries.Count; ++$entryIndex) {
		$recorded = $recordedEntries[$entryIndex]
		$actual = $actualEntries[$entryIndex]
		if ($recorded.path -cne $actual.path -or $recorded.bytes -ne $actual.bytes -or $recorded.sha256 -cne $actual.sha256) {
			throw "Data content mismatch for '$($actual.path)'."
		}
	}
	$computedAggregate = Get-DataAggregate $actualEntries
	if ($recordedAggregate -cne $computedAggregate) { throw 'Receipt aggregate does not match current Data inventory.' }

	$result.status = 'pass'
	$result.code = 'ok'
	$result.message = 'Data oracle receipt verified.'
	$result.receiptPath = $canonicalReceipt
	$result.receiptSha256 = $actualReceiptSha256
	$result.aggregateDigest = $computedAggregate
	$exitCode = 0
}
catch {
	$result.message = $_.Exception.Message
}
finally {
	if ($null -ne $document) { $document.Dispose() }
}

[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 6 -Compress))
exit $exitCode
