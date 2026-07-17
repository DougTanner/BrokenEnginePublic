# Builds session-owned AgentTools candidates: WorktreeCli.exe and AgentHarness.exe
# into the invoking worktree's ignored Temp\AgentToolsCandidate\ tree, validates the
# pair through Test-AgentToolsCapabilities.ps1, and writes a schema-versioned
# candidate receipt binding both executable hashes to the source commit and tool
# tree hashes. Candidate production never writes to a canonical AgentTools Output
# directory or link; the canonical pair identity is snapshotted before the build
# and re-verified after it. Promotion to canonical primary output is a separate
# explicitly gated step owned by /finalize-changes
# (Invoke-AgentToolsPromotion.ps1) and requires this receipt.
#
# Result contract: one broken-engine-agenttools-candidate-result/v1 JSON object on
# stdout. Exit 0 = pass, 2 = deterministic blocker (build or capability failure,
# canonical identity disturbed), 1 = malformed input or internal failure.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot '..\..\..\scripts\AgentScriptCommon.psm1') -Force

$result = [ordered]@{
	schemaVersion = 'broken-engine-agenttools-candidate-result/v1'
	status = 'error'
	code = 'internal.error'
	message = 'Candidate production did not run.'
	receipt = $null
	buildLogs = @()
}

function Complete-Candidate([int] $ExitCode, [string] $Status, [string] $Code, [string] $Message) {
	$result.status = $Status
	$result.code = $Code
	$result.message = $Message
	[Console]::Out.Write(($result | ConvertTo-Json -Depth 100 -Compress))
	exit $ExitCode
}

function Get-FileIdentity([string] $Path) {
	$item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
	if ($null -eq $item -or $item.PSIsContainer) {
		return [ordered]@{ present = $false; bytes = $null; lastWriteUtc = $null }
	}
	return [ordered]@{ present = $true; bytes = $item.Length; lastWriteUtc = $item.LastWriteTimeUtc.ToString('O') }
}

function Test-IdentityEqual($Left, $Right) {
	return $Left.present -eq $Right.present -and $Left.bytes -eq $Right.bytes -and $Left.lastWriteUtc -eq $Right.lastWriteUtc
}

try {
	if (-not [IO.Path]::IsPathRooted($WorktreeRoot)) { throw 'WorktreeRoot must be absolute.' }
	$root = Get-AgentCanonicalPath $WorktreeRoot
	$topLevel = Get-AgentCanonicalPath (@(Invoke-AgentGit @('-C', $root, 'rev-parse', '--show-toplevel'))[0].Trim())
	if (-not $topLevel.Equals($root, [StringComparison]::OrdinalIgnoreCase)) {
		throw "WorktreeRoot is not a repository checkout root: '$root'."
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

	$candidateRoot = Join-Path $root 'Temp\AgentToolsCandidate'
	$executables = [ordered]@{}
	foreach ($entry in $solutions) {
		$outputDirectory = Join-Path $candidateRoot $entry.Name
		$intermediateDirectory = Join-Path $candidateRoot "Build\$($entry.Name)"
		New-Item -ItemType Directory -Force $outputDirectory | Out-Null
		$executable = Join-Path $outputDirectory "$($entry.Name).exe"
		if (Test-Path -LiteralPath $executable) { Remove-Item -LiteralPath $executable -Force -Confirm:$false }
		$buildLog = Join-Path $candidateRoot "build-$($entry.Name).log"
		$result.buildLogs = @($result.buildLogs) + $buildLog
		# Capture the complete MSBuild console stream (including pre-logger engine errors)
		# into the build log so this sidecar's stdout stays exactly one JSON object.
		& $msBuild $entry.Solution '/p:Configuration=Release' '/p:Platform=x64' `
			"/p:OutDir=$outputDirectory\" "/p:IntDir=$intermediateDirectory\" `
			'/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' `
			'/nodeReuse:false' '/verbosity:normal' 2>&1 | Out-File -LiteralPath $buildLog -Encoding utf8
		$exitCode = $LASTEXITCODE
		if ($exitCode -ne 0) {
			Complete-Candidate 2 'blocked' 'candidate.build-failed' "Candidate build failed for '$($entry.Solution)' with exit code $exitCode; see '$buildLog'."
		}
		if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
			Complete-Candidate 2 'blocked' 'candidate.output-missing' "Candidate build for '$($entry.Name)' produced no executable at '$executable'."
		}
		$item = Get-Item -LiteralPath $executable -Force
		if ($item.Length -eq 0 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
			Complete-Candidate 2 'blocked' 'candidate.output-invalid' "Candidate executable must be a nonempty ordinary file: '$executable'."
		}
		$executables[$entry.Name] = [ordered]@{
			path = $executable
			sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
			bytes = $item.Length
		}
	}

	try {
		& $capabilityScript -WorktreeCliExecutable $executables['WorktreeCli'].path -AgentHarnessExecutable $executables['AgentHarness'].path | Out-Null
	}
	catch {
		Complete-Candidate 2 'blocked' 'candidate.capability-failed' "Candidate capability check failed: $($_.Exception.Message)"
	}

	$canonicalAfter = [ordered]@{
		worktreeCli = Get-FileIdentity $canonicalWorktreeCli
		agentHarness = Get-FileIdentity $canonicalAgentHarness
	}
	if (-not (Test-IdentityEqual $canonicalBefore.worktreeCli $canonicalAfter.worktreeCli) -or
		-not (Test-IdentityEqual $canonicalBefore.agentHarness $canonicalAfter.agentHarness)) {
		Complete-Candidate 2 'blocked' 'candidate.canonical-disturbed' 'Canonical AgentTools output identity changed during candidate production.'
	}

	$sourceCommit = (@(Invoke-AgentGit @('-C', $root, 'rev-parse', 'HEAD'))[0]).Trim()
	$treeHashes = @(Invoke-AgentGit @('-C', $root, 'rev-parse', 'HEAD:Tools/WorktreeCli', 'HEAD:Tools/AgentHarness', 'HEAD:Tools/ToolCommon')) | ForEach-Object { $_.Trim() }
	if ($treeHashes.Count -ne 3) { throw 'Unable to resolve AgentTools source tree hashes.' }
	$dirtyToolPaths = @(Invoke-AgentGit @('-C', $root, 'status', '--porcelain', '--', 'Tools/WorktreeCli', 'Tools/AgentHarness', 'Tools/ToolCommon')) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

	$receiptValue = [ordered]@{
		schemaVersion = 'broken-engine-agenttools-candidate/v1'
		createdAt = [DateTime]::UtcNow.ToString('O')
		worktree = $root
		sourceCommit = $sourceCommit
		toolTreeHashes = [ordered]@{
			worktreeCli = $treeHashes[0]
			agentHarness = $treeHashes[1]
			toolCommon = $treeHashes[2]
		}
		dirtyToolPaths = (@($dirtyToolPaths).Count -gt 0)
		msBuild = $msBuild
		executables = $executables
		canonical = [ordered]@{
			worktreeCli = $canonicalAfter.worktreeCli
			agentHarness = $canonicalAfter.agentHarness
			unchanged = $true
		}
		capabilityCheck = 'pass'
	}
	$receiptPath = Join-Path $candidateRoot 'candidate-receipt.json'
	$receiptJson = $receiptValue | ConvertTo-Json -Depth 100
	[IO.File]::WriteAllText($receiptPath, $receiptJson, [Text.UTF8Encoding]::new($false))
	$receiptItem = Get-Item -LiteralPath $receiptPath -Force
	$result.receipt = [ordered]@{
		path = $receiptPath
		sha256 = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
		bytes = $receiptItem.Length
		sourceCommit = $sourceCommit
		dirtyToolPaths = $receiptValue.dirtyToolPaths
		worktreeCliSha256 = $executables['WorktreeCli'].sha256
		agentHarnessSha256 = $executables['AgentHarness'].sha256
	}
	Complete-Candidate 0 'pass' 'ok' 'AgentTools candidate pair was built and capability-checked without touching canonical output.'
}
catch {
	Complete-Candidate 1 'error' 'candidate.failed' $_.Exception.Message
}
