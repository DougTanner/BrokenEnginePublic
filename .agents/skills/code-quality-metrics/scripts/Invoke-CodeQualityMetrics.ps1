[CmdletBinding()]
param(
    [string]$Mode,
    [string]$Target,
    [string]$Scope,
    [string]$Targets,
    [string]$Baseline,
    [string]$RepositoryRoot,
    [switch]$Digest,
    [switch]$Phase0Hints,
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:HintLimit = 10
$script:CloneInstanceLimit = 4

function Write-Diagnostic([string]$Message) { [Console]::Error.WriteLine("CodeQualityMetrics: $Message") }
function Fail([string]$Message) { Write-Diagnostic $Message; exit 2 }
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
function Resolve-ProvisionedItem([string]$Path, [string]$Description) {
    # Wrapper worktrees provision ThirdParty/scb-check entries as links into the primary checkout.
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        $item = $item.ResolveLinkTarget($true)
        if ($null -eq $item) { throw "$Description does not resolve to an existing target." }
    }
    return $item
}
function Copy-AnalyzerTree([string]$From, [string]$To) {
    New-Item -ItemType Directory -Path $To -ErrorAction Stop | Out-Null
    foreach ($entry in Get-ChildItem -LiteralPath $From -Force) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Analyzer source contains a reparse entry: '$($entry.FullName)'." }
        if ($entry.PSIsContainer) {
            if ($entry.Name -eq '__pycache__') { continue }
            Copy-AnalyzerTree $entry.FullName (Join-Path $To $entry.Name)
        }
        else { Copy-Item -LiteralPath $entry.FullName -Destination (Join-Path $To $entry.Name) -ErrorAction Stop }
    }
}
function New-ImmutableScbCheckSource([string]$CacheRoot, [string]$Repository) {
    $provisionedLock = (Resolve-ProvisionedItem (Join-Path $Repository 'ThirdParty\scb-check\requirements.lock') 'Provisioned scb-check requirements.lock').FullName
    $provisionedSource = (Resolve-ProvisionedItem (Join-Path $Repository 'ThirdParty\scb-check\src') 'Provisioned scb-check src').FullName
    Assert-OrdinaryFile $provisionedLock 'Provisioned scb-check requirements.lock'
    Assert-OrdinaryPopulatedDirectory $provisionedSource 'Provisioned scb-check src'
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
        $root = Join-Path $stage 'source'
        New-Item -ItemType Directory -Path $root -ErrorAction Stop | Out-Null
        Copy-AnalyzerTree $provisionedSource (Join-Path $root 'src')
        Copy-Item -LiteralPath $provisionedLock -Destination (Join-Path $root 'requirements.lock') -ErrorAction Stop
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
function Test-MetricContractField([object]$Node, [string]$Name) {
    return $null -ne $Node -and $null -ne $Node.PSObject -and ($Node.PSObject.Properties.Name -ccontains $Name)
}
function Confirm-MetricContractRowField([object]$Row, [string[]]$Names, [string]$RowPath) {
    foreach ($name in $Names) {
        if (-not (Test-MetricContractField $Row $name)) { throw "The metrics report does not contain the field the digest reads: $RowPath.$name." }
    }
}
function Get-MetricContractField([object]$Report, [string]$Path) {
    $node = $Report
    foreach ($segment in $Path.Split('.')) {
        if (-not (Test-MetricContractField $node $segment)) { throw "The metrics report does not contain the field the digest reads: $Path." }
        $node = $node.$segment
    }
    return $node
}
function Get-OrderedDigestRow([object[]]$Rows, [scriptblock]$Weight, [scriptblock]$Key) {
    $ordered = [Collections.Generic.List[object]]::new()
    foreach ($row in $Rows) {
        $ordered.Add([pscustomobject]@{ Weight = [double](& $Weight $row); Key = [string](& $Key $row); Value = $row })
    }
    $ordered.Sort([Comparison[object]] {
        param($left, $right)
        if ($left.Weight -ne $right.Weight) { return $right.Weight.CompareTo($left.Weight) }
        return [string]::CompareOrdinal($left.Key, $right.Key)
    })
    $values = [Collections.Generic.List[object]]::new()
    foreach ($entry in $ordered) { $values.Add($entry.Value) }
    return , $values
}
function New-HintCategory([object]$Ordered, [int]$Total) {
    $items = [Collections.Generic.List[object]]::new()
    foreach ($row in $Ordered) {
        if ($items.Count -ge $script:HintLimit) { break }
        $items.Add($row)
    }
    return [ordered]@{ items = @($items); total = $Total; emitted = $items.Count }
}
function Get-Phase0Hint([object]$Report) {
    $targetPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $identities = @(Get-MetricContractField $Report 'current.targetManifest')
    for ($index = 0; $index -lt $identities.Count; ++$index) {
        Confirm-MetricContractRowField $identities[$index] @('path') "current.targetManifest[$index]"
        [void]$targetPaths.Add([string]$identities[$index].path)
    }

    $buckets = @(Get-MetricContractField $Report 'current.targetOutliers')
    $outlierTotal = 0
    $outlierEmitted = 0
    for ($index = 0; $index -lt $buckets.Count; ++$index) {
        Confirm-MetricContractRowField $buckets[$index] @('items', 'totalCount', 'truncatedCount') "current.targetOutliers[$index]"
        $outlierTotal += [int]$buckets[$index].totalCount
        $outlierEmitted += @($buckets[$index].items).Count
    }
    # The report already truncated and ordered each bucket; re-sorting it would invent an ordering.
    $outliers = [ordered]@{ items = @($buckets); total = $outlierTotal; emitted = $outlierEmitted }

    $cloneCandidates = [Collections.Generic.List[object]]::new()
    $groups = @(Get-MetricContractField $Report 'current.cloneGroups')
    for ($groupIndex = 0; $groupIndex -lt $groups.Count; ++$groupIndex) {
        $group = $groups[$groupIndex]
        Confirm-MetricContractRowField $group @('instances') "current.cloneGroups[$groupIndex]"
        $instances = @($group.instances)
        $targetInstances = [Collections.Generic.List[object]]::new()
        $externalInstances = [Collections.Generic.List[object]]::new()
        $targetSloc = 0.0
        for ($instanceIndex = 0; $instanceIndex -lt $instances.Count; ++$instanceIndex) {
            $instance = $instances[$instanceIndex]
            $instancePath = "current.cloneGroups[$groupIndex].instances[$instanceIndex]"
            Confirm-MetricContractRowField $instance @('path') $instancePath
            if (-not $targetPaths.Contains([string]$instance.path)) { $externalInstances.Add($instance); continue }
            Confirm-MetricContractRowField $instance @('sloc') $instancePath
            $targetSloc += [double]$instance.sloc
            $targetInstances.Add($instance)
        }
        if ($targetInstances.Count -eq 0) { continue }
        Confirm-MetricContractRowField $group @('groupHash') "current.cloneGroups[$groupIndex]"
        $cloneCandidates.Add([pscustomobject]@{
            GroupHash = [string]$group.groupHash
            TargetSloc = $targetSloc
            Digest = [ordered]@{
                groupHash = [string]$group.groupHash
                targetInstances = @($targetInstances | Select-Object -First $script:CloneInstanceLimit)
                externalInstances = @($externalInstances | Select-Object -First $script:CloneInstanceLimit)
            }
        })
    }
    $cloneOrdered = Get-OrderedDigestRow @($cloneCandidates) { param($row) $row.TargetSloc } { param($row) $row.GroupHash }
    $cloneDigests = [Collections.Generic.List[object]]::new()
    foreach ($candidate in $cloneOrdered) { $cloneDigests.Add($candidate.Digest) }
    $cloneGroups = New-HintCategory $cloneDigests $cloneCandidates.Count

    $functionRows = [Collections.Generic.List[object]]::new()
    $allFunctions = @(Get-MetricContractField $Report 'current.highComplexityFunctions')
    for ($index = 0; $index -lt $allFunctions.Count; ++$index) {
        $functionPath = "current.highComplexityFunctions[$index]"
        Confirm-MetricContractRowField $allFunctions[$index] @('path') $functionPath
        if (-not $targetPaths.Contains([string]$allFunctions[$index].path)) { continue }
        Confirm-MetricContractRowField $allFunctions[$index] @('mass', 'owner', 'name', 'signature') $functionPath
        $functionRows.Add($allFunctions[$index])
    }
    $functions = New-HintCategory (Get-OrderedDigestRow @($functionRows) { param($row) [double]$row.mass } { param($row) "$($row.owner)/$($row.name)/$($row.signature)" }) $functionRows.Count

    $skipRows = [Collections.Generic.List[object]]::new()
    $allSkips = @(Get-MetricContractField $Report 'current.skips')
    for ($index = 0; $index -lt $allSkips.Count; ++$index) {
        Confirm-MetricContractRowField $allSkips[$index] @('path') "current.skips[$index]"
        if ($targetPaths.Contains([string]$allSkips[$index].path)) { $skipRows.Add($allSkips[$index]) }
    }
    $skips = New-HintCategory (Get-OrderedDigestRow @($skipRows) { param($row) 0.0 } { param($row) [string]$row.path }) $skipRows.Count

    return [ordered]@{
        targetOutliers = $outliers
        cloneGroups = $cloneGroups
        highComplexityFunctions = $functions
        skips = $skips
    }
}
function New-CodeQualityDigest([object]$Report, [string]$ReportMode, [bool]$IncludeHints) {
    # The digest only selects, orders, and counts fields the report already contains.
    $digest = [ordered]@{
        schemaVersion = 'broken-engine-code-quality-evidence/v2'
        profile = Get-MetricContractField $Report 'profile'
        targetSelection = Get-MetricContractField $Report 'targetSelection'
        coverage = $null
        comparison = $null
    }
    [void](Get-MetricContractField $Report 'current')
    if ($ReportMode -ceq 'Compare') {
        $digest.comparison = Get-MetricContractField $Report 'comparison'
        $digest.coverage = Get-MetricContractField $Report 'comparison.coverage'
    }
    else {
        # The report has no top-level coverage; Snapshot coverage is the current CaptureView counts.
        $digest.coverage = [ordered]@{ corpusCounts = Get-MetricContractField $Report 'current.corpusCounts'; targetCounts = Get-MetricContractField $Report 'current.targetCounts' }
    }
    if ($IncludeHints) { $digest.hints = Get-Phase0Hint $Report }
    return ($digest | ConvertTo-Json -Depth 64 -Compress)
}

$failure = $null
$analyzerDiagnostics = $null
$pendingText = $null
$pendingOutputPath = $null
$sourceStage = $null
$cacheRoot = $null
$repository = $null
try {
    if ($Mode -notin @('Snapshot', 'Compare')) { throw 'Mode must be Snapshot or Compare.' }
    if (-not $RepositoryRoot) { throw 'RepositoryRoot must be an existing absolute directory.' }
    if ($Scope -and $Scope -notin @('Exact', 'Directory', 'Recursive')) { throw 'Scope must be Exact, Directory, or Recursive.' }
    if ($Baseline -and $Baseline -notmatch '^[0-9a-f]{40}$') { throw 'Baseline must be a 40-character lowercase hexadecimal commit SHA.' }
    if ($Mode -eq 'Snapshot' -and ((-not $Target) -or (-not $Scope) -or $Targets -or $Baseline)) { throw 'Snapshot requires only Target and Scope.' }
    if ($Mode -eq 'Compare' -and ((-not $Targets) -or (-not $Baseline) -or $Target -or $Scope)) { throw 'Compare requires only Targets and Baseline.' }
    $repository = Get-CanonicalPath $RepositoryRoot
    if (-not [IO.Path]::IsPathFullyQualified($RepositoryRoot) -or -not (Test-Path -LiteralPath $repository -PathType Container)) { throw 'RepositoryRoot must be an existing absolute directory.' }
    $rootItem = Get-Item -LiteralPath $repository -Force
    if ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'RepositoryRoot must not be a reparse point.' }
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
    $sourceStage = New-ImmutableScbCheckSource $cacheRoot $repository
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
    $request = [ordered]@{ mode = $Mode; repositoryRoot = $repository; captureRoot = $environment; analyzerSource = Get-CanonicalPath $sourceStage.Source; tool = [ordered]@{ adapterVersion = '5'; lockSha256 = $lockSha; python = [ordered]@{ implementation = $probe.implementation; version = $probe.version; architecture = $probe.arch; executableSha256 = $pythonSha }; disableSg = $true } }
    if ($Mode -eq 'Snapshot') { $request.target = $Target; $request.scope = $Scope } else { $request.targets = [IO.Path]::GetFullPath($Targets); $request.baseline = $Baseline }
    $requestPath = Join-Path $environment ("request-" + [guid]::NewGuid().ToString('N') + '.json')
    $requestFailure = $null
    $requestDiagnostics = $null
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
                # The analyzer owns its own diagnostic wording, including the two structured target-failure lines.
                $requestDiagnostics = if ($diagnostics.Trim()) { $diagnostics } else { "CodeQualityMetrics: Metrics analyzer failed with exit $($analyzerProcess.ExitCode).`n" }
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
    if ($null -ne $requestDiagnostics) { $analyzerDiagnostics = $requestDiagnostics }
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
if ($null -ne $analyzerDiagnostics) { [Console]::Error.Write($analyzerDiagnostics); exit 2 }
try {
    if ($pendingOutputPath) {
        [IO.File]::WriteAllText($pendingOutputPath, $pendingText, [Text.UTF8Encoding]::new($false))
    }
    $emitText = $pendingText
    if ($Digest -or $Phase0Hints) { $emitText = (New-CodeQualityDigest ($pendingText | ConvertFrom-Json -Depth 64) $Mode $Phase0Hints.IsPresent) + "`n" }
    [Console]::Out.Write($emitText)
}
catch { Fail $_.Exception.Message }
