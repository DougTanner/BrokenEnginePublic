# detect-python.ps1
# Detects a usable python.exe (>= 3.10), skipping the Windows Store shim and the
# `py` launcher (the launcher is on PATH but isn't a direct interpreter — skills
# pass the detected exe path explicitly to avoid `python` resolving to nothing).
# Usage (execute; capture the printed exe path for subsequent Python calls):
#   pwsh -ExecutionPolicy Bypass -File .claude/skills/gaea2-shared/scripts/detect-python.ps1
# Exit codes:
#   0 - found, prints "OK <python.exe-path> Python <major>.<minor>"
#   1 - missing or version too old, prints reason
#
# Bootstrap for the gaea2-* family skills (and gaea1-load); they pass the
# detected exe path explicitly to the Python scripts in this directory.

$minMajor = 3
$minMinor = 10

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

# Probe each candidate once; remember version for the STALE pass.
$versions = @{}
foreach ($exe in $candidates) {
    $versionLine = & $exe --version 2>&1 | Out-String
    $versions[$exe] = $versionLine.Trim()
    if ($versionLine -match 'Python\s+(\d+)\.(\d+)\.(\d+)') {
        $major = [int]$Matches[1]
        $minor = [int]$Matches[2]
        if ($major -gt $minMajor -or ($major -eq $minMajor -and $minor -ge $minMinor)) {
            Write-Output "OK $exe Python $major.$minor"
            exit 0
        }
    }
}

# Found one, but too old.
foreach ($exe in $candidates) {
    if ($versions[$exe] -match 'Python\s+\d+\.\d+\.\d+') {
        Write-Output "STALE $exe $($versions[$exe]) (need >= $minMajor.$minMinor)"
        exit 1
    }
}

Write-Output "MISSING no usable Python found (need >= $minMajor.$minMinor); install with: winget install Python.Python.3.12 --accept-source-agreements --accept-package-agreements"
exit 1
