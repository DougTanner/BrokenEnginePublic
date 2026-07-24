# Builds checkout-owned AgentTools candidates without writing through canonical
# Output. A checkout-local, zero-wait producer lock serializes this source
# snapshot/build/verification transaction independently from every other checkout.
#
# Result contract: one broken-engine-agenttools-candidate-result/v1 JSON object on
# stdout. Exit 0 = pass, 2 = deterministic blocker, 1 = malformed input, OS, or
# internal failure. The receipt contract is broken-engine-agenttools-candidate/v2.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\AgentScriptCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\FinalizeWorkflowCommon.psm1') -Force

$result = [ordered]@{
	schemaVersion = 'broken-engine-agenttools-candidate-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Candidate production did not run.'
	receipt = $null
	buildLogs = @()
}
$exitCode = 1
$producerLock = $null

function New-CandidateFailure([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$exception = [InvalidOperationException]::new($Message)
	$exception.Data['CandidateFailure'] = $true
	$exception.Data['ExitCode'] = $ExitCode
	$exception.Data['Status'] = $Status
	$exception.Data['Code'] = $Code
	return $exception
}

function Throw-CandidateBlocker([string] $Code, [string] $Message) {
	throw (New-CandidateFailure 2 'blocked' $Code $Message)
}

function Get-Sha256([string] $Path) {
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TextSha256([string] $Value) {
	$bytes = [Text.UTF8Encoding]::new($false).GetBytes($Value)
	return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}

function Get-FileIdentity([string] $Path) {
	$item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
	if ($null -eq $item -or $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		return [ordered]@{ path = $Path; present = $false; bytes = $null; sha256 = $null; lastWriteUtc = $null }
	}
	return [ordered]@{
		path = $Path
		present = $true
		bytes = $item.Length
		sha256 = Get-Sha256 $Path
		lastWriteUtc = $item.LastWriteTimeUtc.ToString('O')
	}
}

function Test-IdentityEqual($Left, $Right) {
	return $Left.present -eq $Right.present -and $Left.bytes -eq $Right.bytes -and
		$Left.sha256 -ceq $Right.sha256 -and $Left.lastWriteUtc -ceq $Right.lastWriteUtc
}

function Test-CandidatePathContained([string] $Root, [string] $Candidate) {
	$relative = [IO.Path]::GetRelativePath($Root, $Candidate)
	return -not [IO.Path]::IsPathRooted($relative) -and $relative -ne '..' -and
		-not $relative.StartsWith("..$([IO.Path]::DirectorySeparatorChar)", [StringComparison]::Ordinal) -and
		-not $relative.StartsWith("..$([IO.Path]::AltDirectorySeparatorChar)", [StringComparison]::Ordinal)
}

function Assert-CandidatePathPlan([string] $CheckoutRoot, [string] $Path) {
	$fullPath = Get-AgentCanonicalPath $Path
	if (-not (Test-CandidatePathContained $CheckoutRoot $fullPath) -or $fullPath.Equals($CheckoutRoot, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Candidate path must remain beneath the checkout: '$fullPath'."
	}
	$current = $fullPath
	while (-not $current.Equals($CheckoutRoot, [StringComparison]::OrdinalIgnoreCase)) {
		$item = Get-Item -LiteralPath $current -Force -ErrorAction SilentlyContinue
		if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
			throw "Candidate path contains a reparse point: '$current'."
		}
		$current = Split-Path -Parent $current
	}
	return $fullPath
}

function Assert-CandidateDirectory([string] $CheckoutRoot, [string] $CheckoutIdentity, [string] $Path) {
	$fullPath = Assert-CandidatePathPlan $CheckoutRoot $Path
	$item = Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop
	if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		throw "Candidate path must be an ordinary directory: '$fullPath'."
	}
	$resolvedPath = Get-FinalizeExistingWindowsIdentity $fullPath 'Candidate directory'
	if (-not (Test-CandidatePathContained $CheckoutIdentity $resolvedPath) -or $resolvedPath.Equals($CheckoutIdentity, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Candidate directory resolves outside the checkout: '$fullPath' -> '$resolvedPath'."
	}
	return $fullPath
}

function New-CandidateDirectory([string] $CheckoutRoot, [string] $CheckoutIdentity, [string] $Path) {
	$fullPath = Assert-CandidatePathPlan $CheckoutRoot $Path
	New-Item -ItemType Directory -Force $fullPath | Out-Null
	return Assert-CandidateDirectory $CheckoutRoot $CheckoutIdentity $fullPath
}

function Get-SourceSnapshot([string] $Root) {
	$sourceScopes = @(
		'Tools/WorktreeCli',
		'Tools/AgentHarness',
		'Tools/ToolCommon'
	)
	$gitArguments = @('-C', $Root, '-c', 'core.quotePath=false', 'ls-files', '--cached', '--others', '--exclude-standard', '--') + $sourceScopes
	$listed = @(Invoke-AgentGit $gitArguments)
	$pathSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
	foreach ($listedPath in $listed) {
		$relativePath = $listedPath.Replace('\', '/').Trim()
		if ([string]::IsNullOrWhiteSpace($relativePath)) { continue }
		$fullPath = Join-Path $Root ($relativePath.Replace('/', '\'))
		$item = Get-Item -LiteralPath $fullPath -Force -ErrorAction SilentlyContinue
		if ($null -eq $item -or $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
		[void] $pathSet.Add($relativePath)
	}
	# Worktree provisioning exposes this submodule file as an immutable file link;
	# it is still a compiler input whose resolved bytes and clean-filter blob bind
	# the candidate.
	$jsonRelativePath = 'ThirdParty/tinygltf/json.hpp'
	$jsonItem = Get-Item -LiteralPath (Join-Path $Root 'ThirdParty\tinygltf\json.hpp') -Force -ErrorAction SilentlyContinue
	if ($null -eq $jsonItem -or $jsonItem.PSIsContainer) { throw "Required AgentTools compiler input is missing: '$jsonRelativePath'." }
	[void] $pathSet.Add($jsonRelativePath)
	$paths = @($pathSet)
	[Array]::Sort($paths, [StringComparer]::Ordinal)

	$manifest = @()
	$digestLines = [Collections.Generic.List[string]]::new()
	foreach ($relativePath in $paths) {
		$fullPath = Join-Path $Root ($relativePath.Replace('/', '\'))
		$item = Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop
		$stream = [IO.File]::OpenRead($fullPath)
		try { $bytes = $stream.Length }
		finally { $stream.Dispose() }
		$sha256 = Get-Sha256 $fullPath
		$blobOutput = @(Invoke-AgentGit @('-C', $Root, 'hash-object', "--path=$relativePath", '--', $fullPath))
		if ($blobOutput.Count -ne 1 -or $blobOutput[0].Trim() -cnotmatch '^[0-9a-f]{40}$') {
			throw "Unable to compute clean-filter Git blob identity for '$relativePath'."
		}
		$gitBlob = $blobOutput[0].Trim()
		$manifest += [ordered]@{
			path = $relativePath
			bytes = $bytes
			sha256 = $sha256
			gitBlob = $gitBlob
		}
		$digestLines.Add("$relativePath`0$bytes`0$sha256`0$gitBlob")
	}
	return [ordered]@{
		digest = Get-TextSha256 (($digestLines -join "`n") + "`n")
		manifest = $manifest
	}
}

function Get-SourceChange($Before, $After) {
	$beforeByPath = @{}
	foreach ($entry in $Before.manifest) { $beforeByPath[$entry.path] = $entry }
	$afterByPath = @{}
	foreach ($entry in $After.manifest) { $afterByPath[$entry.path] = $entry }
	$allPaths = @($beforeByPath.Keys + $afterByPath.Keys | Sort-Object -Unique)
	foreach ($path in $allPaths) {
		if (-not $beforeByPath.ContainsKey($path)) { return "source membership added '$path'" }
		if (-not $afterByPath.ContainsKey($path)) { return "source membership removed '$path'" }
		$beforeEntry = $beforeByPath[$path]
		$afterEntry = $afterByPath[$path]
		if ($beforeEntry.bytes -ne $afterEntry.bytes -or $beforeEntry.sha256 -cne $afterEntry.sha256) {
			return "source bytes changed '$path'"
		}
		if ($beforeEntry.gitBlob -cne $afterEntry.gitBlob) { return "source clean-filter identity changed '$path'" }
	}
	return $null
}

function Get-CompilerDependencyEvidence([string] $Root, [string[]] $IntermediateDirectories, $BeforeSnapshot) {
	$logs = @()
	$repoInputs = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
	$gitCommonDirectory = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $Root, 'rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
	$primaryRoot = Get-AgentCanonicalPath (Split-Path -Parent $gitCommonDirectory)
	$repoPrefixes = @(
		$Root.TrimEnd('\') + '\'
		$primaryRoot.TrimEnd('\') + '\'
	) | Select-Object -Unique
	$blocker = $null
	$manifestByFullPath = @{}
	foreach ($entry in $BeforeSnapshot.manifest) {
		$fullPath = Get-AgentCanonicalPath (Join-Path $Root ($entry.path.Replace('/', '\')))
		$manifestByFullPath[$fullPath] = $entry.path
		$item = Get-Item -LiteralPath $fullPath -Force -ErrorAction SilentlyContinue
		if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -and $null -ne $item.Target) {
			foreach ($targetValue in @($item.Target)) {
				$targetPath = if ([IO.Path]::IsPathRooted($targetValue)) { $targetValue } else { Join-Path $item.DirectoryName $targetValue }
				$manifestByFullPath[(Get-AgentCanonicalPath $targetPath)] = $entry.path
			}
		}
	}
	foreach ($intermediateDirectory in $IntermediateDirectories) {
		$dependencyLogs = @(Get-ChildItem -LiteralPath $intermediateDirectory -Recurse -File -Filter 'CL.read.*.tlog' -ErrorAction SilentlyContinue)
		if ($dependencyLogs.Count -eq 0) {
			$blocker = [ordered]@{ code = 'candidate.dependency-log-missing'; message = "Compiler dependency log is missing below '$intermediateDirectory'." }
			continue
		}
		foreach ($dependencyLog in $dependencyLogs) {
			$logs += [ordered]@{ path = $dependencyLog.FullName; bytes = $dependencyLog.Length; sha256 = Get-Sha256 $dependencyLog.FullName }
			foreach ($lineValue in [IO.File]::ReadAllLines($dependencyLog.FullName)) {
				$line = $lineValue.Trim()
				if ([string]::IsNullOrWhiteSpace($line)) { continue }
				if ($line.StartsWith('^', [StringComparison]::Ordinal)) { $line = $line.Substring(1) }
				foreach ($candidatePath in $line.Split('|', [StringSplitOptions]::RemoveEmptyEntries)) {
					$trimmed = $candidatePath.Trim().Trim('"')
					if (-not [IO.Path]::IsPathRooted($trimmed)) { continue }
					try { $canonicalPath = Get-AgentCanonicalPath $trimmed } catch { continue }
					$isRepoLocal = $manifestByFullPath.ContainsKey($canonicalPath)
					foreach ($repoPrefix in $repoPrefixes) {
						if ($canonicalPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) { $isRepoLocal = $true; break }
					}
					if ($isRepoLocal) {
						[void] $repoInputs.Add($canonicalPath)
					}
				}
			}
		}
	}

	$inputPaths = @($repoInputs)
	[Array]::Sort($inputPaths, [StringComparer]::OrdinalIgnoreCase)
	$normalizedInputs = @()
	foreach ($inputPath in $inputPaths) {
		if (-not $manifestByFullPath.ContainsKey($inputPath)) {
			if ($null -eq $blocker) {
				$blocker = [ordered]@{ code = 'candidate.compiler-input-unbound'; message = "Repository-local compiler input was absent from the pre-build source manifest: '$inputPath'." }
			}
			continue
		}
		$normalizedInputs += $manifestByFullPath[$inputPath]
	}
	return [ordered]@{ logs = $logs; repoLocalInputs = $normalizedInputs; blocker = $blocker }
}

try {
	if (-not [IO.Path]::IsPathRooted($WorktreeRoot)) { throw 'WorktreeRoot must be absolute.' }
	$root = Get-AgentCanonicalPath $WorktreeRoot
	$rootIdentity = Get-FinalizeExistingWindowsIdentity $root 'WorktreeRoot'
	$topLevel = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $root, 'rev-parse', '--show-toplevel'))[0].Trim())
	if (-not $topLevel.Equals($root, [StringComparison]::OrdinalIgnoreCase)) {
		throw "WorktreeRoot is not a repository checkout root: '$root'."
	}

	$candidateRoot = Join-Path $root 'Temp\AgentToolsCandidate'
	$candidateRoot = New-CandidateDirectory $root $rootIdentity $candidateRoot
	$lockPath = Join-Path $candidateRoot '.producer.lock'
	[void](Assert-CandidatePathPlan $root $lockPath)
	try {
		$producerLock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
	}
	catch [IO.IOException] {
		$nativeCode = $_.Exception.HResult -band 0xFFFF
		if ($nativeCode -in @(32, 33)) {
			Throw-CandidateBlocker 'candidate.concurrent-producer' "Another candidate producer already holds '$lockPath'."
		}
		throw
	}

	$solutions = @(
		[ordered]@{ Name = 'WorktreeCli'; Solution = Join-Path $root 'Tools\WorktreeCli\Platforms\VisualStudio2026\WorktreeCli.sln' },
		[ordered]@{ Name = 'AgentHarness'; Solution = Join-Path $root 'Tools\AgentHarness\Platforms\VisualStudio2026\AgentHarness.sln' }
	)
	foreach ($entry in $solutions) {
		if (-not (Test-Path -LiteralPath $entry.Solution -PathType Leaf)) { throw "AgentTools solution is missing: '$($entry.Solution)'." }
	}
	$capabilityScript = Join-Path $root '.agents\scripts\Test-AgentToolsCapabilities.ps1'
	if (-not (Test-Path -LiteralPath $capabilityScript -PathType Leaf)) { throw "AgentTools capability checker is missing: '$capabilityScript'." }

	$canonicalWorktreeCli = Join-Path $root 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe'
	$canonicalAgentHarness = Join-Path $root 'Tools\AgentHarness\Platforms\VisualStudio2026\Output\AgentHarness.exe'
	$canonicalBefore = [ordered]@{
		worktreeCli = Get-FileIdentity $canonicalWorktreeCli
		agentHarness = Get-FileIdentity $canonicalAgentHarness
	}
	$sourceBefore = Get-SourceSnapshot $root

	$msBuild = $env:BROKEN_ENGINE_MSBUILD_PATH
	if ([string]::IsNullOrWhiteSpace($msBuild)) {
		$msBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
		if (-not (Test-Path -LiteralPath $msBuild -PathType Leaf)) {
			$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
			if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) { throw "Visual Studio locator is missing: '$vsWhere'." }
			$installPaths = @(& $vsWhere -latest -version '[18.0,19.0)' -products * -requires Microsoft.Component.MSBuild -property installationPath)
			if ($LASTEXITCODE -ne 0 -or $installPaths.Count -ne 1 -or [string]::IsNullOrWhiteSpace($installPaths[0])) {
				throw 'Unable to locate a Visual Studio 2026 installation containing MSBuild.'
			}
			$msBuild = Join-Path $installPaths[0].Trim() 'MSBuild\Current\Bin\MSBuild.exe'
		}
	}
	if (-not (Test-Path -LiteralPath $msBuild -PathType Leaf)) { throw "MSBuild is missing: '$msBuild'." }
	$msBuild = (Get-Item -LiteralPath $msBuild -Force).FullName
	$msBuildItem = Get-Item -LiteralPath $msBuild -Force

	$runId = "{0}-{1}" -f [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'), $PID
	$executables = [ordered]@{}
	$intermediateDirectories = @()
	$buildFailure = $null
	foreach ($entry in $solutions) {
		$outputDirectory = Join-Path $candidateRoot "Candidates\$runId\$($entry.Name)"
		$intermediateDirectory = Join-Path $candidateRoot "Build\$runId\$($entry.Name)"
		$intermediateDirectories += $intermediateDirectory
		$outputDirectory = New-CandidateDirectory $root $rootIdentity $outputDirectory
		$intermediateDirectory = New-CandidateDirectory $root $rootIdentity $intermediateDirectory
		$executable = Join-Path $outputDirectory "$($entry.Name).exe"
		$buildLog = Join-Path $candidateRoot "build-$runId-$($entry.Name).log"
		[void](Assert-CandidatePathPlan $root $executable)
		[void](Assert-CandidatePathPlan $root $buildLog)
		$result.buildLogs = @($result.buildLogs) + $buildLog
		& $msBuild $entry.Solution '/p:Configuration=Release' '/p:Platform=x64' `
			"/p:OutDir=$outputDirectory\\" "/p:IntDir=$intermediateDirectory\\" `
			'/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' `
			'/nodeReuse:false' '/verbosity:normal' 2>&1 | Out-File -LiteralPath $buildLog -Encoding utf8
		$buildExitCode = $LASTEXITCODE
		if ($buildExitCode -ne 0) {
			$buildFailure = "Candidate build failed for '$($entry.Solution)' with exit code $buildExitCode; see '$buildLog'."
			break
		}
		if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
			$buildFailure = "Candidate build for '$($entry.Name)' produced no executable at '$executable'."
			break
		}
		$executableItem = Get-Item -LiteralPath $executable -Force
		if ($executableItem.Length -eq 0 -or ($executableItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
			$buildFailure = "Candidate executable must be a nonempty ordinary file: '$executable'."
			break
		}
		$executables[$entry.Name] = [ordered]@{
			path = $executable
			sha256 = Get-Sha256 $executable
			bytes = $executableItem.Length
		}
	}

	$dependencyEvidence = $null
	$capabilityFailure = $null
	if ($null -eq $buildFailure) {
		$dependencyEvidence = Get-CompilerDependencyEvidence $root $intermediateDirectories $sourceBefore
		if ($null -eq $dependencyEvidence.blocker) {
			try {
				& $capabilityScript -WorktreeCliExecutable $executables['WorktreeCli'].path -AgentHarnessExecutable $executables['AgentHarness'].path | Out-Null
			}
			catch {
				$capabilityFailure = "Candidate capability check failed: $($_.Exception.Message)"
			}
		}
	}

	$sourceAfter = Get-SourceSnapshot $root
	$sourceChange = Get-SourceChange $sourceBefore $sourceAfter
	if ($null -ne $sourceChange -or $sourceBefore.digest -cne $sourceAfter.digest) {
		if ($null -eq $sourceChange) { $sourceChange = 'source snapshot digest changed' }
		Throw-CandidateBlocker 'candidate.source-changed' "AgentTools source changed during candidate production: $sourceChange."
	}

	$canonicalAfter = [ordered]@{
		worktreeCli = Get-FileIdentity $canonicalWorktreeCli
		agentHarness = Get-FileIdentity $canonicalAgentHarness
	}
	if (-not (Test-IdentityEqual $canonicalBefore.worktreeCli $canonicalAfter.worktreeCli) -or
		-not (Test-IdentityEqual $canonicalBefore.agentHarness $canonicalAfter.agentHarness)) {
		Throw-CandidateBlocker 'candidate.canonical-disturbed' 'Canonical AgentTools output identity changed during candidate production.'
	}
	if ($null -ne $buildFailure) { Throw-CandidateBlocker 'candidate.build-failed' $buildFailure }
	if ($null -ne $dependencyEvidence.blocker) { Throw-CandidateBlocker $dependencyEvidence.blocker.code $dependencyEvidence.blocker.message }
	if ($null -ne $capabilityFailure) { Throw-CandidateBlocker 'candidate.capability-failed' $capabilityFailure }

	$sourceCommit = (@(Invoke-AgentGit @('-C', $root, 'rev-parse', 'HEAD'))[0]).Trim()
	$gitCommonDirectory = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $root, 'rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
	$capabilityItem = Get-Item -LiteralPath $capabilityScript -Force
	$sourceReceipt = Get-SourceSnapshot $root
	$lateSourceChange = Get-SourceChange $sourceAfter $sourceReceipt
	if ($null -ne $lateSourceChange -or $sourceAfter.digest -cne $sourceReceipt.digest) {
		if ($null -eq $lateSourceChange) { $lateSourceChange = 'source snapshot digest changed before receipt creation' }
		Throw-CandidateBlocker 'candidate.source-changed' "AgentTools source changed during candidate production: $lateSourceChange."
	}
	$sourceAfter = $sourceReceipt
	$canonicalReceipt = [ordered]@{
		worktreeCli = Get-FileIdentity $canonicalWorktreeCli
		agentHarness = Get-FileIdentity $canonicalAgentHarness
	}
	if (-not (Test-IdentityEqual $canonicalBefore.worktreeCli $canonicalReceipt.worktreeCli) -or
		-not (Test-IdentityEqual $canonicalBefore.agentHarness $canonicalReceipt.agentHarness)) {
		Throw-CandidateBlocker 'candidate.canonical-disturbed' 'Canonical AgentTools output identity changed during candidate production.'
	}
	$canonicalAfter = $canonicalReceipt
	$receiptValue = [ordered]@{
		schemaVersion = 'broken-engine-agenttools-candidate/v2'
		createdAt = [DateTime]::UtcNow.ToString('O')
		checkout = [ordered]@{
			root = $root
			gitCommonDirectory = $gitCommonDirectory
			sourceCommit = $sourceCommit
		}
		source = [ordered]@{
			algorithm = 'sorted-path-bytes-sha256-clean-filter-blob/v1'
			before = $sourceBefore
			after = $sourceAfter
			compilerDependencies = $dependencyEvidence
		}
		toolchain = [ordered]@{
			msBuild = [ordered]@{
				path = $msBuild
				bytes = $msBuildItem.Length
				sha256 = Get-Sha256 $msBuild
				fileVersion = $msBuildItem.VersionInfo.FileVersion
				productVersion = $msBuildItem.VersionInfo.ProductVersion
			}
			configuration = 'Release'
			platform = 'x64'
		}
		executables = $executables
		canonical = [ordered]@{
			before = $canonicalBefore
			after = $canonicalAfter
			unchanged = $true
		}
		capability = [ordered]@{
			status = 'pass'
			script = [ordered]@{ path = $capabilityScript; bytes = $capabilityItem.Length; sha256 = Get-Sha256 $capabilityScript }
		}
	}
	$receiptDirectory = Join-Path $candidateRoot 'Receipts'
	$receiptDirectory = New-CandidateDirectory $root $rootIdentity $receiptDirectory
	$receiptPath = Join-Path $receiptDirectory "candidate-$runId.json"
	[void](Assert-CandidatePathPlan $root $receiptPath)
	[IO.File]::WriteAllText($receiptPath, ($receiptValue | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
	$receiptItem = Get-Item -LiteralPath $receiptPath -Force
	$result.receipt = [ordered]@{
		path = $receiptPath
		sha256 = Get-Sha256 $receiptPath
		bytes = $receiptItem.Length
		schemaVersion = $receiptValue.schemaVersion
		sourceCommit = $sourceCommit
		sourceDigest = $sourceAfter.digest
		worktreeCliSha256 = $executables['WorktreeCli'].sha256
		agentHarnessSha256 = $executables['AgentHarness'].sha256
	}
	$result.status = 'pass'
	$result.code = 'ok'
	$result.message = 'AgentTools candidate pair was built from a stable, dependency-complete source snapshot without touching canonical output.'
	$exitCode = 0
}
catch {
	if ($_.Exception.Data.Contains('CandidateFailure')) {
		$result.status = [string] $_.Exception.Data['Status']
		$result.code = [string] $_.Exception.Data['Code']
		$result.message = $_.Exception.Message
		$exitCode = [int] $_.Exception.Data['ExitCode']
	}
	else {
		$result.status = 'error'
		$result.code = 'candidate.failed'
		$result.message = $_.Exception.Message
		$exitCode = 1
	}
}
finally {
	if ($null -ne $producerLock) { $producerLock.Dispose() }
}

[Console]::Out.Write(($result | ConvertTo-Json -Depth 100 -Compress))
exit $exitCode
