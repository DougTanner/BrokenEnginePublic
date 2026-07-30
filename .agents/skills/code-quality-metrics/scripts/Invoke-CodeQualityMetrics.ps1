[CmdletBinding()]
param(
    [string]$Mode,
    [string]$Target,
    [string]$Scope,
    [string]$TargetManifest,
    [string]$Baseline,
    [string]$RepositoryRoot,
    [string]$Profile = 'BrokenEngineExtended',
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Diagnostic([string]$Message) { [Console]::Error.WriteLine("CodeQualityMetrics: $Message") }
function Fail([string]$Message) { Write-Diagnostic $Message; exit 2 }
function Get-TargetFailureDiagnostic([string]$Diagnostics) {
    $lines = @($Diagnostics -split "`r?`n" | Where-Object { $_ })
    if ($lines.Count -ne 1) { return $null }
    try {
        $diagnostic = $lines[0] | ConvertFrom-Json -ErrorAction Stop
        if ($diagnostic.code -notin @('target-parse-failure', 'target-signature-extraction-failure') -or $diagnostic.message -isnot [string] -or $null -eq $diagnostic.failures) { return $null }
        if (@($diagnostic.PSObject.Properties.Name | Sort-Object) -join ',' -ne 'code,failures,message') { return $null }
        foreach ($failure in @($diagnostic.failures)) {
            if (@($failure.PSObject.Properties.Name | Sort-Object) -join ',' -ne 'code,column,line,path,side,stage' -or $failure.side -notin @('baseline', 'current') -or $failure.path -isnot [string] -or $failure.stage -notin @('dispatch-parse', 'signature-extraction') -or $failure.code -isnot [string] -or $failure.line -isnot [long] -or $failure.column -isnot [long]) { return $null }
            if ($failure.line -lt 1 -or $failure.column -lt 0 -or ($failure.stage -eq 'dispatch-parse' -and $failure.code -notin @('dispatch-parse-failure', 'normalized-tree-error', 'normalized-tree-missing-node', 'normalized-tree-unavailable')) -or ($failure.stage -eq 'signature-extraction' -and $failure.code -ne 'signature-extraction-failure')) { return $null }
            if (($diagnostic.code -eq 'target-parse-failure' -and $failure.stage -ne 'dispatch-parse') -or ($diagnostic.code -eq 'target-signature-extraction-failure' -and $failure.stage -ne 'signature-extraction')) { return $null }
        }
        return $lines[0]
    }
    catch { return $null }
}
function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert-OrdinaryDirectory([string]$Path, [string]$Root) {
    $resolvedRoot = [IO.Path]::GetFullPath($Root)
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    if ($resolvedPath -ne $resolvedRoot -and -not $resolvedPath.StartsWith($resolvedRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Cache path escapes RepositoryRoot.' }
    $relative = [IO.Path]::GetRelativePath($resolvedRoot, $resolvedPath)
    $current = $resolvedRoot
    foreach ($part in $relative.Split([IO.Path]::DirectorySeparatorChar, [StringSplitOptions]::RemoveEmptyEntries)) {
        $current = Join-Path $current $part
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Cache path contains a nonordinary or reparse directory.' }
        }
    }
}
function Assert-OrdinaryPopulatedDirectory([string]$Path, [string]$Description) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "$Description must be an ordinary directory." }
    if (-not (Get-ChildItem -LiteralPath $Path -Force | Select-Object -First 1)) { throw "$Description is empty." }
}
function Assert-OrdinaryFile([string]$Path, [string]$Description) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "$Description must be an ordinary file." }
}
function Get-CanonicalPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($full)
    if ($full.Length -gt $root.Length) { return $full.TrimEnd('\', '/') }
    return $full
}
function Invoke-GitLines([string]$Root, [string[]]$Arguments, [string]$Description) {
    $output = @(& git -C $Root @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "$Description failed: $($output -join '; ')" }
    return $output
}
function Get-TreeGitlink([string]$Root, [string]$Revision, [string]$Description) {
    $entries = @(Invoke-GitLines $Root @('ls-tree', $Revision, '--', 'ThirdParty/scb-check') $Description)
    if ($entries.Count -ne 1 -or $entries[0] -notmatch '^160000 commit ([0-9a-f]{40})\tThirdParty/scb-check$') { throw "$Description must contain one mode-160000 scb-check gitlink." }
    return $Matches[1]
}
function Get-IndexGitlink([string]$Root) {
    $entries = @(Invoke-GitLines $Root @('ls-files', '--stage', '--', 'ThirdParty/scb-check') 'Current index query')
    if ($entries.Count -ne 1 -or $entries[0] -notmatch '^160000 ([0-9a-f]{40}) 0\tThirdParty/scb-check$') { throw 'Current index must contain one mode-160000 stage-0 scb-check gitlink.' }
    return $Matches[1]
}
function Get-RegisteredWorktreeRecords([string]$Root) {
    $records = [Collections.Generic.List[object]]::new()
    $record = $null
    foreach ($line in @(Invoke-GitLines $Root @('worktree', 'list', '--porcelain') 'Git worktree list')) {
        if ($line -eq '') { continue }
        elseif ($line -match '^worktree (.+)$') {
            if ($null -ne $record) { $records.Add([pscustomobject]$record) }
            $record = [ordered]@{ Path = Get-CanonicalPath $Matches[1]; Bare = $false; Prunable = $false }
        }
        elseif ($null -eq $record) { throw 'Git worktree list has a field before its worktree record.' }
        elseif ($line -match '^HEAD [0-9a-f]{40}$' -or $line -match '^branch refs/heads/.+$' -or $line -eq 'detached' -or $line -match '^locked(?: .*)?$') { continue }
        elseif ($line -eq 'bare') { $record.Bare = $true }
        elseif ($line -match '^prunable(?: .*)?$') { $record.Prunable = $true }
        else { throw "Git worktree list has an unrecognized record field: '$line'." }
    }
    if ($null -ne $record) { $records.Add([pscustomobject]$record) }
    if ($records.Count -eq 0) { throw 'Git worktree list returned no worktree records.' }
    return @($records)
}
function Get-AuthoritativeScbCheck([string]$Repository) {
    $topLevel = Get-CanonicalPath (@(Invoke-GitLines $Repository @('rev-parse', '--show-toplevel') 'Repository top-level')[0].Trim())
    if (-not $topLevel.Equals($Repository, [StringComparison]::OrdinalIgnoreCase)) { throw 'RepositoryRoot is not the Git top level.' }
    $commonDirectory = Get-CanonicalPath (@(Invoke-GitLines $Repository @('rev-parse', '--path-format=absolute', '--git-common-dir') 'Git common directory')[0].Trim())
    $records = @(Get-RegisteredWorktreeRecords $Repository)
    $current = @($records | Where-Object { $_.Path.Equals($Repository, [StringComparison]::OrdinalIgnoreCase) })
    if ($current.Count -ne 1 -or $current[0].Bare -or $current[0].Prunable) { throw 'RepositoryRoot must be one live registered non-bare worktree.' }
    $primary = @($records | Where-Object {
        if ($_.Bare -or $_.Prunable) { return $false }
        $gitDirectory = Get-Item -LiteralPath (Join-Path $_.Path '.git') -Force -ErrorAction SilentlyContinue
        return $null -ne $gitDirectory -and $gitDirectory.PSIsContainer -and -not ($gitDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -and
            (Get-CanonicalPath $gitDirectory.FullName).Equals($commonDirectory, [StringComparison]::OrdinalIgnoreCase)
    })
    if ($primary.Count -ne 1) { throw "Expected exactly one ordinary primary checkout; found $($primary.Count)." }

    $currentHeadPin = Get-TreeGitlink $Repository 'HEAD' 'Committed worktree HEAD'
    $currentIndexPin = Get-IndexGitlink $Repository
    $primaryPin = Get-TreeGitlink $primary[0].Path 'HEAD' 'Primary HEAD'
    $sourceRepository = Get-CanonicalPath (Join-Path $primary[0].Path 'ThirdParty/scb-check')
    Assert-OrdinaryPopulatedDirectory $sourceRepository 'Primary scb-check checkout'
    $sourceTopLevel = Get-CanonicalPath (@(Invoke-GitLines $sourceRepository @('rev-parse', '--show-toplevel') 'Primary scb-check top-level')[0].Trim())
    if (-not $sourceTopLevel.Equals($sourceRepository, [StringComparison]::OrdinalIgnoreCase)) { throw 'Primary scb-check checkout is not its Git top level.' }
    $sourceHead = @(Invoke-GitLines $sourceRepository @('rev-parse', 'HEAD') 'Primary scb-check HEAD')[0].Trim()
    if ($sourceHead -notmatch '^[0-9a-f]{40}$') { throw 'Primary scb-check HEAD is malformed.' }
    if ($currentHeadPin -ne $primaryPin -or $currentHeadPin -ne $currentIndexPin -or $currentHeadPin -ne $sourceHead) {
        throw 'Committed scb-check gitlink pin mismatch.'
    }
    if (@(Invoke-GitLines $sourceRepository @('status', '--porcelain', '--untracked-files=all') 'Primary scb-check status').Count -ne 0) { throw 'Primary scb-check checkout has local changes.' }
    $lock = Join-Path $sourceRepository 'requirements.lock'
    $source = Join-Path $sourceRepository 'src'
    Assert-OrdinaryFile $lock 'Primary scb-check requirements.lock'
    Assert-OrdinaryPopulatedDirectory $source 'Primary scb-check src'
    return [pscustomobject]@{ Pin = $currentHeadPin; SourceRepository = $sourceRepository }
}
function New-ImmutableScbCheckSource([string]$SourceRepository, [string]$Pin, [string]$CacheRoot, [string]$Repository) {
    $stageCreated = $false
    for ($attempt = 0; $attempt -lt 64; ++$attempt) {
        $stageLeaf = "m-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
        $stage = Join-Path $CacheRoot $stageLeaf
        try {
            New-Item -ItemType Directory -Path $stage -ErrorAction Stop | Out-Null
            $stageCreated = $true
            break
        }
        catch {
            if ($_.CategoryInfo.Category -ne [Management.Automation.ErrorCategory]::ResourceExists) {
                throw "Immutable scb-check source stage creation failed for '$stageLeaf': $($_.Exception.Message)"
            }
        }
    }
    if (-not $stageCreated) { throw 'Immutable scb-check source stage creation exhausted 64 collision retries.' }
    try {
        $archive = Join-Path $stage 'source.zip'
        & git -C $SourceRepository archive --format=zip "--output=$archive" $Pin
        if ($LASTEXITCODE -ne 0) { throw 'Immutable scb-check archive creation failed.' }
        Assert-OrdinaryFile $archive 'Immutable scb-check archive'
        $root = Join-Path $stage 'source'
        Expand-Archive -LiteralPath $archive -DestinationPath $root -ErrorAction Stop
        Assert-OrdinaryPopulatedDirectory $root 'Immutable scb-check source root'
        $reparse = Get-ChildItem -LiteralPath $root -Force -Recurse -Attributes ReparsePoint | Select-Object -First 1
        if ($reparse) { throw "Immutable scb-check source contains a reparse point: '$($reparse.FullName)'." }
        $lock = Join-Path $root 'requirements.lock'
        $source = Join-Path $root 'src'
        Assert-OrdinaryFile $lock 'Immutable scb-check requirements.lock'
        Assert-OrdinaryPopulatedDirectory $source 'Immutable scb-check src'
        return [pscustomobject]@{ Stage = $stage; Leaf = $stageLeaf; Lock = $lock; Source = $source }
    }
    catch {
        $materializationFailure = $_
        try {
            Remove-BootstrapDirectory $stage $stageLeaf $CacheRoot $Repository
        }
        catch {
            throw "Immutable scb-check materialization failed: $($materializationFailure.Exception.Message)`nImmutable scb-check source cleanup failed: $($_.Exception.Message)"
        }
        throw $materializationFailure
    }
}
function Remove-BootstrapDirectory([string]$Path, [string]$ExpectedLeaf, [string]$CacheRoot, [string]$RepositoryRoot) {
    Assert-OrdinaryDirectory (Join-Path $RepositoryRoot 'Temp') $RepositoryRoot
    Assert-OrdinaryDirectory $CacheRoot $RepositoryRoot
    Assert-OrdinaryDirectory $Path $RepositoryRoot
    if ([IO.Path]::GetFullPath((Split-Path -Parent $Path)) -ne [IO.Path]::GetFullPath($CacheRoot) -or (Split-Path -Leaf $Path) -ne $ExpectedLeaf) { throw 'Refusing to delete an unsafe bootstrap cache path.' }
    if ((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing to delete a reparse-point bootstrap cache path.' }
    Remove-Item -LiteralPath $Path -Force -Recurse
}
function Test-BootstrapEnvironment([string]$Environment, [string]$Key, [string]$PythonExecutable, [string]$LockSha, [string]$Source) {
    $complete = Join-Path $Environment 'complete.json'
    if (-not (Test-Path -LiteralPath $complete -PathType Leaf)) { return $false }
    try {
        $record = Get-Content -Raw -LiteralPath $complete | ConvertFrom-Json
        $venvPython = Join-Path $Environment 'Scripts\python.exe'
        $sg = Join-Path $Environment 'Scripts\sg.exe'
        if ($record.key -ne $Key -or $record.python -ne $PythonExecutable -or $record.lockSha256 -ne $LockSha -or
            -not (Test-Path -LiteralPath $venvPython -PathType Leaf) -or -not (Test-Path -LiteralPath $sg -PathType Leaf) -or
            $record.venvPythonSha256 -ne (Get-Sha256 $venvPython) -or $record.sgSha256 -ne (Get-Sha256 $sg)) { return $false }
        $identityText = & $venvPython -c 'import json, os, subprocess, sys; sys.path.insert(0, sys.argv[1]); import scb_check, scb_check.pipeline; sg=os.path.join(os.path.dirname(sys.executable), "sg.exe"); run=subprocess.run([sg, "--version"], capture_output=True, text=True); print(json.dumps({"python":os.path.abspath(sys.executable),"prefix":os.path.abspath(sys.prefix),"module":os.path.abspath(scb_check.__file__),"sg":os.path.abspath(sg),"sgExitCode":run.returncode,"sgVersion":run.stdout.strip()}))' $Source
        if ($LASTEXITCODE -ne 0) { return $false }
        $identity = $identityText | ConvertFrom-Json
        $expectedModuleRoot = [IO.Path]::GetFullPath((Join-Path $Source 'scb_check')) + [IO.Path]::DirectorySeparatorChar
        return $identity.python -eq [IO.Path]::GetFullPath($venvPython) -and $identity.prefix -eq [IO.Path]::GetFullPath($Environment) -and
            $identity.module.StartsWith($expectedModuleRoot, [StringComparison]::OrdinalIgnoreCase) -and $identity.sg -eq [IO.Path]::GetFullPath($sg) -and
            $identity.sgExitCode -eq 0 -and $identity.sgVersion -eq $record.sgVersion
    } catch { return $false }
}

$failure = $null
$targetFailure = $null
$pendingText = $null
$pendingOutputPath = $null
$sourceStage = $null
$cacheRoot = $null
$repository = $null
try {
    if ($Mode -notin @('Snapshot', 'Compare')) { throw 'Mode must be Snapshot or Compare.' }
    if (-not $RepositoryRoot) { throw 'RepositoryRoot must be an existing absolute directory.' }
    if ($Profile -notin @('BrokenEngineExtended', 'StrictUpstream')) { throw 'Profile must be BrokenEngineExtended or StrictUpstream.' }
    if ($Scope -and $Scope -notin @('Exact', 'Directory', 'Recursive')) { throw 'Scope must be Exact, Directory, or Recursive.' }
    if ($Baseline -and $Baseline -notmatch '^[0-9a-f]{40}$') { throw 'Baseline must be a 40-character lowercase hexadecimal commit SHA.' }
    if ($Mode -eq 'Snapshot' -and ((-not $Target) -or (-not $Scope) -or $TargetManifest -or $Baseline)) { throw 'Snapshot requires only Target and Scope.' }
    if ($Mode -eq 'Compare' -and ((-not $TargetManifest) -or (-not $Baseline) -or $Target -or $Scope)) { throw 'Compare requires only TargetManifest and Baseline.' }
    $repository = Get-CanonicalPath $RepositoryRoot
    if (-not [IO.Path]::IsPathFullyQualified($RepositoryRoot) -or -not (Test-Path -LiteralPath $repository -PathType Container)) { throw 'RepositoryRoot must be an existing absolute directory.' }
    $rootItem = Get-Item -LiteralPath $repository -Force
    if ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'RepositoryRoot must not be a reparse point.' }
    $authoritative = Get-AuthoritativeScbCheck $repository
    $gitLinkSha = $authoritative.Pin
    $pythonCommand = Get-Command python -CommandType Application -ErrorAction Stop | Select-Object -First 1
    $python = $pythonCommand.Source
    $probeText = & $python -c 'import json,platform,sys; print(json.dumps({"implementation":platform.python_implementation(),"version":"%d.%d.%d"%sys.version_info[:3],"arch":platform.machine(),"exe":sys.executable}))'
    if ($LASTEXITCODE -ne 0) { throw 'Python probe failed.' }
    $probe = $probeText | ConvertFrom-Json
    try { $pythonVersion = [Version]$probe.version } catch { throw 'Python probe returned an invalid version.' }
    if ($probe.implementation -ne 'CPython' -or $probe.arch -notmatch '^(AMD64|x86_64)$' -or $pythonVersion -lt [Version]'3.12.0') { throw 'Python must be x64 CPython 3.12 or newer.' }
    $pythonSha = Get-Sha256 $python
    $cacheRoot = Join-Path $repository 'Temp\CodeQualityMetrics'
    Assert-OrdinaryDirectory (Join-Path $repository 'Temp') $repository
    Assert-OrdinaryDirectory $cacheRoot $repository
    New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null
    Assert-OrdinaryDirectory $cacheRoot $repository
    $sourceStage = New-ImmutableScbCheckSource $authoritative.SourceRepository $gitLinkSha $cacheRoot $repository
    $lock = $sourceStage.Lock
    $source = $sourceStage.Source
    $lockSha = Get-Sha256 $lock
    $keySource = "$($probe.implementation)|$($probe.version)|$($probe.arch)|$pythonSha|$lockSha"
    $key = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($keySource))).ToLowerInvariant()
    $environment = Join-Path $cacheRoot $key
    Assert-OrdinaryDirectory $environment $repository
    $mutex = [Threading.Mutex]::new($false, "Local\BrokenEngine.CodeQualityMetrics.$key")
    try {
        if (-not $mutex.WaitOne([TimeSpan]::FromMinutes(10))) { throw 'Timed out waiting for bootstrap mutex.' }
        $valid = Test-BootstrapEnvironment $environment $key $probe.exe $lockSha $source
        if (-not $valid) {
            if (Test-Path -LiteralPath $environment) {
                Remove-BootstrapDirectory $environment $key $cacheRoot $repository
            }
            New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null
            Assert-OrdinaryDirectory $cacheRoot $repository
            do {
                $stageLeaf = "s-$($key.Substring(0, 8))-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
                $staging = Join-Path $cacheRoot $stageLeaf
            } while (Test-Path -LiteralPath $staging)
            try {
                & $python -m venv $staging
                if ($LASTEXITCODE -ne 0) { throw 'Virtual environment creation failed.' }
                $venvPython = Join-Path $staging 'Scripts\python.exe'
                $pipOutput = @(& $venvPython -m pip install --require-hashes --only-binary=:all: -r $lock 2>&1)
                $pipExitCode = $LASTEXITCODE
                if ($pipExitCode -ne 0) {
                    foreach ($line in $pipOutput) { Write-Diagnostic "$line" }
                    throw 'Locked dependency installation failed.'
                }
                $sg = Join-Path $staging 'Scripts\sg.exe'
                $identityText = & $venvPython -c 'import json, os, subprocess, sys; sys.path.insert(0, sys.argv[1]); import scb_check, scb_check.pipeline; sg=os.path.join(os.path.dirname(sys.executable), "sg.exe"); run=subprocess.run([sg, "--version"], capture_output=True, text=True); print(json.dumps({"python":os.path.abspath(sys.executable),"prefix":os.path.abspath(sys.prefix),"module":os.path.abspath(scb_check.__file__),"sg":os.path.abspath(sg),"sgExitCode":run.returncode,"sgVersion":run.stdout.strip()}))' $source
                if ($LASTEXITCODE -ne 0) { throw 'Analyzer import validation failed.' }
                $identity = $identityText | ConvertFrom-Json
                $expectedModuleRoot = [IO.Path]::GetFullPath((Join-Path $source 'scb_check')) + [IO.Path]::DirectorySeparatorChar
                if ($identity.python -ne [IO.Path]::GetFullPath($venvPython) -or $identity.prefix -ne [IO.Path]::GetFullPath($staging) -or -not $identity.module.StartsWith($expectedModuleRoot, [StringComparison]::OrdinalIgnoreCase) -or $identity.sg -ne [IO.Path]::GetFullPath($sg) -or $identity.sgExitCode -ne 0) { throw 'Analyzer runtime identity validation failed.' }
                $complete = Join-Path $staging 'complete.json'
                [IO.File]::WriteAllText($complete, (@{ key = $key; python = $probe.exe; lockSha256 = $lockSha; venvPythonSha256 = Get-Sha256 $venvPython; sgSha256 = Get-Sha256 $sg; sgVersion = $identity.sgVersion } | ConvertTo-Json -Compress), [Text.UTF8Encoding]::new($false))
                if (-not (Test-BootstrapEnvironment $staging $key $probe.exe $lockSha $source)) { throw 'Staged bootstrap environment failed operational validation.' }
                [IO.Directory]::Move($staging, $environment)
            } finally {
                if (Test-Path -LiteralPath $staging) { Remove-BootstrapDirectory $staging $stageLeaf $cacheRoot $repository }
            }
        }
    } finally {
        if ($mutex) { try { $mutex.ReleaseMutex() } catch {} ; $mutex.Dispose() }
    }
    $request = [ordered]@{ mode = $Mode; repositoryRoot = $repository; profile = $Profile; captureRoot = $environment; analyzerSource = Get-CanonicalPath $sourceStage.Source; tool = [ordered]@{ adapterVersion = '4'; lockSha256 = $lockSha; python = [ordered]@{ implementation = $probe.implementation; version = $probe.version; architecture = $probe.arch; executableSha256 = $pythonSha }; disableSg = $true } }
    if ($Mode -eq 'Snapshot') { $request.target = $Target; $request.scope = $Scope } else { $request.targetManifest = [IO.Path]::GetFullPath($TargetManifest); $request.baseline = $Baseline }
    $requestPath = Join-Path $environment ("request-" + [guid]::NewGuid().ToString('N') + '.json')
    $requestFailure = $null
    $requestTargetFailure = $null
    try {
        [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Compress -Depth 5), [Text.UTF8Encoding]::new($false))
        $venvPython = Join-Path $environment 'Scripts\python.exe'
        $analyzer = Join-Path $PSScriptRoot 'Analyze-CodeQualityMetrics.py'
        $processStart = [Diagnostics.ProcessStartInfo]::new()
        $processStart.FileName = $venvPython
        $processStart.UseShellExecute = $false
        $processStart.RedirectStandardOutput = $true
        $processStart.RedirectStandardError = $true
        $processStart.Environment['PYTHONIOENCODING'] = 'utf-8'
        $processStart.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
        $processStart.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
        [void]$processStart.ArgumentList.Add($analyzer)
        [void]$processStart.ArgumentList.Add('--request')
        [void]$processStart.ArgumentList.Add($requestPath)
        $analyzerProcess = [Diagnostics.Process]::new()
        try {
            $analyzerProcess.StartInfo = $processStart
            if (-not $analyzerProcess.Start()) { throw 'Metrics analyzer failed to start.' }
            $stdoutTask = $analyzerProcess.StandardOutput.ReadToEndAsync()
            $stderrTask = $analyzerProcess.StandardError.ReadToEndAsync()
            $analyzerProcess.WaitForExit()
            $payload = $stdoutTask.GetAwaiter().GetResult()
            $diagnostics = $stderrTask.GetAwaiter().GetResult()
            if ($analyzerProcess.ExitCode -ne 0) {
                $requestTargetFailure = Get-TargetFailureDiagnostic $diagnostics
                if ($null -eq $requestTargetFailure) {
                    $classificationDiagnostic = 'CodeQualityMetrics: target is not classified as C++ for BrokenEngineExtended: '
                    if ($diagnostics.TrimEnd("`r", "`n").StartsWith($classificationDiagnostic, [StringComparison]::Ordinal) -and @($diagnostics -split "`r?`n" | Where-Object { $_ }).Count -eq 1) {
                        throw "target is not classified as C++ for BrokenEngineExtended: $($diagnostics.TrimEnd("`r", "`n").Substring($classificationDiagnostic.Length))"
                    }
                    foreach ($line in $diagnostics -split "`r?`n") { if ($line) { Write-Diagnostic $line } }
                    throw 'Metrics analyzer failed.'
                }
            }
        } finally { $analyzerProcess.Dispose() }
        $pendingText = $payload.TrimEnd("`r", "`n") + "`n"
        if ($OutputPath) { $pendingOutputPath = [IO.Path]::GetFullPath($OutputPath) }
    }
    catch {
        $requestFailure = $_.Exception.Message
    }
    finally {
        try {
            if (Test-Path -LiteralPath $requestPath) {
                Remove-Item -LiteralPath $requestPath -Force -ErrorAction Stop
                if (Test-Path -LiteralPath $requestPath) { throw 'Request cleanup left the invocation-owned request file behind.' }
            }
        }
        catch {
            $cleanupFailure = "Metrics request cleanup failed: $($_.Exception.Message)"
            if ($null -eq $requestFailure) { $requestFailure = $cleanupFailure }
            else { $requestFailure = "$requestFailure`n$cleanupFailure" }
        }
    }
    if ($null -ne $requestFailure) { throw $requestFailure }
    if ($null -ne $requestTargetFailure) { $targetFailure = $requestTargetFailure }
}
catch {
    $failure = $_.Exception.Message
}
finally {
    if ($null -ne $sourceStage -and $null -ne $cacheRoot -and $null -ne $repository) {
        try {
            if (Test-Path -LiteralPath $sourceStage.Stage) {
                Remove-BootstrapDirectory $sourceStage.Stage $sourceStage.Leaf $cacheRoot $repository
                if (Test-Path -LiteralPath $sourceStage.Stage) { throw 'Cleanup left the immutable scb-check source stage behind.' }
            }
        }
        catch {
            $cleanupFailure = "Immutable scb-check source cleanup failed: $($_.Exception.Message)"
            if ($null -eq $failure) { $failure = $cleanupFailure }
            else { $failure = "$failure`n$cleanupFailure" }
        }
    }
}
if ($null -ne $failure) { Fail $failure }
if ($null -ne $targetFailure) { [Console]::Error.WriteLine("CodeQualityMetrics: $targetFailure"); exit 2 }
try {
    if ($pendingOutputPath) {
        [IO.File]::WriteAllText($pendingOutputPath, $pendingText, [Text.UTF8Encoding]::new($false))
        if ([IO.File]::ReadAllText($pendingOutputPath, [Text.UTF8Encoding]::new($false)) -ne $pendingText) { throw 'OutputPath bytes did not persist identically.' }
    }
    [Console]::Out.Write($pendingText)
}
catch { Fail $_.Exception.Message }
