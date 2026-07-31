# Deterministic fixtures for Get-SessionChangeInventory.ps1 against throwaway Git repositories
# under the operating system temp directory. Every repository is created and removed by this
# harness; it never points the inventory at a real checkout for a mutating case, and it proves
# the inventory leaves each fixture repository byte-identical. Exit 0 means every case passed.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Failures = [Collections.Generic.List[string]]::new()
$script:Roots = [Collections.Generic.List[string]]::new()
$script:Utf8 = [Text.UTF8Encoding]::new($false)
$script:Inventory = Join-Path $PSScriptRoot 'Get-SessionChangeInventory.ps1'
$script:PwshPath = (Get-Process -Id $PID).Path
$script:GitlinkCommit = '1234567890abcdef1234567890abcdef12345678'

function Assert-True([bool] $Condition, [string] $Name) {
	if (-not $Condition) { $script:Failures.Add($Name); Write-Host "FAIL $Name" } else { Write-Host "pass $Name" }
}

function Assert-Equal($Expected, $Actual, [string] $Name) {
	Assert-True ("$Expected" -ceq "$Actual") "$Name (expected '$Expected', was '$Actual')"
}

function Get-TextSha256([string] $Text) {
	return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($script:Utf8.GetBytes($Text))).ToLowerInvariant()
}

function Get-ByteSha256([byte[]] $Bytes) {
	return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function New-FixtureRoot([string] $Name) {
	$root = Join-Path ([IO.Path]::GetTempPath()) ("inventory-fixture-$Name-" + [Guid]::NewGuid().ToString('n').Substring(0, 8))
	[void] (New-Item -ItemType Directory -Path $root -Force)
	$script:Roots.Add($root)
	Invoke-FixtureGit $root @('init', '--quiet', '--initial-branch=main')
	Invoke-FixtureGit $root @('config', 'user.name', 'Fixture')
	Invoke-FixtureGit $root @('config', 'user.email', 'fixture@example.invalid')
	Invoke-FixtureGit $root @('config', 'core.autocrlf', 'false')
	Invoke-FixtureGit $root @('config', 'commit.gpgsign', 'false')
	return $root
}

function Get-FixtureGitText([string] $Root, [string[]] $Arguments) {
	$output = @(& git -C $Root @Arguments 2>&1)
	if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed in '$Root': $($output -join '; ')" }
	return ($output -join "`n")
}

function Invoke-FixtureGit([string] $Root, [string[]] $Arguments) {
	[void] (Get-FixtureGitText $Root $Arguments)
}

function Set-FixtureText([string] $Root, [string] $RelativePath, [string] $Text) {
	$full = Join-Path $Root ($RelativePath -replace '/', [IO.Path]::DirectorySeparatorChar)
	[void] (New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($full)) -Force)
	[IO.File]::WriteAllBytes($full, $script:Utf8.GetBytes($Text))
}

function Set-FixtureBytes([string] $Root, [string] $RelativePath, [byte[]] $Bytes) {
	$full = Join-Path $Root ($RelativePath -replace '/', [IO.Path]::DirectorySeparatorChar)
	[void] (New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($full)) -Force)
	[IO.File]::WriteAllBytes($full, $Bytes)
}

function Invoke-Inventory([string[]] $Arguments) {
	$start = [Diagnostics.ProcessStartInfo]::new()
	$start.FileName = $script:PwshPath
	$start.WorkingDirectory = [IO.Path]::GetTempPath()
	$start.UseShellExecute = $false
	$start.CreateNoWindow = $true
	$start.RedirectStandardOutput = $true
	$start.RedirectStandardError = $true
	$start.StandardOutputEncoding = $script:Utf8
	$start.StandardErrorEncoding = $script:Utf8
	foreach ($argument in @('-NoProfile', '-File', $script:Inventory) + $Arguments) { [void] $start.ArgumentList.Add($argument) }
	$process = [Diagnostics.Process]::new()
	$process.StartInfo = $start
	if (-not $process.Start()) { throw 'Could not start pwsh for the inventory script.' }
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	$process.WaitForExit()
	$run = [ordered]@{
		ExitCode = $process.ExitCode
		Stdout = $stdoutTask.GetAwaiter().GetResult()
		Stderr = $stderrTask.GetAwaiter().GetResult()
		Json = $null
		ErrorJson = $null
	}
	$process.Dispose()
	if (-not [string]::IsNullOrWhiteSpace($run.Stdout)) { try { $run.Json = $run.Stdout | ConvertFrom-Json -Depth 32 } catch { } }
	if (-not [string]::IsNullOrWhiteSpace($run.Stderr)) { try { $run.ErrorJson = $run.Stderr | ConvertFrom-Json -Depth 32 } catch { } }
	return [pscustomobject] $run
}

function Get-EntryMap($Json) {
	$map = @{}
	foreach ($entry in @($Json.entries)) { $map[$entry.path] = $entry }
	return $map
}

function Assert-CleanRepository([string] $Root, [string] $Expected, [string] $Name) {
	$actual = Get-FixtureGitText $Root @('status', '--porcelain')
	Assert-True ($actual -ceq $Expected) "$Name leaves the repository unchanged"
}

function Get-FixtureTreeSnapshot([string] $Root) {
	# Every file under the root, including the .git directory and ignored files, with its size and
	# content hash: the decisive proof that a run wrote nothing anywhere, not just nothing Git reports.
	$lines = [Collections.Generic.List[string]]::new()
	foreach ($item in @(Get-ChildItem -LiteralPath $Root -Recurse -Force -File)) {
		$hash = Get-ByteSha256 ([IO.File]::ReadAllBytes($item.FullName))
		$lines.Add("$($item.FullName.Substring($Root.Length))|$($item.Length)|$hash")
	}
	$lines.Sort([StringComparer]::Ordinal)
	return ($lines -join "`n")
}

function Invoke-InventoryReadOnly([string] $Root, [string[]] $Arguments, [string] $Name) {
	$before = Get-FixtureTreeSnapshot $Root
	$run = Invoke-Inventory $Arguments
	Assert-True ((Get-FixtureTreeSnapshot $Root) -ceq $before) "$Name writes no file under the repository, including .git and ignored files"
	return $run
}

# --- Repository A: every class rule, every working-tree status, untracked handling ------------

function New-RepositoryA() {
	$root = New-FixtureRoot 'a'
	Set-FixtureText $root 'Engine/Source/Keep.cpp' "1`n2`n3`n4`n5`n"
	Set-FixtureText $root 'Engine/Source/Delete.h' "a`nb`n"
	Set-FixtureText $root 'Engine/Source/Old.cpp' "int Old() { return 1; }`n"
	Set-FixtureText $root 'Engine/Source/ShaderLayouts.h' "layouts`n"
	Set-FixtureText $root 'Engine/Source/ShaderLayoutsBase.h' "layouts base`n"
	Set-FixtureText $root 'Data/Shaders/Common.h' "common`n"
	Set-FixtureText $root 'Data/Shaders/AGENTS.md' "shader docs`n"
	Set-FixtureText $root 'Data/Shaders/Ray.rgen' "rgen`n"
	Set-FixtureText $root 'Data/Shaders/Geo.mesh' "mesh`n"
	Set-FixtureText $root '.agents/skills/demo/SKILL.md' "skill`n"
	Set-FixtureText $root 'Documents/Plans/Area/Thing.md' "plan`n"
	Set-FixtureText $root 'Documents/Plans/AGENTS.md' "plans doc`n"
	Set-FixtureText $root 'Build/App.vcxproj' "<Project/>`n"
	Set-FixtureText $root 'Tools/Run.ps1' "Write-Output 1`n"
	Set-FixtureText $root 'Misc/data.json' "{}`n"
	Set-FixtureBytes $root 'Tools/Blob.bin' ([byte[]] @(0, 1, 2, 3, 0, 255))
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	Set-FixtureText $root 'Engine/Source/Keep.cpp' "1`n2`nthree`n4`n5`n"
	Invoke-FixtureGit $root @('rm', '--quiet', 'Engine/Source/Delete.h')
	Invoke-FixtureGit $root @('mv', 'Engine/Source/Old.cpp', 'Engine/Source/New.cpp')
	foreach ($path in @('Engine/Source/ShaderLayouts.h', 'Engine/Source/ShaderLayoutsBase.h', 'Data/Shaders/Common.h', 'Data/Shaders/AGENTS.md', 'Data/Shaders/Ray.rgen', 'Data/Shaders/Geo.mesh', '.agents/skills/demo/SKILL.md', 'Documents/Plans/Area/Thing.md', 'Documents/Plans/AGENTS.md', 'Build/App.vcxproj', 'Tools/Run.ps1', 'Misc/data.json')) {
		$full = Join-Path $root ($path -replace '/', [IO.Path]::DirectorySeparatorChar)
		[IO.File]::WriteAllBytes($full, $script:Utf8.GetBytes(([IO.File]::ReadAllText($full)) + "changed`n"))
	}
	Set-FixtureBytes $root 'Tools/Blob.bin' ([byte[]] @(0, 9, 9, 3, 0, 254))
	Set-FixtureText $root 'Engine/Source/Untracked.cpp' "u1`nu2`nu3`n"
	Set-FixtureText $root 'Engine/Source/Unlisted.cpp' "unlisted`n"
	return [pscustomobject] @{ Root = $root; Baseline = $baseline }
}

function Test-DefaultInventory($Fixture) {
	$before = Get-FixtureGitText $Fixture.Root @('status', '--porcelain')
	$run = Invoke-InventoryReadOnly $Fixture.Root @('-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-IncludeUntracked', 'Engine/Source/Untracked.cpp') 'default run'
	Assert-Equal 0 $run.ExitCode 'default exit code'
	Assert-True ($null -ne $run.Json) 'default emitted JSON'
	if ($null -eq $run.Json) { return }
	Assert-Equal 'broken-engine-session-change-inventory/v1' $run.Json.schemaVersion 'default schemaVersion'
	Assert-Equal 'pass' $run.Json.status 'default status'
	Assert-Equal $Fixture.Baseline $run.Json.baselineSha 'default baselineSha'
	Assert-True ($null -eq $run.Json.headSha) 'default headSha null for a working-tree head'
	$map = Get-EntryMap $run.Json
	$expected = @{
		'Engine/Source/Keep.cpp' = @('M', 'cpp')
		'Engine/Source/Delete.h' = @('D', 'cpp')
		'Engine/Source/New.cpp' = @('R', 'cpp')
		'Engine/Source/ShaderLayouts.h' = @('M', 'dual-language-header')
		'Engine/Source/ShaderLayoutsBase.h' = @('M', 'dual-language-header')
		'Data/Shaders/Common.h' = @('M', 'glsl')
		'Data/Shaders/Ray.rgen' = @('M', 'glsl')
		'Data/Shaders/Geo.mesh' = @('M', 'glsl')
		'Data/Shaders/AGENTS.md' = @('M', 'doc')
		'.agents/skills/demo/SKILL.md' = @('M', 'skill')
		'Documents/Plans/Area/Thing.md' = @('M', 'plan')
		'Documents/Plans/AGENTS.md' = @('M', 'doc')
		'Build/App.vcxproj' = @('M', 'vcxproj')
		'Tools/Run.ps1' = @('M', 'script')
		'Misc/data.json' = @('M', 'other')
		'Tools/Blob.bin' = @('M', 'binary')
		'Engine/Source/Untracked.cpp' = @('A', 'cpp')
	}
	foreach ($path in $expected.Keys) {
		if (-not $map.ContainsKey($path)) { Assert-True $false "default has an entry for $path"; continue }
		Assert-Equal $expected[$path][0] $map[$path].status "default status of $path"
		Assert-Equal $expected[$path][1] $map[$path].class "default class of $path"
	}
	Assert-True (-not $map.ContainsKey('Engine/Source/Unlisted.cpp')) 'default omits the unlisted untracked path'
	Assert-Equal 17 $run.Json.counts.total 'default counts.total'
	Assert-Equal 1 $run.Json.counts.unlistedUntracked 'default counts.unlistedUntracked'
	Assert-Equal 4 $run.Json.counts.cpp 'default counts.cpp'
	Assert-Equal 2 $run.Json.counts.'dual-language-header' 'default counts.dual-language-header'
	Assert-Equal 3 $run.Json.counts.glsl 'default counts.glsl'
	Assert-Equal 2 $run.Json.counts.doc 'default counts.doc'
	Assert-Equal 1 $run.Json.counts.binary 'default counts.binary'
	Assert-Equal 1 $run.Json.counts.other 'default counts.other'
	Assert-Equal 'Engine/Source/Old.cpp' $map['Engine/Source/New.cpp'].oldPath 'default rename oldPath'
	Assert-True ($null -eq $map['Engine/Source/Keep.cpp'].oldPath) 'default non-rename oldPath is null'
	Assert-Equal (Get-TextSha256 "1`n2`n3`n4`n5`n") $map['Engine/Source/Keep.cpp'].baseline.sha256 'default baseline identity hash'
	Assert-Equal (Get-TextSha256 "1`n2`nthree`n4`n5`n") $map['Engine/Source/Keep.cpp'].current.sha256 'default current identity hash'
	Assert-Equal '100644' $map['Engine/Source/Keep.cpp'].current.mode 'default current identity mode'
	Assert-True ($null -eq $map['Engine/Source/Delete.h'].current) 'default deletion has no current identity'
	Assert-True ($null -eq $map['Engine/Source/Untracked.cpp'].baseline) 'default addition has no baseline identity'
	Assert-Equal (Get-TextSha256 "u1`nu2`nu3`n") $map['Engine/Source/Untracked.cpp'].current.sha256 'default untracked identity hash'
	Assert-Equal (Get-ByteSha256 ([byte[]] @(0, 9, 9, 3, 0, 254))) $map['Tools/Blob.bin'].current.sha256 'default binary identity hash'
	foreach ($name in @('repoCodeReview', 'glslReview', 'codeStyleReview', 'updateAffectedCode', 'updateVcxproj', 'updateClaudeDocs', 'validateSkill', 'planTouched')) {
		Assert-True ($run.Json.triggers.$name -eq $true) "default trigger $name is true"
	}
	Assert-True ($run.Json.truncated -eq $false) 'default truncated is false'
	Assert-Equal 17 $run.Json.truncation.entries.emitted 'default emitted entry count'
	Assert-True ([string]::IsNullOrEmpty($run.Stderr)) 'default writes nothing to stderr'
	Assert-CleanRepository $Fixture.Root $before 'default run'
}

function Test-RegionInventory($Fixture) {
	$run = Invoke-InventoryReadOnly $Fixture.Root @('-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-IncludeUntracked', 'Engine/Source/Untracked.cpp', '-Regions') 'regions run'
	Assert-Equal 0 $run.ExitCode 'regions exit code'
	if ($null -eq $run.Json) { Assert-True $false 'regions emitted JSON'; return }
	$modified = @($run.Json.regions | Where-Object { $_.path -ceq 'Engine/Source/Keep.cpp' })
	Assert-Equal 1 $modified.Count 'regions has one hunk for the modified file'
	if ($modified.Count -eq 1) {
		Assert-Equal 'modified' $modified[0].kind 'regions modified kind'
		Assert-Equal 3 $modified[0].startLine 'regions modified startLine'
		Assert-Equal 3 $modified[0].endLine 'regions modified endLine'
		Assert-Equal 3 $modified[0].oldStartLine 'regions modified oldStartLine'
		Assert-Equal 3 $modified[0].oldEndLine 'regions modified oldEndLine'
		Assert-Equal 1 $modified[0].addedLines 'regions modified addedLines'
		Assert-Equal 1 $modified[0].deletedLines 'regions modified deletedLines'
	}
	$deleted = @($run.Json.regions | Where-Object { $_.path -ceq 'Engine/Source/Delete.h' })
	Assert-Equal 1 $deleted.Count 'regions has one hunk for the deleted file'
	if ($deleted.Count -eq 1) {
		Assert-Equal 'deleted' $deleted[0].kind 'regions deleted kind'
		Assert-True ($null -eq $deleted[0].startLine) 'regions deleted startLine is null'
		Assert-True ($null -eq $deleted[0].endLine) 'regions deleted endLine is null'
		Assert-Equal 1 $deleted[0].oldStartLine 'regions deleted oldStartLine'
		Assert-Equal 2 $deleted[0].oldEndLine 'regions deleted oldEndLine'
		Assert-Equal 0 $deleted[0].addedLines 'regions deleted addedLines'
		Assert-Equal 2 $deleted[0].deletedLines 'regions deleted deletedLines'
	}
	$added = @($run.Json.regions | Where-Object { $_.path -ceq 'Engine/Source/Untracked.cpp' })
	Assert-Equal 1 $added.Count 'regions has one hunk for the listed untracked file'
	if ($added.Count -eq 1) {
		Assert-Equal 'added' $added[0].kind 'regions added kind'
		Assert-Equal 1 $added[0].startLine 'regions added startLine'
		Assert-Equal 3 $added[0].endLine 'regions added endLine'
		Assert-True ($null -eq $added[0].oldStartLine) 'regions added oldStartLine is null'
		Assert-Equal 3 $added[0].addedLines 'regions added addedLines'
	}
	Assert-Equal 17 $run.Json.counts.total 'regions keeps the full counts'
}

function Test-ManifestInventory($Fixture) {
	$run = Invoke-InventoryReadOnly $Fixture.Root @('-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-IncludeUntracked', 'Engine/Source/Untracked.cpp', '-EmitTargetManifest') 'manifest run'
	Assert-Equal 0 $run.ExitCode 'manifest exit code'
	Assert-True ([string]::IsNullOrEmpty($run.Stderr)) 'manifest pass writes nothing to stderr'
	if ($null -eq $run.Json) { Assert-True $false 'manifest emitted JSON'; return }
	Assert-Equal 'broken-engine-code-quality-target-manifest/v1' $run.Json.schemaVersion 'manifest schemaVersion'
	Assert-True (@($run.Json.PSObject.Properties.Name).Count -eq 2) 'manifest has exactly schemaVersion and pairs'
	$pairs = @($run.Json.pairs)
	$keys = @($pairs | ForEach-Object { "$(if ($null -eq $_.baseline) { '' } else { $_.baseline.path })|$(if ($null -eq $_.current) { '' } else { $_.current.path })" })
	$expected = @(
		'|Engine/Source/Untracked.cpp'
		'Engine/Source/Delete.h|'
		'Engine/Source/Keep.cpp|Engine/Source/Keep.cpp'
		'Engine/Source/Old.cpp|Engine/Source/New.cpp'
		'Engine/Source/ShaderLayouts.h|Engine/Source/ShaderLayouts.h'
		'Engine/Source/ShaderLayoutsBase.h|Engine/Source/ShaderLayoutsBase.h'
	)
	Assert-Equal ($expected -join ' ; ') ($keys -join ' ; ') 'manifest pair set and ordinal ordering'
	foreach ($pair in $pairs) {
		foreach ($side in @('baseline', 'current')) {
			if ($null -eq $pair.$side) { continue }
			Assert-True (@($pair.$side.PSObject.Properties.Name | Sort-Object) -join ',' -ceq 'mode,path,sha256') 'manifest identity has exactly path, mode, and sha256'
			Assert-True ($pair.$side.mode -ceq '100644') 'manifest identity uses an ordinary mode'
			Assert-True ($pair.$side.sha256 -cmatch '^[0-9a-f]{64}$') 'manifest identity hash is lowercase SHA-256'
		}
	}
	Assert-True ($run.Stdout.EndsWith("`n")) 'manifest ends with one LF'
	Assert-True (-not $run.Stdout.TrimStart().StartsWith('{"schemaVersion":"broken-engine-session-change-inventory')) 'manifest stdout carries no envelope wrapper'
}

# --- Repository B: cross-class renames ---------------------------------------------------------

function Test-CrossClassRename() {
	$root = New-FixtureRoot 'crossclass'
	Set-FixtureText $root 'Engine/Source/A.cpp' "int A() { return 1; }`n"
	Set-FixtureText $root 'Data/Shaders/B.h' "float b = 1.0;`n"
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	Invoke-FixtureGit $root @('mv', 'Engine/Source/A.cpp', 'Data/Shaders/A.h')
	Invoke-FixtureGit $root @('mv', 'Data/Shaders/B.h', 'Engine/Source/B.h')
	$default = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline)
	Assert-Equal 0 $default.ExitCode 'cross-class default exit code'
	if ($null -ne $default.Json) {
		$map = Get-EntryMap $default.Json
		Assert-Equal 'glsl' $map['Data/Shaders/A.h'].class 'cross-class rename into Data/Shaders is glsl'
		Assert-Equal 'cpp' $map['Engine/Source/B.h'].class 'cross-class rename out of Data/Shaders is cpp'
		Assert-True ($default.Json.triggers.repoCodeReview -eq $true) 'cross-class rename triggers repoCodeReview from the C++ side'
		Assert-True ($default.Json.triggers.glslReview -eq $true) 'cross-class rename triggers glslReview from the GLSL side'
	}
	$manifest = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline, '-EmitTargetManifest')
	Assert-Equal 0 $manifest.ExitCode 'cross-class manifest exit code'
	if ($null -ne $manifest.Json) {
		$keys = @(@($manifest.Json.pairs) | ForEach-Object { "$(if ($null -eq $_.baseline) { '' } else { $_.baseline.path })|$(if ($null -eq $_.current) { '' } else { $_.current.path })" })
		Assert-Equal ('|Engine/Source/B.h ; Engine/Source/A.cpp|') ($keys -join ' ; ') 'cross-class rename emits one-sided pairs for the C++ side only'
	}
}

# --- Repository C: type change, gitlink identity, landing, manifest mode block ------------------

function New-RepositoryC() {
	$root = New-FixtureRoot 'head'
	Set-FixtureText $root 'Engine/Source/Link.h' "int link = 1;`n"
	Set-FixtureText $root 'Documents/Plans/Area/Thing.md' "plan`n"
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	$linkTarget = 'Engine/Source/Target.h'
	$blobSource = Join-Path ([IO.Path]::GetTempPath()) ('inventory-linktarget-' + [Guid]::NewGuid().ToString('n').Substring(0, 8))
	[IO.File]::WriteAllBytes($blobSource, $script:Utf8.GetBytes($linkTarget))
	$blob = (Get-FixtureGitText $root @('hash-object', '-w', '--', $blobSource)).Trim()
	Remove-Item -LiteralPath $blobSource -Force
	Invoke-FixtureGit $root @('update-index', '--cacheinfo', "120000,$blob,Engine/Source/Link.h")
	Invoke-FixtureGit $root @('update-index', '--add', '--cacheinfo', "160000,$script:GitlinkCommit,Third/Sub")
	Set-FixtureText $root 'Documents/Plans/Area/Thing.md' "plan`nchanged`n"
	Invoke-FixtureGit $root @('add', 'Documents/Plans/Area/Thing.md')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'head')
	$head = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	return [pscustomobject] @{ Root = $root; Baseline = $baseline; Head = $head; LinkTarget = $linkTarget }
}

function Test-HeadInventory($Fixture) {
	$run = Invoke-Inventory @('-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-Head', $Fixture.Head)
	Assert-Equal 0 $run.ExitCode 'head-side exit code'
	if ($null -eq $run.Json) { Assert-True $false 'head-side emitted JSON'; return }
	Assert-Equal $Fixture.Head $run.Json.headSha 'head-side headSha'
	$map = Get-EntryMap $run.Json
	Assert-Equal 'T' $map['Engine/Source/Link.h'].status 'type change reports status T'
	Assert-Equal 'cpp' $map['Engine/Source/Link.h'].class 'type change keeps its path class'
	Assert-Equal '120000' $map['Engine/Source/Link.h'].current.mode 'type change reports the new mode'
	Assert-Equal (Get-TextSha256 $Fixture.LinkTarget) $map['Engine/Source/Link.h'].current.sha256 'type change hashes the symlink blob'
	Assert-Equal 'A' $map['Third/Sub'].status 'gitlink reports status A'
	Assert-Equal 'other' $map['Third/Sub'].class 'gitlink classifies as other'
	Assert-Equal '160000' $map['Third/Sub'].current.mode 'gitlink reports mode 160000'
	Assert-Equal (Get-TextSha256 $script:GitlinkCommit) $map['Third/Sub'].current.sha256 'gitlink identity hashes its commit hex'
	Assert-Equal 0 $run.Json.counts.unlistedUntracked 'a commit-valued head has no untracked count'
	Assert-True ($run.Json.triggers.planTouched -eq $true) 'head-side planTouched'
}

function Test-LandingInventory($Fixture) {
	$run = Invoke-InventoryReadOnly $Fixture.Root @('-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-Head', $Fixture.Head, '-Landing') 'landing run'
	Assert-Equal 0 $run.ExitCode 'landing exit code'
	if ($null -eq $run.Json) { Assert-True $false 'landing emitted JSON'; return }
	$expected = @()
	foreach ($line in (Get-FixtureGitText $Fixture.Root @('diff', '--raw', '-M', "$($Fixture.Baseline)...$($Fixture.Head)")) -split "`n") {
		if ([string]::IsNullOrWhiteSpace($line)) { continue }
		$expected += ($line -split "`t")[1]
	}
	$actual = @($run.Json.landing.reviewed | ForEach-Object { $_.path })
	Assert-Equal (($expected | Sort-Object) -join ' ; ') ((@($actual) | Sort-Object) -join ' ; ') 'landing reviewed diff paths match git diff --raw -M base...head'
	Assert-True ($run.Json.landing.planTouched -eq $true) 'landing reports planTouched'
	Assert-True ($null -ne $run.Json.landing.porcelain) 'landing reports porcelain state'
	Assert-True ($null -ne $run.Json.landing.submodules) 'landing reports submodule state'
	Assert-True ($null -eq $run.Json.regions) 'landing does not emit regions'
}

function Test-ManifestModeBlocked($Fixture) {
	$run = Invoke-Inventory @('-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-Head', $Fixture.Head, '-EmitTargetManifest')
	Assert-Equal 2 $run.ExitCode 'manifest mode block exit code'
	Assert-Equal 0 $run.Stdout.Length 'manifest mode block writes zero stdout bytes'
	Assert-True ($null -ne $run.ErrorJson) 'manifest mode block emits a structured stderr envelope'
	if ($null -ne $run.ErrorJson) {
		Assert-Equal 'blocked' $run.ErrorJson.status 'manifest mode block status'
		Assert-Equal 'inventory.manifest-mode-unsupported' $run.ErrorJson.code 'manifest mode block code'
	}
}

# --- Empty results stay arrays ------------------------------------------------------------------

function Test-EmptyResult() {
	$root = New-FixtureRoot 'empty'
	Set-FixtureText $root 'Notes.md' "notes`n"
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	$unchanged = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline)
	Assert-Equal 0 $unchanged.ExitCode 'unchanged tree exit code'
	Assert-True ($unchanged.Stdout.Contains('"entries":[]')) 'unchanged tree emits entries as an empty array'
	if ($null -ne $unchanged.Json) { Assert-Equal 0 $unchanged.Json.counts.total 'unchanged tree counts.total' }
	Set-FixtureText $root 'Notes.md' "notes`nchanged`n"
	$manifest = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline, '-EmitTargetManifest')
	Assert-Equal 0 $manifest.ExitCode 'manifest without C++ changes exit code'
	Assert-True ($manifest.Stdout.Contains('"pairs":[]')) 'manifest without C++ changes emits pairs as an empty array'
}

# --- Repository D: an untracked addition Git cannot report as a rename --------------------------

function Test-UnreportedRenameBlocked() {
	$root = New-FixtureRoot 'rename'
	$content = "int Moved() { return 7; }`n"
	Set-FixtureText $root 'Engine/Source/Moved.cpp' $content
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	Invoke-FixtureGit $root @('rm', '--quiet', 'Engine/Source/Moved.cpp')
	Set-FixtureText $root 'Engine/Source/MovedNew.cpp' $content
	$run = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline, '-IncludeUntracked', 'Engine/Source/MovedNew.cpp', '-EmitTargetManifest')
	Assert-Equal 2 $run.ExitCode 'unreported rename exit code'
	Assert-Equal 0 $run.Stdout.Length 'unreported rename writes zero stdout bytes'
	if ($null -ne $run.ErrorJson) { Assert-Equal 'inventory.manifest-unreported-rename' $run.ErrorJson.code 'unreported rename code' }
	else { Assert-True $false 'unreported rename emits a structured stderr envelope' }
}

# --- Repository E: cap behaviour ---------------------------------------------------------------

function Test-CapBehaviour() {
	$root = New-FixtureRoot 'caps'
	Set-FixtureText $root 'Engine/Source/Seed.cpp' "seed`n"
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	$listed = [Collections.Generic.List[string]]::new()
	for ($index = 0; $index -lt 501; $index++) {
		$path = 'Bulk/File{0:D3}.cpp' -f $index
		Set-FixtureText $root $path "line $index`n"
		$listed.Add($path)
	}
	$arguments = @('-RepositoryRoot', $root, '-Baseline', $baseline, '-IncludeUntracked', ($listed -join ','))
	$run = Invoke-Inventory $arguments
	Assert-Equal 0 $run.ExitCode 'entry cap exit code'
	if ($null -ne $run.Json) {
		Assert-Equal 501 $run.Json.counts.total 'entry cap counts describe the full inventory'
		Assert-Equal 501 $run.Json.counts.cpp 'entry cap per-class counts describe the full inventory'
		Assert-Equal 0 $run.Json.counts.unlistedUntracked 'entry cap listed every untracked path'
		Assert-True ($run.Json.truncated -eq $true) 'entry cap reports truncated'
		Assert-Equal 501 $run.Json.truncation.entries.full 'entry cap reports the full entry count'
		Assert-Equal 500 $run.Json.truncation.entries.emitted 'entry cap reports the emitted entry count'
		Assert-Equal 500 @($run.Json.entries).Count 'entry cap emits exactly the capped entries'
		Assert-True ($run.Json.triggers.repoCodeReview -eq $true) 'entry cap derives triggers from the full inventory'
		Assert-True ($script:Utf8.GetByteCount($run.Stdout) -le 131072) 'entry cap output stays within the stdout budget'
	}
	else { Assert-True $false 'entry cap emitted JSON' }
	$manifest = Invoke-Inventory ($arguments + @('-EmitTargetManifest'))
	Assert-Equal 2 $manifest.ExitCode 'manifest cap exit code'
	Assert-Equal 0 $manifest.Stdout.Length 'manifest cap writes zero stdout bytes'
	if ($null -ne $manifest.ErrorJson) { Assert-Equal 'inventory.manifest-cap-exceeded' $manifest.ErrorJson.code 'manifest cap code' }
	else { Assert-True $false 'manifest cap emits a structured stderr envelope' }
}

# --- Repository F: the manifest stdout byte budget, distinct from the pair cap -------------------

function Test-ManifestByteBudgetBlocked() {
	$root = New-FixtureRoot 'bytes'
	# Exactly the pair cap, so only the byte budget can block: 500 two-sided pairs of these paths
	# serialize past 131,072 bytes.
	for ($index = 0; $index -lt 500; $index++) {
		Set-FixtureText $root ('Engine/Source/Subsystem/LongNamedTranslationUnit{0:D3}.cpp' -f $index) "line $index`n"
	}
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	for ($index = 0; $index -lt 500; $index++) {
		Set-FixtureText $root ('Engine/Source/Subsystem/LongNamedTranslationUnit{0:D3}.cpp' -f $index) "line $index`nchanged`n"
	}
	$run = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline, '-EmitTargetManifest')
	Assert-Equal 2 $run.ExitCode 'manifest byte budget exit code'
	Assert-Equal 0 $run.Stdout.Length 'manifest byte budget writes zero stdout bytes'
	if ($null -ne $run.ErrorJson) {
		Assert-Equal 'inventory.manifest-cap-exceeded' $run.ErrorJson.code 'manifest byte budget code'
		Assert-True ($run.ErrorJson.message.Contains('stdout budget')) 'manifest byte budget blocks on the byte budget, not the pair cap'
	}
	else { Assert-True $false 'manifest byte budget emits a structured stderr envelope' }
}

# --- Repository I: the stdout budget binds on the landing arrays ---------------------------------

function Test-LandingBudget() {
	$root = New-FixtureRoot 'budget'
	# 500 long-pathed renames make the reviewed array alone exceed the stdout budget, so the budget can
	# only hold if the loop keeps shedding after the regions and entries are gone.
	$filler = 'n' * 80
	for ($index = 0; $index -lt 500; $index++) {
		Set-FixtureText $root ('Engine/Source/Subsystem/{0}{1:D3}.cpp' -f $filler, $index) "x`n"
	}
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	Invoke-FixtureGit $root @('mv', 'Engine/Source/Subsystem', 'Engine/Source/Relocated')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'head')
	$head = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	$run = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline, '-Head', $head, '-Landing')
	Assert-Equal 0 $run.ExitCode 'landing budget exit code'
	$actualBytes = $script:Utf8.GetByteCount($run.Stdout)
	Assert-True ($actualBytes -le 131072) 'landing budget output stays within the stdout budget'
	if ($null -eq $run.Json) { Assert-True $false 'landing budget emitted JSON'; return }
	Assert-Equal $actualBytes $run.Json.truncation.outputBytes 'landing budget reports the byte count it actually emitted'
	Assert-True ($run.Json.truncated -eq $true) 'landing budget reports truncated'
	Assert-Equal 0 $run.Json.truncation.entries.emitted 'landing budget sheds the entries first'
	Assert-True ($run.Json.truncation.landing.reviewed.emitted -lt $run.Json.truncation.landing.reviewed.full) 'landing budget sheds reviewed rows and reports the full count'
	Assert-Equal $run.Json.truncation.landing.reviewed.emitted @($run.Json.landing.reviewed).Count 'landing budget emits exactly the reported reviewed rows'
}

# --- Repository G: directories outside the analyzer corpus ---------------------------------------

function Test-ManifestCorpusExclusion() {
	$root = New-FixtureRoot 'corpus'
	$excluded = @('ThirdParty/Prebuilts/Source/Vendor.cpp', '.agents/scripts/Helper.cpp', '.claude/Tool.cpp', 'Temp/Scratch.cpp')
	foreach ($path in $excluded + @('Engine/Source/Keep.cpp', 'ThirdParty/Prebuilts/Source/Adopted.cpp')) { Set-FixtureText $root $path "int f() { return 1; }`n" }
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	foreach ($path in $excluded + @('Engine/Source/Keep.cpp')) {
		$full = Join-Path $root ($path -replace '/', [IO.Path]::DirectorySeparatorChar)
		[IO.File]::WriteAllBytes($full, $script:Utf8.GetBytes(([IO.File]::ReadAllText($full)) + "// changed`n"))
	}
	Invoke-FixtureGit $root @('mv', 'ThirdParty/Prebuilts/Source/Adopted.cpp', 'Engine/Source/Adopted.cpp')
	$default = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline)
	Assert-Equal 0 $default.ExitCode 'corpus exclusion default exit code'
	if ($null -ne $default.Json) {
		Assert-Equal 6 $default.Json.counts.cpp 'corpus exclusion leaves classes and counts unchanged'
		Assert-True ($default.Json.triggers.repoCodeReview -eq $true) 'corpus exclusion leaves triggers unchanged'
	}
	else { Assert-True $false 'corpus exclusion default emitted JSON' }
	$manifest = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline, '-EmitTargetManifest')
	Assert-Equal 0 $manifest.ExitCode 'corpus exclusion manifest exit code'
	if ($null -ne $manifest.Json) {
		$keys = @(@($manifest.Json.pairs) | ForEach-Object { "$(if ($null -eq $_.baseline) { '' } else { $_.baseline.path })|$(if ($null -eq $_.current) { '' } else { $_.current.path })" })
		Assert-Equal ('|Engine/Source/Adopted.cpp ; Engine/Source/Keep.cpp|Engine/Source/Keep.cpp') ($keys -join ' ; ') 'manifest omits excluded directories and emits the eligible rename side one-sided'
	}
	else { Assert-True $false 'corpus exclusion manifest emitted JSON' }
}

# --- Repository H: a type change between an ordinary C++ file and a gitlink -----------------------

function Test-GitlinkTypeChange() {
	$root = New-FixtureRoot 'gitlink'
	Set-FixtureText $root 'Engine/Source/ToLink.h' "int toLink = 1;`n"
	Set-FixtureText $root 'Notes.md' "notes`n"
	Invoke-FixtureGit $root @('add', '--all')
	Invoke-FixtureGit $root @('update-index', '--add', '--cacheinfo', "160000,$script:GitlinkCommit,Engine/Source/FromLink.h")
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'baseline')
	$baseline = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	$blobSource = Join-Path ([IO.Path]::GetTempPath()) ('inventory-fromlink-' + [Guid]::NewGuid().ToString('n').Substring(0, 8))
	[IO.File]::WriteAllBytes($blobSource, $script:Utf8.GetBytes("int fromLink = 2;`n"))
	$blob = (Get-FixtureGitText $root @('hash-object', '-w', '--', $blobSource)).Trim()
	Remove-Item -LiteralPath $blobSource -Force
	Invoke-FixtureGit $root @('update-index', '--add', '--cacheinfo', "100644,$blob,Engine/Source/FromLink.h")
	Invoke-FixtureGit $root @('update-index', '--add', '--cacheinfo', "160000,$script:GitlinkCommit,Engine/Source/ToLink.h")
	Invoke-FixtureGit $root @('commit', '--quiet', '-m', 'head')
	$head = (Get-FixtureGitText $root @('rev-parse', 'HEAD')).Trim()
	$default = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline, '-Head', $head)
	Assert-Equal 0 $default.ExitCode 'gitlink type change default exit code'
	if ($null -ne $default.Json) {
		$map = Get-EntryMap $default.Json
		Assert-Equal 'T' $map['Engine/Source/ToLink.h'].status 'gitlink type change reports status T'
		Assert-Equal 'other' $map['Engine/Source/ToLink.h'].class 'a path turned into a gitlink classifies as other'
		Assert-Equal 'cpp' $map['Engine/Source/FromLink.h'].class 'a gitlink turned into an ordinary header classifies as cpp'
		Assert-True ($default.Json.triggers.repoCodeReview -eq $true) 'the baseline C++ side of a type change still triggers repoCodeReview'
	}
	else { Assert-True $false 'gitlink type change default emitted JSON' }
	$manifest = Invoke-Inventory @('-RepositoryRoot', $root, '-Baseline', $baseline, '-Head', $head, '-EmitTargetManifest')
	Assert-Equal 0 $manifest.ExitCode 'gitlink type change manifest exit code'
	if ($null -ne $manifest.Json) {
		$keys = @(@($manifest.Json.pairs) | ForEach-Object { "$(if ($null -eq $_.baseline) { '' } else { $_.baseline.path })|$(if ($null -eq $_.current) { '' } else { $_.current.path })" })
		Assert-Equal ('|Engine/Source/FromLink.h ; Engine/Source/ToLink.h|') ($keys -join ' ; ') 'a gitlink type change emits only its ordinary side, one-sided'
	}
	else { Assert-True $false 'gitlink type change manifest emitted JSON' }
}

# --- Argument-level blocked results -------------------------------------------------------------

function Test-BlockedArgument($Fixture) {
	$conflict = Invoke-Inventory @('-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-Regions', '-Landing')
	Assert-Equal 2 $conflict.ExitCode 'mode conflict exit code'
	if ($null -ne $conflict.Json) { Assert-Equal 'inventory.mode-conflict' $conflict.Json.code 'mode conflict code' } else { Assert-True $false 'mode conflict emitted JSON' }

	$conflictManifest = Invoke-Inventory @('-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-Regions', '-EmitTargetManifest')
	Assert-Equal 2 $conflictManifest.ExitCode 'mode conflict under manifest exit code'
	Assert-Equal 0 $conflictManifest.Stdout.Length 'mode conflict under manifest writes zero stdout bytes'
	if ($null -ne $conflictManifest.ErrorJson) { Assert-Equal 'inventory.mode-conflict' $conflictManifest.ErrorJson.code 'mode conflict under manifest code' }

	$landing = Invoke-Inventory @('-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-Landing')
	Assert-Equal 2 $landing.ExitCode 'landing without head exit code'
	if ($null -ne $landing.Json) { Assert-Equal 'inventory.landing-requires-head' $landing.Json.code 'landing without head code' } else { Assert-True $false 'landing without head emitted JSON' }

	$missing = 'a' * 40
	$unresolved = Invoke-Inventory @('-RepositoryRoot', $Fixture.Root, '-Baseline', $missing)
	Assert-Equal 2 $unresolved.ExitCode 'unresolvable baseline exit code'
	if ($null -ne $unresolved.Json) {
		Assert-Equal 'inventory.baseline-unresolved' $unresolved.Json.code 'unresolvable baseline code'
		Assert-True ($unresolved.Json.message.Contains($missing)) 'unresolvable baseline message names the baseline'
	}
	else { Assert-True $false 'unresolvable baseline emitted JSON' }

	$shortBaseline = Invoke-Inventory @('-RepositoryRoot', $Fixture.Root, '-Baseline', 'HEAD')
	Assert-Equal 2 $shortBaseline.ExitCode 'non-SHA baseline exit code'
	if ($null -ne $shortBaseline.Json) { Assert-Equal 'inventory.baseline-unresolved' $shortBaseline.Json.code 'non-SHA baseline code' }

	$unresolvedManifest = Invoke-Inventory @('-RepositoryRoot', $Fixture.Root, '-Baseline', $missing, '-EmitTargetManifest')
	Assert-Equal 2 $unresolvedManifest.ExitCode 'unresolvable baseline under manifest exit code'
	Assert-Equal 0 $unresolvedManifest.Stdout.Length 'unresolvable baseline under manifest writes zero stdout bytes'

	$head = Invoke-Inventory @('-RepositoryRoot', $Fixture.Root, '-Baseline', $Fixture.Baseline, '-Head', 'no-such-revision')
	Assert-Equal 2 $head.ExitCode 'unresolvable head exit code'
	if ($null -ne $head.Json) { Assert-Equal 'inventory.head-unresolved' $head.Json.code 'unresolvable head code' }

	$relative = Invoke-Inventory @('-RepositoryRoot', 'relative/path', '-Baseline', $Fixture.Baseline)
	Assert-Equal 2 $relative.ExitCode 'relative repository root exit code'
	if ($null -ne $relative.Json) { Assert-Equal 'inventory.repository-root-invalid' $relative.Json.code 'relative repository root code' }

	$subdirectory = Invoke-Inventory @('-RepositoryRoot', (Join-Path $Fixture.Root 'Engine'), '-Baseline', $Fixture.Baseline)
	Assert-Equal 2 $subdirectory.ExitCode 'subdirectory repository root exit code'
	if ($null -ne $subdirectory.Json) { Assert-Equal 'inventory.root-not-toplevel' $subdirectory.Json.code 'subdirectory repository root code' }
	else { Assert-True $false 'subdirectory repository root emitted JSON' }
}

try {
	$repositoryA = New-RepositoryA
	Test-DefaultInventory $repositoryA
	Test-RegionInventory $repositoryA
	Test-ManifestInventory $repositoryA
	Test-BlockedArgument $repositoryA
	Test-CrossClassRename
	$repositoryC = New-RepositoryC
	Test-HeadInventory $repositoryC
	Test-LandingInventory $repositoryC
	Test-ManifestModeBlocked $repositoryC
	Test-EmptyResult
	Test-UnreportedRenameBlocked
	Test-CapBehaviour
	Test-ManifestByteBudgetBlocked
	Test-LandingBudget
	Test-ManifestCorpusExclusion
	Test-GitlinkTypeChange
}
finally {
	foreach ($root in $script:Roots) {
		if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
	}
}

if ($script:Failures.Count -gt 0) {
	Write-Host "FAILED: $($script:Failures.Count) fixture assertion(s)."
	exit 1
}

Write-Host 'All session change inventory fixtures passed.'
exit 0
