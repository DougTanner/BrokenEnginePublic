[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $EtlPath,

	[Parameter(Mandatory = $true)]
	[string] $RepositoryRoot,

	[Parameter(Mandatory = $true)]
	[string] $OutputPath,

	[string] $SymbolCacheRoot = $env:TEMP
)

$ErrorActionPreference = 'Stop'

$xperf = 'C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe'
if (-not (Test-Path -LiteralPath $xperf -PathType Leaf)) {
	throw "Windows Performance Toolkit xperf is missing: '$xperf'."
}
if (-not (Test-Path -LiteralPath $EtlPath -PathType Leaf)) {
	throw "ETL input is missing: '$EtlPath'."
}
if (-not (Test-Path -LiteralPath $RepositoryRoot -PathType Container)) {
	throw "Repository root is missing: '$RepositoryRoot'."
}
if ([string]::IsNullOrWhiteSpace($SymbolCacheRoot)) {
	throw 'SymbolCacheRoot must not be empty.'
}

$repository = [System.IO.Path]::GetFullPath($RepositoryRoot)
$cacheRoot = [System.IO.Path]::GetFullPath($SymbolCacheRoot)
$processStart = [System.Diagnostics.ProcessStartInfo]::new()
$processStart.FileName = $xperf
$processStart.UseShellExecute = $false
$processStart.Environment['_NT_SYMBOL_PATH'] = "$(Join-Path $repository 'Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output');srv*$(Join-Path $cacheRoot 'symsrv')*https://msdl.microsoft.com/download/symbols"
$processStart.Environment['_NT_SYMCACHE_PATH'] = Join-Path $cacheRoot 'symc'
foreach ($argument in @('-i', $EtlPath, '-symbols', '-tle', '-tti', '-a', 'profile', '-detail', '-ao', $OutputPath)) {
	$processStart.ArgumentList.Add($argument)
}

$process = [System.Diagnostics.Process]::Start($processStart)
$process.WaitForExit()
exit $process.ExitCode
