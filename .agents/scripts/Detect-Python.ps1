# Detect-Python.ps1
# Detects a usable host python.exe (x64 CPython >= 3.12), skipping the Windows
# Store shim and the `py` launcher (the launcher is on PATH but isn't a direct
# interpreter — callers pass the detected exe path explicitly to avoid `python`
# resolving to nothing).
# Usage (run from repository root; capture the printed exe path for subsequent
# Python calls):
#   pwsh -ExecutionPolicy Bypass -File .agents/scripts/Detect-Python.ps1
# Exit codes:
#   0 - found, prints "OK <python.exe-path> Python <major>.<minor>"
#   1 - missing or unsuitable, prints reason
#
# Shared bootstrap for the skills that need host Python (gaea2-* and
# analyze-diagsession); each passes the detected exe path explicitly to its own
# Python scripts.

$minVersion = [Version]'3.12.0'

# Direct python.exe candidates (PATH first; winget installs don't refresh PATH in
# the current shell, so probe well-known install locations too).
$candidates = @()
foreach ($name in @('python', 'python3')) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source -notlike '*\WindowsApps\*' -and $cmd.Source -like '*python*.exe') {
        $candidates += $cmd.Source
    }
}

# Directories whose subdirs each contain a python.exe (winget, ProgramFiles,
# choco-by-version, pyenv-win versions, UV-managed installs).
$pythonRootDirs = @(
    "$env:LOCALAPPDATA\Programs\Python",
    "$env:ProgramFiles\Python*",
    "${env:ProgramFiles(x86)}\Python*",
    "C:\Python*",
    "C:\tools\python*",
    "$env:USERPROFILE\.pyenv\pyenv-win\versions",
    "$env:LOCALAPPDATA\uv\python"
) | Where-Object { $_ }
foreach ($dir in $pythonRootDirs) {
    Get-ChildItem -Path $dir -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        $exe = Join-Path $_.FullName 'python.exe'
        if (Test-Path $exe) { $candidates += $exe }
    }
}

# Direct python.exe locations (conda root, scoop current, active venv).
$directExes = @(
    "$env:USERPROFILE\miniconda3\python.exe",
    "$env:USERPROFILE\anaconda3\python.exe",
    "$env:ProgramData\miniconda3\python.exe",
    "$env:ProgramData\anaconda3\python.exe",
    "$env:USERPROFILE\scoop\apps\python\current\python.exe"
)
if ($env:VIRTUAL_ENV) {
    $directExes += (Join-Path $env:VIRTUAL_ENV 'Scripts\python.exe')
}
foreach ($exe in $directExes) {
    if (Test-Path $exe) { $candidates += $exe }
}

$candidates = $candidates | Select-Object -Unique

# Probe each candidate once; remember what answered for the STALE pass.
$probes = @{}
foreach ($exe in $candidates) {
    $probeText = & $exe -c 'import json,platform,sys; print(json.dumps({"implementation":platform.python_implementation(),"version":"%d.%d.%d"%sys.version_info[:3],"arch":platform.machine()}))' 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $probeText) { continue }
    try { $probe = $probeText | ConvertFrom-Json } catch { continue }
    try { $version = [Version]$probe.version } catch { continue }
    $probes[$exe] = "$($probe.implementation) $($probe.version) $($probe.arch)"
    if ($probe.implementation -eq 'CPython' -and $probe.arch -match '^(AMD64|x86_64)$' -and $version -ge $minVersion) {
        Write-Output "OK $exe Python $($version.Major).$($version.Minor)"
        exit 0
    }
}

# Found one that answered, but it does not meet the requirement.
foreach ($exe in $candidates) {
    if ($probes.ContainsKey($exe)) {
        Write-Output "STALE $exe $($probes[$exe]) (need x64 CPython >= $($minVersion.Major).$($minVersion.Minor))"
        exit 1
    }
}

Write-Output "MISSING no usable Python found (need x64 CPython >= $($minVersion.Major).$($minVersion.Minor)); install with: winget install Python.Python.3.12 --accept-source-agreements --accept-package-agreements"
exit 1
