[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $RepositoryRoot,
	[int] $WaitSeconds = 660
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'WorktreeCliSessionExclusion.psm1') -Force

if (-not [System.IO.Path]::IsPathRooted($RepositoryRoot)) { throw 'RepositoryRoot must be absolute.' }
$root = Get-AgentCanonicalPath $RepositoryRoot
$topLevel = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $root, 'rev-parse', '--show-toplevel'))[0].Trim())
if (-not $topLevel.Equals($root, [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot is not the repository root: '$root'." }
$gitDirectory = Get-Item -LiteralPath (Join-Path $root '.git') -Force -ErrorAction Stop
if (-not $gitDirectory.PSIsContainer -or ($gitDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "RepositoryRoot is not the primary checkout: '$root'." }
$commonDir = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $root, 'rev-parse', '--path-format=absolute', '--git-common-dir'))[0].Trim())
if (-not $commonDir.Equals((Get-AgentCanonicalPath $gitDirectory.FullName), [StringComparison]::OrdinalIgnoreCase)) { throw "RepositoryRoot is not the primary checkout: '$root'." }

$worktreeCliOutput = Join-Path $root 'Tools\WorktreeCli\Platforms\VisualStudio2026\Output'
$agentHarnessOutput = Join-Path $root 'Tools\AgentHarness\Platforms\VisualStudio2026\Output'
$thirdPartyOutput = Join-Path $root 'ThirdParty\Prebuilts\Platforms\VisualStudio2026\Output'
foreach ($output in @($worktreeCliOutput, $agentHarnessOutput, $thirdPartyOutput)) {
	$outputItem = Get-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
	if ($null -ne $outputItem -and (-not $outputItem.PSIsContainer -or ($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint))) { throw "Primary shared Output must be an ordinary directory: '$output'." }
}
$worktreeCli = Join-Path $worktreeCliOutput 'WorktreeCli.exe'
$agentHarness = Join-Path $agentHarnessOutput 'AgentHarness.exe'
$stampPath = Join-Path $worktreeCliOutput 'AgentToolsSourceStamp.txt'
$capabilityScript = Join-Path $PSScriptRoot 'Test-AgentToolsCapabilities.ps1'
function Get-AgentToolsSourceStamp {
	# The stamp records built-source provenance only; a rev-parse failure must never block the
	# build, so on failure the stamp is simply left unwritten.
	try { return (@(Invoke-AgentGit @('-C', $root, 'rev-parse', 'HEAD:Tools/WorktreeCli', 'HEAD:Tools/AgentHarness', 'HEAD:Tools/ToolCommon')) -join "`n") }
	catch { return $null }
}

# A transient operation claim excludes AgentTools promotion from swapping the shared WorktreeCli and
# AgentHarness executables while this bootstrap consumes them, on both the dirty-skip and build
# paths. Registered before the dirty check because that path already runs the canonical executables;
# released in the finally covering both paths.
$claimOwner = [guid]::NewGuid().ToString()
Register-WorktreeCliSession -RepositoryRoot $root -Owner $claimOwner -Label 'agenttools bootstrap' -Worktree $root -WaitSeconds $WaitSeconds | Out-Null
try {
	# Never compile uncertified working-tree bytes into the shared binaries: if the primary has
	# uncommitted Tools/ThirdParty changes, skip all builds and start on the existing binaries.
	$dirty = @(Invoke-AgentGit @('-C', $root, 'status', '--porcelain', '--untracked-files=all', '--', 'Tools', 'ThirdParty'))
	if ($dirty.Count -ne 0) {
		Write-Warning "Primary has uncommitted changes under Tools/ or ThirdParty/, so shared AgentTools and ThirdParty binaries were NOT rebuilt: $($dirty -join '; ')."
		if (-not (Test-Path -LiteralPath $worktreeCli -PathType Leaf) -or -not (Test-Path -LiteralPath $agentHarness -PathType Leaf)) {
			throw "Primary AgentTools executables are missing and uncommitted Tools/ThirdParty changes block a rebuild. Commit or stash those changes, then retry."
		}
		& $capabilityScript -WorktreeCliExecutable $worktreeCli -AgentHarnessExecutable $agentHarness | Out-Null
		Write-Host "Skipped AgentTools bootstrap builds (dirty primary); using existing binaries at '$worktreeCli' and '$agentHarness'."
		return
	}

	$msBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
	if (-not (Test-Path -LiteralPath $msBuild -PathType Leaf)) {
		$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
		if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) { throw "Visual Studio locator is missing: '$vsWhere'." }
		$installPaths = @(& $vsWhere -latest -version '[18.0,19.0)' -products * -requires Microsoft.Component.MSBuild -property installationPath)
		if ($LASTEXITCODE -ne 0 -or $installPaths.Count -ne 1 -or [string]::IsNullOrWhiteSpace($installPaths[0])) { throw 'Unable to locate a Visual Studio installation containing MSBuild.' }
		$msBuild = Join-Path $installPaths[0].Trim() 'MSBuild\Current\Bin\MSBuild.exe'
		if (-not (Test-Path -LiteralPath $msBuild -PathType Leaf)) { throw "MSBuild is missing: '$msBuild'." }
	}

	$worktreeCliSolution = Join-Path $root 'Tools\WorktreeCli\Platforms\VisualStudio2026\WorktreeCli.sln'
	$agentHarnessSolution = Join-Path $root 'Tools\AgentHarness\Platforms\VisualStudio2026\AgentHarness.sln'
	$thirdPartySolution = Join-Path $root 'ThirdParty\Prebuilts\Platforms\VisualStudio2026\ThirdParty.sln'
	foreach ($solution in @($worktreeCliSolution, $agentHarnessSolution, $thirdPartySolution)) {
		if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) { throw "Bootstrap solution is missing: '$solution'." }
	}
	# DataPacker is intentionally kept out of the hard-fail existence check above: its prebuild is
	# best-effort, so a missing solution warns and skips rather than failing session start.
	$dataPackerSolution = Join-Path $root 'DataPacker\Platforms\VisualStudio2026\DataPacker.sln'
	$dataPackerOutput = Join-Path $root 'DataPacker\Platforms\VisualStudio2026\Output'
	$dataPackerExe = Join-Path $dataPackerOutput 'DataPacker.exe'
	$dataPackerStampPath = Join-Path $dataPackerOutput 'DataPackerPrebuildStamp.txt'

	$commonBuildArguments = @('/p:Platform=x64', '/p:EnableClangTidyCodeAnalysis=false', '/p:RunCodeAnalysis=false', '/verbosity:minimal')
	function Invoke-BootstrapBuild([string] $Solution, [string] $Configuration) {
		$exitCode = Invoke-WorktreeCliTrackedProcess -Executable $msBuild -ArgumentList (@($Solution, "/p:Configuration=$Configuration") + $commonBuildArguments) -WorkingDirectory $root
		if ($exitCode -ne 0) { throw "AgentTools bootstrap build failed for '$Solution' with exit code $exitCode. If another live worktree session is holding these executables, wrap up active worktree sessions and retry." }
	}

	# PC-global mutex scoped like the session ledger mutex (Global\ + SID + repo hash) so concurrent
	# always-rebuild bootstraps serialize. The distinct AgentToolsBootstrap suffix keeps ledger
	# transitions unblocked.
	$bootstrapMutexName = "$((Get-WorktreeCliRepositoryIdentity $root).MutexName)_AgentToolsBootstrap"
	$mutex = [Threading.Mutex]::new($false, $bootstrapMutexName)
	$held = $false
	try {
		$deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
		$milliseconds = [Math]::Max(0, [Math]::Min([int]::MaxValue, [Math]::Ceiling(($deadline - [DateTime]::UtcNow).TotalMilliseconds)))
		try { $held = $mutex.WaitOne([int]$milliseconds) } catch [Threading.AbandonedMutexException] { $held = $true }
		if (-not $held) { throw "Timed out after $WaitSeconds seconds waiting for the AgentTools bootstrap mutex. If another live worktree session is holding these executables, wrap up active worktree sessions and retry." }

		Invoke-BootstrapBuild $worktreeCliSolution 'Release'
		Invoke-BootstrapBuild $agentHarnessSolution 'Release'
		& $capabilityScript -WorktreeCliExecutable $worktreeCli -AgentHarnessExecutable $agentHarness | Out-Null
		$builtStamp = Get-AgentToolsSourceStamp
		if ($null -ne $builtStamp) { [IO.File]::WriteAllText($stampPath, $builtStamp + "`n") }
		# Snapshot DataPacker's build inputs before the ThirdParty Release lib it links is (re)built just
		# below, so the post-build re-check covers a landing or user VS build that advances any consumed
		# input during the prebuild. Best-effort like the prebuild: a snapshot failure disables it.
		$dataPackerPreTrees = $null
		try {
			$dataPackerPreTrees = @(Invoke-AgentGit @('-C', $root, 'rev-parse', 'HEAD:DataPacker', 'HEAD:Common', 'HEAD:ThirdParty'))
			$dataPackerPreDirty = @(Invoke-AgentGit @('-C', $root, 'status', '--porcelain', '--untracked-files=all', '--', 'DataPacker', 'Common', 'ThirdParty'))
		}
		catch { Write-Warning "Could not snapshot DataPacker prebuild inputs, so the prebuild was skipped: $($_.Exception.Message)"; $dataPackerPreTrees = $null }
		foreach ($configuration in @('Debug', 'Profile', 'Release')) { Invoke-BootstrapBuild $thirdPartySolution $configuration }
		foreach ($configuration in @('Debug', 'Profile', 'Release')) {
			$library = Join-Path $thirdPartyOutput "ThirdParty.$configuration.lib"
			if (-not (Test-Path -LiteralPath $library -PathType Leaf) -or (Get-Item -LiteralPath $library).Length -eq 0) { throw "Required primary ThirdParty library is missing or empty after bootstrap: '$library'." }
		}
		# Best-effort DataPacker Release prebuild so a new session worktree seeds its Output\DataPacker.exe
		# by verified copy (Build-WorktreeDataPacker.ps1) instead of compiling from scratch. It links the
		# ThirdParty Release lib built just above, and runs inside this mutex so the exe and its stamp stay
		# atomic against peer bootstraps. Unlike the hard-fail AgentTools builds it only warns on failure:
		# the worktree-local fallback reproduces any real failure. The mutex excludes peer bootstraps but
		# not a landing or user VS build, so the stamp is written only when the before and after snapshots
		# (the three DataPacker/Common/ThirdParty tree hashes plus a clean DataPacker/Common/ThirdParty
		# state) are identical. The pre-snapshot is captured above, before the ThirdParty lib build, so it
		# covers every input DataPacker consumes; the pre-mutex Tools/ThirdParty dirty early-return (lines
		# 48-57) plus this scoped gate keep the normal path clean.
		try {
			if ($null -ne $dataPackerPreTrees -and $dataPackerPreDirty.Count -eq 0) {
				Invoke-BootstrapBuild $dataPackerSolution 'Release'
				if (-not (Test-Path -LiteralPath $dataPackerExe -PathType Leaf)) { throw "DataPacker executable is missing after the prebuild: '$dataPackerExe'." }
				$dataPackerBytes = [IO.File]::ReadAllBytes($dataPackerExe)
				$dataPackerHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($dataPackerBytes)).ToLowerInvariant()
				$dataPackerPostTrees = @(Invoke-AgentGit @('-C', $root, 'rev-parse', 'HEAD:DataPacker', 'HEAD:Common', 'HEAD:ThirdParty'))
				$dataPackerPostDirty = @(Invoke-AgentGit @('-C', $root, 'status', '--porcelain', '--untracked-files=all', '--', 'DataPacker', 'Common', 'ThirdParty'))
				if (($dataPackerPreTrees -join "`n") -cne ($dataPackerPostTrees -join "`n") -or $dataPackerPostDirty.Count -ne 0) {
					Write-Warning "Primary DataPacker/Common/ThirdParty changed during the DataPacker prebuild, so the stamp was not written; new worktrees will build DataPacker locally."
				}
				else {
					$dataPackerStamp = ($dataPackerPreTrees + $dataPackerHash + $dataPackerBytes.Length) -join "`n"
					[IO.File]::WriteAllText($dataPackerStampPath, $dataPackerStamp + "`n")
				}
			}
			elseif ($null -ne $dataPackerPreTrees) {
				Write-Warning "Primary has uncommitted changes under DataPacker/, Common/, or ThirdParty/, so the DataPacker Release prebuild was skipped: $($dataPackerPreDirty -join '; ')."
			}
		}
		catch { Write-Warning "DataPacker Release prebuild failed, so new worktrees will build DataPacker locally: $($_.Exception.Message)" }
		Write-Host "Built primary AgentTools and ThirdParty at '$worktreeCliOutput', '$agentHarnessOutput', and '$thirdPartyOutput'."
	}
	finally { if ($held) { $mutex.ReleaseMutex() }; $mutex.Dispose() }
}
finally {
	Unregister-WorktreeCliSession -RepositoryRoot $root -Owner $claimOwner
}
