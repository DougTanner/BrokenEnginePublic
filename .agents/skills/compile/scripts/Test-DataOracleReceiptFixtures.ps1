# Integration fixtures for the Data oracle producer/verifier. All Data files are tiny.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$producer = Join-Path $PSScriptRoot 'New-DataOracleReceipt.ps1'
$verifier = Join-Path $PSScriptRoot 'Test-DataOracleReceipt.ps1'
$pwsh = (Get-Command pwsh -ErrorAction Stop).Source
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$baseline = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
$otherBaseline = 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
$expectedFiles = @(
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
[Array]::Sort($expectedFiles, [StringComparer]::Ordinal)

$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('BrokenEngineDataOracle-' + [guid]::NewGuid().ToString('N'))
$workspace = Join-Path $fixtureRoot 'workspace'
$junctions = [Collections.Generic.List[string]]::new()
$cases = [Collections.Generic.List[string]]::new()

function Assert-True([bool] $Condition, [string] $Message) {
	if (-not $Condition) { throw "Fixture assertion failed: $Message" }
}

function Assert-Contained([string] $Root, [string] $Path, [string] $Label) {
	$rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
	$pathFull = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
	$relative = [IO.Path]::GetRelativePath($rootFull, $pathFull)
	if ([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith("..$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::Ordinal) -or
		$relative.StartsWith("..$([IO.Path]::AltDirectorySeparatorChar)", [StringComparison]::Ordinal)) {
		throw "$Label escaped fixture root: '$pathFull'."
	}
}

function New-DataDirectory([string] $Name, [string] $Tag) {
	$path = Join-Path $workspace $Name
	New-Item -ItemType Directory -Path $path -ErrorAction Stop | Out-Null
	foreach ($file in $expectedFiles) {
		[IO.File]::WriteAllText((Join-Path $path $file), "$Tag/$file", $utf8)
	}
	return $path
}

function Get-Sha256([string] $Path) {
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Invoke-FixtureScript([string] $Script, [string[]] $Arguments) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $pwsh
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.ArgumentList.Add('-NoProfile')
	$start.ArgumentList.Add('-File')
	$start.ArgumentList.Add($Script)
	foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	try {
		[void]$process.Start()
		$stdout = $process.StandardOutput.ReadToEnd()
		$stderr = $process.StandardError.ReadToEnd()
		$process.WaitForExit()
		$json = $null
		try { $json = $stdout.Trim() | ConvertFrom-Json -Depth 20 }
		catch { throw "Script '$Script' did not emit one JSON result. stdout='$stdout' stderr='$stderr'." }
		return [pscustomobject]@{ ExitCode = $process.ExitCode; Json = $json; Stdout = $stdout; Stderr = $stderr }
	}
	finally { $process.Dispose() }
}

function New-Receipt([string] $DataRoot, [string] $Mode, [string] $Path) {
	$run = Invoke-FixtureScript $producer @('-DataRoot', $DataRoot, '-Mode', $Mode, '-Baseline', $baseline, '-ReceiptPath', $Path)
	Assert-True ($run.ExitCode -eq 0 -and $run.Json.status -ceq 'pass' -and $run.Json.code -ceq 'ok') "producer failed: $($run.Stdout) $($run.Stderr)"
	return $run.Json
}

function Test-Receipt([string] $Path, [string] $Sha256, [string] $DataRoot, [string] $Mode, [string] $ExpectedCommit) {
	return Invoke-FixtureScript $verifier @('-ReceiptPath', $Path, '-ReceiptSha256', $Sha256, '-ExpectedDataRoot', $DataRoot, '-ExpectedMode', $Mode, '-ExpectedBaseline', $ExpectedCommit)
}

function Assert-VerifierPass([object] $Run, [string] $Name) {
	Assert-True ($Run.ExitCode -eq 0 -and $Run.Json.status -ceq 'pass' -and $Run.Json.code -ceq 'ok') "$Name should pass: $($Run.Stdout) $($Run.Stderr)"
	$cases.Add($Name)
}

function Assert-VerifierReject([object] $Run, [string] $Name) {
	Assert-True ($Run.ExitCode -ne 0 -and $Run.Json.status -ceq 'error' -and $Run.Json.code -ceq 'validation.failed') "$Name should reject: $($Run.Stdout) $($Run.Stderr)"
	$cases.Add($Name)
}

function Assert-ProducerReject([object] $Run, [string] $Name) {
	Assert-True ($Run.ExitCode -ne 0 -and $Run.Json.status -ceq 'error') "$Name should reject: $($Run.Stdout) $($Run.Stderr)"
	$cases.Add($Name)
}

$result = [ordered]@{ schemaVersion = 'broken-engine-data-oracle-fixtures/v1'; status = 'error'; code = 'fixture.failed'; cases = @(); message = 'Fixtures did not run.' }
$exitCode = 1
try {
	New-Item -ItemType Directory -Path $workspace -Force | Out-Null

	$sharedData = New-DataDirectory 'shared-valid' 'shared'
	$sharedReceiptPath = Join-Path $workspace 'shared-receipt.json'
	[IO.File]::WriteAllText($sharedReceiptPath, 'pre-existing receipt bytes', $utf8)
	$shared = New-Receipt $sharedData 'Shared' $sharedReceiptPath
	Assert-VerifierPass (Test-Receipt $shared.receiptPath $shared.receiptSha256 $sharedData 'Shared' $baseline) 'valid exact Shared inventory'
	$sharedBytes = [IO.File]::ReadAllBytes($sharedReceiptPath)
	Assert-True (-not ($sharedBytes.Length -ge 3 -and $sharedBytes[0] -eq 0xef -and $sharedBytes[1] -eq 0xbb -and $sharedBytes[2] -eq 0xbf)) 'receipt contains UTF-8 BOM'
	[void]$utf8.GetString($sharedBytes)
	Assert-True (@(Get-ChildItem -LiteralPath $workspace -Filter '.shared-receipt.json.*.tmp' -Force).Count -eq 0) 'atomic write left a sibling temporary'
	$cases.Add('UTF-8 no-BOM atomic replacement')

	$localData = New-DataDirectory 'local-valid' 'local-different'
	$local = New-Receipt $localData 'Local' (Join-Path $workspace 'local-receipt.json')
	Assert-VerifierPass (Test-Receipt $local.receiptPath $local.receiptSha256 $localData 'Local' $baseline) 'valid exact Local inventory'
	Assert-True ($local.aggregateDigest -cne $shared.aggregateDigest) 'independent Shared and Local receipts should differ for differing bytes'
	$cases.Add('independent differing Shared and Local receipts')

	$emptyProducerData = New-DataDirectory 'empty-producer' 'empty-producer'
	[IO.File]::WriteAllBytes((Join-Path $emptyProducerData 'Audio.h'), [byte[]]@())
	$emptyProducer = Invoke-FixtureScript $producer @('-DataRoot', $emptyProducerData, '-Mode', 'Local', '-Baseline', $baseline, '-ReceiptPath', (Join-Path $workspace 'empty-producer-receipt.json'))
	Assert-ProducerReject $emptyProducer 'producer rejects empty entry'
	Assert-True ($emptyProducer.Json.message -like '*must not be empty*') "producer empty entry rejection did not report the empty-entry rule: $($emptyProducer.Stdout) $($emptyProducer.Stderr)"

	$emptyLiveData = New-DataDirectory 'empty-live' 'empty-live'
	$emptyLive = New-Receipt $emptyLiveData 'Local' (Join-Path $workspace 'empty-live-receipt.json')
	[IO.File]::WriteAllBytes((Join-Path $emptyLiveData 'Audio.h'), [byte[]]@())
	$emptyLiveRun = Test-Receipt $emptyLive.receiptPath $emptyLive.receiptSha256 $emptyLiveData 'Local' $baseline
	Assert-VerifierReject $emptyLiveRun 'verifier rejects empty live entry'
	Assert-True ($emptyLiveRun.Json.message -like '*must not be empty*') "verifier live empty entry rejection did not report the empty-entry rule: $($emptyLiveRun.Stdout) $($emptyLiveRun.Stderr)"

	$emptyReceiptPath = Join-Path $workspace 'empty-receipt-entry.json'
	$emptyReceiptText = $utf8.GetString($sharedBytes)
	$firstEntryPattern = [regex]::new('(?<prefix>"entries":\[\{"path":"[^"]+","bytes":)\d+(?<shaPrefix>,"sha256":")[0-9a-f]{64}(?<suffix>")')
	Assert-True ($firstEntryPattern.Matches($emptyReceiptText).Count -eq 1) 'receipt first entry mutation target is not unique'
	$emptyReceiptText = $firstEntryPattern.Replace($emptyReceiptText, '${prefix}0${shaPrefix}e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855${suffix}', 1)
	[IO.File]::WriteAllText($emptyReceiptPath, $emptyReceiptText, $utf8)
	$emptyReceiptRun = Test-Receipt $emptyReceiptPath (Get-Sha256 $emptyReceiptPath) $sharedData 'Shared' $baseline
	Assert-VerifierReject $emptyReceiptRun 'verifier rejects empty receipt entry'
	Assert-True ($emptyReceiptRun.Json.message -like '*must be positive*') "verifier receipt empty entry rejection did not report the positive-byte rule: $($emptyReceiptRun.Stdout) $($emptyReceiptRun.Stderr)"

	$addedData = New-DataDirectory 'added' 'added'
	$added = New-Receipt $addedData 'Local' (Join-Path $workspace 'added-receipt.json')
	[IO.File]::WriteAllText((Join-Path $addedData 'extra.txt'), 'extra', $utf8)
	Assert-VerifierReject (Test-Receipt $added.receiptPath $added.receiptSha256 $addedData 'Local' $baseline) 'added entry mismatch'

	$missingData = New-DataDirectory 'missing' 'missing'
	$missing = New-Receipt $missingData 'Local' (Join-Path $workspace 'missing-receipt.json')
	Remove-Item -LiteralPath (Join-Path $missingData 'Raw.pack') -Force
	Assert-VerifierReject (Test-Receipt $missing.receiptPath $missing.receiptSha256 $missingData 'Local' $baseline) 'missing entry mismatch'

	$substitutedData = New-DataDirectory 'substituted' 'substituted'
	$substituted = New-Receipt $substitutedData 'Local' (Join-Path $workspace 'substituted-receipt.json')
	Remove-Item -LiteralPath (Join-Path $substitutedData 'Audio.h') -Force
	New-Item -ItemType Directory -Path (Join-Path $substitutedData 'Audio.h') | Out-Null
	Assert-VerifierReject (Test-Receipt $substituted.receiptPath $substituted.receiptSha256 $substitutedData 'Local' $baseline) 'substituted entry mismatch'

	$reparseData = New-DataDirectory 'reparse' 'reparse'
	$reparse = New-Receipt $reparseData 'Local' (Join-Path $workspace 'reparse-receipt.json')
	$junctionTarget = Join-Path $workspace 'junction-target'
	New-Item -ItemType Directory -Path $junctionTarget | Out-Null
	$junction = Join-Path $reparseData 'Raw.h'
	Remove-Item -LiteralPath $junction -Force
	New-Item -ItemType Junction -Path $junction -Target $junctionTarget | Out-Null
	$junctions.Add($junction)
	Assert-VerifierReject (Test-Receipt $reparse.receiptPath $reparse.receiptSha256 $reparseData 'Local' $baseline) 'reparse entry mismatch'

	$mutatedData = New-DataDirectory 'mutated' 'before-mutation'
	$mutated = New-Receipt $mutatedData 'Local' (Join-Path $workspace 'mutated-receipt.json')
	[IO.File]::WriteAllText((Join-Path $mutatedData 'Data.h'), 'after-mutation', $utf8)
	Assert-VerifierReject (Test-Receipt $mutated.receiptPath $mutated.receiptSha256 $mutatedData 'Local' $baseline) 'file content mutation mismatch'

	Assert-VerifierReject (Test-Receipt $shared.receiptPath ('0' * 64) $sharedData 'Shared' $baseline) 'receipt hash mismatch'
	Assert-VerifierReject (Test-Receipt (Join-Path $workspace 'nonexistent-receipt.json') $shared.receiptSha256 $sharedData 'Shared' $baseline) 'receipt path mismatch'

	$tamperPath = Join-Path $workspace 'tampered-receipt.json'
	[IO.File]::WriteAllBytes($tamperPath, $sharedBytes)
	$tamperedValue = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($tamperPath)) | ConvertFrom-Json -Depth 20
	$tamperedValue | Add-Member -NotePropertyName unexpected -NotePropertyValue $true
	[IO.File]::WriteAllText($tamperPath, ($tamperedValue | ConvertTo-Json -Depth 20 -Compress), $utf8)
	Assert-VerifierReject (Test-Receipt $tamperPath (Get-Sha256 $tamperPath) $sharedData 'Shared' $baseline) 'receipt schema tamper'

	$wrongRoot = New-DataDirectory 'wrong-root' 'same-shape-other-root'
	Assert-VerifierReject (Test-Receipt $shared.receiptPath $shared.receiptSha256 $wrongRoot 'Shared' $baseline) 'wrong root mismatch'
	Assert-VerifierReject (Test-Receipt $shared.receiptPath $shared.receiptSha256 $sharedData 'Local' $baseline) 'wrong mode mismatch'
	Assert-VerifierReject (Test-Receipt $shared.receiptPath $shared.receiptSha256 $sharedData 'Shared' $otherBaseline) 'wrong baseline mismatch'

	$result.status = 'pass'
	$result.code = 'ok'
	$result.cases = @($cases)
	$result.message = 'All Data oracle fixtures passed.'
	$exitCode = 0
}
catch {
	$result.cases = @($cases)
	$result.message = $_.Exception.Message
}
finally {
	foreach ($junction in $junctions) {
		if (Test-Path -LiteralPath $junction) {
			Assert-Contained $workspace $junction 'Junction cleanup target'
			Remove-Item -LiteralPath $junction -Force
		}
	}
	if (Test-Path -LiteralPath $workspace) {
		Assert-Contained $fixtureRoot $workspace 'Recursive cleanup target'
		Remove-Item -LiteralPath $workspace -Recurse -Force
	}
	if (Test-Path -LiteralPath $fixtureRoot) { Remove-Item -LiteralPath $fixtureRoot -Force }
}

[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 8 -Compress))
exit $exitCode
