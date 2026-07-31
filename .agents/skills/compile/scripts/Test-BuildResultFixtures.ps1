# Deterministic fixtures for the WorktreeCli `build` result contract
# (broken-engine-build-result/v1): success, warning, compiler error, selective
# --files invalidation, missing target, missing MSBuild, launch failure, lock
# timeout, and retained-log failure. Each case asserts exactly one schema-valid
# JSON object on stdout, the documented exit code, and the retained combined log.
# Run whenever BuildCommand.cpp's result contract changes, against a freshly
# built WorktreeCli.exe — never against a build that would write through a
# canonical Output link.
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string] $WorktreeCliExecutable
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:Failures = [Collections.Generic.List[string]]::new()
$script:CaseCount = 0

function Assert-True([bool] $Condition, [string] $Name) {
	if (-not $Condition) { $script:Failures.Add($Name); Write-Host "FAIL $Name" } else { Write-Host "pass $Name" }
}

function New-FixtureWorktree([string] $Name) {
	$root = Join-Path ([IO.Path]::GetTempPath()) "BrokenEngineBuildFixtures\$([guid]::NewGuid().ToString('N').Substring(0, 8))-$Name"
	New-Item -ItemType Directory -Force (Join-Path $root '.git') | Out-Null
	return $root
}

function Invoke-Build([string] $Worktree, [string[]] $Arguments) {
	$stdout = @(& $WorktreeCliExecutable build @Arguments 2>$null)
	$text = ($stdout -join "`n").Trim()
	$json = $null
	$singleObject = $false
	if (-not [string]::IsNullOrWhiteSpace($text)) {
		try {
			$json = $text | ConvertFrom-Json -Depth 32 -ErrorAction Stop
			$singleObject = @($text -split "`n").Count -eq 1
		}
		catch { }
	}
	return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Text = $text; Json = $json; SingleObject = $singleObject }
}

function Assert-Result($Run, [string] $Case, [string] $ExpectedStatus, [string] $ExpectedFailureKind, [int] $ExpectedExit) {
	$script:CaseCount++
	Assert-True ($null -ne $Run.Json) "$Case stdout parses as JSON"
	if ($null -eq $Run.Json) { return }
	Assert-True $Run.SingleObject "$Case stdout is exactly one JSON object"
	Assert-True ($Run.Json.schemaVersion -ceq 'broken-engine-build-result/v1') "$Case schemaVersion"
	Assert-True ($Run.Json.status -ceq $ExpectedStatus) "$Case status=$ExpectedStatus (was $($Run.Json.status))"
	Assert-True ($Run.Json.failureKind -ceq $ExpectedFailureKind) "$Case failureKind=$ExpectedFailureKind (was $($Run.Json.failureKind))"
	Assert-True ($Run.ExitCode -eq $ExpectedExit) "$Case exit=$ExpectedExit (was $($Run.ExitCode))"
	Assert-True ($Run.Json.exitCode -eq $Run.ExitCode) "$Case json exitCode matches process exit"
}

$WorktreeCliExecutable = (Get-Item -LiteralPath $WorktreeCliExecutable -ErrorAction Stop).FullName
foreach ($variable in @('BROKEN_ENGINE_MSBUILD_PATH', 'BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS')) {
	Remove-Item "Env:\$variable" -ErrorAction SilentlyContinue
}

try {
	# Success: one JSON, retained log holds the raw MSBuild stream.
	$worktree = New-FixtureWorktree 'success'
	Set-Content (Join-Path $worktree 'success.proj') '<Project><Target Name="Build"><Message Text="fixture hello" Importance="high"/></Target></Project>'
	$run = Invoke-Build $worktree @((Join-Path $worktree 'success.proj'), '/verbosity:normal')
	Assert-Result $run 'success' 'success' 'none' 0
	if ($null -ne $run.Json) {
		Assert-True ($run.Json.msbuild.launched -eq $true) 'success msbuild launched'
		Assert-True ($run.Json.retainedLog.complete -eq $true) 'success retained log complete'
		Assert-True (Test-Path -LiteralPath $run.Json.retainedLog.path -PathType Leaf) 'success retained log exists'
		Assert-True ($run.Json.retainedLog.path.StartsWith((Join-Path $worktree 'Temp\AgentBuildLogs'), [StringComparison]::OrdinalIgnoreCase)) 'success retained log under Temp\AgentBuildLogs'
		$log = Get-Content -LiteralPath $run.Json.retainedLog.path -Raw
		Assert-True ($log.Contains('fixture hello')) 'success retained log holds raw output'
		Assert-True ((Get-Item -LiteralPath $run.Json.retainedLog.path).Length -eq $run.Json.retainedLog.bytes) 'success retained log byte count'
	}

	# Warning diagnostic.
	$worktree = New-FixtureWorktree 'warning'
	Set-Content (Join-Path $worktree 'warning.proj') '<Project><Target Name="Build"><Warning Text="fixture careful" Code="TST0002"/></Target></Project>'
	$run = Invoke-Build $worktree @((Join-Path $worktree 'warning.proj'), '/verbosity:normal')
	Assert-Result $run 'warning' 'success' 'none' 0
	if ($null -ne $run.Json) {
		$warnings = @($run.Json.diagnostics | Where-Object { $_.severity -ceq 'warning' -and $_.code -ceq 'TST0002' })
		Assert-True ($warnings.Count -eq 1) 'warning diagnostic parsed'
		if ($warnings.Count -eq 1) { Assert-True ($warnings[0].message -ceq 'fixture careful') 'warning message parsed' }
	}

	# Compiler-style error diagnostic with file/line/column.
	$worktree = New-FixtureWorktree 'error'
	Set-Content (Join-Path $worktree 'error.proj') '<Project><Target Name="Build"><Error Text="fixture boom" Code="TST0001"/></Target></Project>'
	$run = Invoke-Build $worktree @((Join-Path $worktree 'error.proj'), '/verbosity:normal')
	Assert-Result $run 'error' 'fail' 'msbuild' 1
	if ($null -ne $run.Json) {
		$errors = @($run.Json.diagnostics | Where-Object { $_.severity -ceq 'error' -and $_.code -ceq 'TST0001' })
		Assert-True ($errors.Count -eq 1) 'error diagnostic parsed'
		if ($errors.Count -eq 1) {
			Assert-True ($errors[0].message -ceq 'fixture boom') 'error message parsed'
			Assert-True ($errors[0].file.EndsWith('error.proj')) 'error file parsed'
			Assert-True ($errors[0].line -ge 1) 'error line parsed'
			Assert-True ($errors[0].raw.Contains('error TST0001')) 'error raw line preserved'
		}
		Assert-True ($run.Json.msbuild.exitCode -eq 1) 'error msbuild exit recorded'
	}

	# Selective --files invalidation against a minimal ClCompile project.
	$worktree = New-FixtureWorktree 'selective'
	$sourcePath = Join-Path $worktree 'a.cpp'
	Set-Content $sourcePath '// fixture'
	New-Item -ItemType Directory -Force (Join-Path $worktree 'obj') | Out-Null
	Set-Content (Join-Path $worktree 'obj\a.obj') 'stale'
	Set-Content (Join-Path $worktree 'selective.vcxproj') @'
<Project>
	<PropertyGroup><IntDir>$(MSBuildProjectDirectory)\obj\</IntDir></PropertyGroup>
	<ItemGroup><ClCompile Include="a.cpp"><ObjectFileName>obj\a.obj</ObjectFileName></ClCompile></ItemGroup>
	<Target Name="Build"><Message Text="fixture selective" Importance="high"/></Target>
</Project>
'@
	$run = Invoke-Build $worktree @('--files', $sourcePath, '--', (Join-Path $worktree 'selective.vcxproj'), '/p:Configuration=Debug', '/p:Platform=x64', '/verbosity:normal')
	Assert-Result $run 'selective' 'success' 'none' 0
	if ($null -ne $run.Json) {
		Assert-True (@($run.Json.invalidatedObjects).Count -eq 1) 'selective invalidated one object'
		Assert-True (-not (Test-Path -LiteralPath (Join-Path $worktree 'obj\a.obj'))) 'selective object deleted'
		Assert-True (@($run.Json.selectedFiles).Count -eq 1) 'selective selectedFiles recorded'
	}

	# Missing target.
	$worktree = New-FixtureWorktree 'missing-target'
	$run = Invoke-Build $worktree @((Join-Path $worktree 'absent.proj'))
	Assert-Result $run 'missing-target' 'fail' 'tool' 1
	if ($null -ne $run.Json) {
		Assert-True (@($run.Json.messages | Where-Object { $_.Contains('build target does not exist') }).Count -eq 1) 'missing-target message'
	}

	# Missing MSBuild via an invalid explicit pin.
	$worktree = New-FixtureWorktree 'missing-msbuild'
	Set-Content (Join-Path $worktree 'pin.proj') '<Project><Target Name="Build"/></Project>'
	$env:BROKEN_ENGINE_MSBUILD_PATH = Join-Path $worktree 'no-such-msbuild.exe'
	try { $run = Invoke-Build $worktree @((Join-Path $worktree 'pin.proj')) }
	finally { Remove-Item 'Env:\BROKEN_ENGINE_MSBUILD_PATH' -ErrorAction SilentlyContinue }
	Assert-Result $run 'missing-msbuild' 'fail' 'tool' 1
	if ($null -ne $run.Json) {
		Assert-True ($run.Json.msbuild.discovered -eq $false) 'missing-msbuild not discovered'
	}

	# Launch failure: pinned MSBuild exists but is not executable.
	$worktree = New-FixtureWorktree 'launch-failure'
	Set-Content (Join-Path $worktree 'launch.proj') '<Project><Target Name="Build"/></Project>'
	Set-Content (Join-Path $worktree 'not-an-exe.txt') 'plain text'
	$env:BROKEN_ENGINE_MSBUILD_PATH = Join-Path $worktree 'not-an-exe.txt'
	try { $run = Invoke-Build $worktree @((Join-Path $worktree 'launch.proj')) }
	finally { Remove-Item 'Env:\BROKEN_ENGINE_MSBUILD_PATH' -ErrorAction SilentlyContinue }
	Assert-Result $run 'launch-failure' 'fail' 'tool' 1
	if ($null -ne $run.Json) {
		Assert-True ($run.Json.msbuild.discovered -eq $true) 'launch-failure discovered'
		Assert-True ($run.Json.msbuild.launched -eq $false) 'launch-failure not launched'
		Assert-True (@($run.Json.messages | Where-Object { $_.Contains('launch process failed') }).Count -ge 1) 'launch-failure message'
	}

	# Lock timeout: another writer holds the target lock; wait shortened to 0.
	$worktree = New-FixtureWorktree 'lock-timeout'
	Set-Content (Join-Path $worktree 'locked.proj') '<Project><Target Name="Build"/></Project>'
	$lockDirectory = Join-Path $worktree '.claude\build-locks'
	New-Item -ItemType Directory -Force $lockDirectory | Out-Null
	$lockStream = [IO.File]::Open((Join-Path $lockDirectory 'locked.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
	try {
		$env:BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS = '0'
		try { $run = Invoke-Build $worktree @((Join-Path $worktree 'locked.proj')) }
		finally { Remove-Item 'Env:\BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS' -ErrorAction SilentlyContinue }
	}
	finally { $lockStream.Dispose() }
	Assert-Result $run 'lock-timeout' 'fail' 'tool' 1
	if ($null -ne $run.Json) {
		Assert-True ($run.Json.lock.disposition -ceq 'timeout') 'lock-timeout disposition'
	}

	# Retained-log failure: Temp\AgentBuildLogs is blocked by a file.
	$worktree = New-FixtureWorktree 'log-failure'
	Set-Content (Join-Path $worktree 'log.proj') '<Project><Target Name="Build"/></Project>'
	New-Item -ItemType Directory -Force (Join-Path $worktree 'Temp') | Out-Null
	Set-Content (Join-Path $worktree 'Temp\AgentBuildLogs') 'blocking file'
	$run = Invoke-Build $worktree @((Join-Path $worktree 'log.proj'))
	Assert-Result $run 'log-failure' 'fail' 'tool' 1
	if ($null -ne $run.Json) {
		Assert-True (@($run.Json.messages | Where-Object { $_.Contains('retained build log') }).Count -ge 1) 'log-failure message'
	}
}
finally {
	foreach ($variable in @('BROKEN_ENGINE_MSBUILD_PATH', 'BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS')) {
		Remove-Item "Env:\$variable" -ErrorAction SilentlyContinue
	}
}

Write-Host ''
if ($script:Failures.Count -gt 0) {
	Write-Host "Build-result fixtures FAILED ($($script:Failures.Count) assertion(s) across $script:CaseCount cases)."
	exit 1
}
Write-Host "Build-result fixtures passed ($script:CaseCount cases)."
exit 0
