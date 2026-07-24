[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $Worktree,
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliExecutable,
	[Parameter(Mandatory = $true)]
	[string] $PrimaryCheckout
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'AgentScriptCommon.psm1') -Force

# DataPacker is not symlinked, so each worktree owns its own Output. Rather than compile it from
# scratch at session start, seed the worktree Output by copying the primary bootstrap prebuild
# (Bootstrap-AgentTools.ps1) when a stamp proves the primary exe was built from the exact
# DataPacker/Common/ThirdParty trees this clean worktree holds. The copy runs outside the bootstrap
# mutex, so the copied exe is re-hashed against the stamp to close torn-read races against a
# concurrent primary rebuild or user VS build. Any failure removes partial copies, warns with the
# reason, and falls through to the verbatim local build below.
$outputDirectory = Join-Path $Worktree 'DataPacker\Platforms\VisualStudio2026\Output'
$worktreeExe = Join-Path $outputDirectory 'DataPacker.exe'
$worktreePdb = Join-Path $outputDirectory 'DataPacker.pdb'
$primaryOutput = Join-Path $PrimaryCheckout 'DataPacker\Platforms\VisualStudio2026\Output'
$stampPath = Join-Path $primaryOutput 'DataPackerPrebuildStamp.txt'
$seeded = $false
try {
	if (-not (Test-Path -LiteralPath $stampPath -PathType Leaf)) { throw "No DataPacker prebuild stamp at '$stampPath'." }
	$stampLines = @([IO.File]::ReadAllText($stampPath).Replace("`r", '').Split("`n") | Where-Object { $_.Length -ne 0 })
	if ($stampLines.Count -ne 5) { throw "DataPacker prebuild stamp is malformed: '$stampPath'." }
	$stampTreeHashes = $stampLines[0..2]
	$stampExeHash = $stampLines[3]
	$stampExeLength = [long] $stampLines[4]

	# ThirdParty is included because directly tracked files under it are on DataPacker's include path;
	# the tree-hash check below only catches committed drift, so uncommitted edits need this gate.
	$dirty = @(Invoke-AgentGit @('-C', $Worktree, 'status', '--porcelain', '--untracked-files=all', '--', 'DataPacker', 'Common', 'ThirdParty'))
	if ($dirty.Count -ne 0) { throw "Worktree has uncommitted changes under DataPacker/, Common/, or ThirdParty/: $($dirty -join '; ')." }
	$worktreeTreeHashes = @(Invoke-AgentGit @('-C', $Worktree, 'rev-parse', 'HEAD:DataPacker', 'HEAD:Common', 'HEAD:ThirdParty'))
	if (($worktreeTreeHashes -join "`n") -cne ($stampTreeHashes -join "`n")) { throw 'Worktree DataPacker/Common/ThirdParty trees differ from the primary prebuild stamp.' }

	$primaryExe = Join-Path $primaryOutput 'DataPacker.exe'
	$primaryPdb = Join-Path $primaryOutput 'DataPacker.pdb'
	if (-not (Test-Path -LiteralPath $primaryExe -PathType Leaf)) { throw "Primary DataPacker executable is missing: '$primaryExe'." }
	New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
	Copy-Item -LiteralPath $primaryExe -Destination $worktreeExe -Force
	if (Test-Path -LiteralPath $primaryPdb -PathType Leaf) { Copy-Item -LiteralPath $primaryPdb -Destination $worktreePdb -Force }

	$copiedBytes = [IO.File]::ReadAllBytes($worktreeExe)
	$copiedHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($copiedBytes)).ToLowerInvariant()
	if ($copiedBytes.Length -ne $stampExeLength -or $copiedHash -cne $stampExeHash) { throw 'Copied DataPacker executable does not match the primary prebuild stamp.' }
	$seeded = $true
	Write-Host "Seeded from primary prebuilt DataPacker at '$primaryOutput' for '$Worktree'."
}
catch {
	foreach ($partial in @($worktreeExe, $worktreePdb)) { Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue }
	Write-Warning "Could not seed worktree DataPacker from the primary prebuild, building locally: $($_.Exception.Message)"
}
if ($seeded) { return }

# WorktreeCli serializes the target inside this private worktree, so there is no cross-session
# contention here. A nonzero exit hard-fails session start (new session or reattach); the message
# names the local build, not the shared-executable wrap-up-sessions guidance, which does not apply
# to a private compile.
$solution = Join-Path $Worktree 'DataPacker\Platforms\VisualStudio2026\DataPacker.sln'
& $WorktreeCliExecutable build $solution '/p:Configuration=Release' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
if ($LASTEXITCODE -ne 0) { throw "Worktree DataPacker Release build failed with exit code $LASTEXITCODE for '$Worktree'. Fix the local DataPacker sources so they compile, then retry." }
