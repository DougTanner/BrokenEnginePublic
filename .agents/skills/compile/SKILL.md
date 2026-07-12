---
name: compile
description: Builds Broken Engine projects through AgentCli's serialized MSBuild driver — exact commands for full and selective (--files) builds. Use whenever you need to build, rebuild, compile, or check for compile/link errors in ThirdParty, DataPacker, AgentCli, or BrokenEngineSandbox (client or server).
allowed-tools: [PowerShell]
---

# Build

Builds through installed AgentCli v2. `AgentCli build` serializes writers per target basename inside the current worktree, waits up to 660 seconds, preserves native Windows argument boundaries, and owns MSBuild through a kill-on-close Job Object.

## Bootstrap AgentCli

Use `%LOCALAPPDATA%\BrokenEngine\AgentCli\v2\AgentCli.exe`. Before the first invocation, probe `--version`; it must print exactly `2`. If missing or mismatched, build Release directly with native PowerShell/MSBuild, install it, then probe again:

```powershell
$AgentCli = Join-Path $env:LOCALAPPDATA 'BrokenEngine\AgentCli\v2\AgentCli.exe'
$Version = if (Test-Path -LiteralPath $AgentCli) { & $AgentCli --version }
if ($Version -ne '2') {
	$MSBuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
	if (-not (Test-Path -LiteralPath $MSBuild)) {
		$VSWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
		$InstallPath = & $VSWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
		$MSBuild = Join-Path $InstallPath 'MSBuild\Current\Bin\MSBuild.exe'
	}
	& $MSBuild "$ROOT\Tools\AgentCli\Platforms\VisualStudio2026\AgentCli.sln" '/p:Configuration=Release' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
	if ($LASTEXITCODE -ne 0) { throw "AgentCli bootstrap build failed: $LASTEXITCODE" }
	& "$ROOT\Tools\AgentCli\Platforms\VisualStudio2026\Output\AgentCli.exe" install
	if ($LASTEXITCODE -ne 0) { throw "AgentCli install failed: $LASTEXITCODE" }
	if ((& $AgentCli --version) -ne '2') { throw 'AgentCli v2 installation verification failed' }
}
```

This direct build is only bootstrap/recovery. All normal builds, including later AgentCli builds, use `AgentCli build`.

## Determine what to build

- Default: BrokenEngineSandbox client Debug.
- If any changed file is shared (`Common/`, `Engine/`, or non-exclusive game code), build both client and server.
- ThirdParty builds only on explicit request or a missing ThirdParty-library link failure.
- DataPacker builds Release only. AgentCli still supplies the normal worktree-local target serialization; DataPacker's `"BrokenEngineDataPacker"` mutex remains the only PC-global coordination exception.

`$ROOT` is the absolute adopted/session worktree. Never build in the primary checkout. `Build/`, `Output/`, and `.claude/build-locks/` are worktree-local; do not share or seed them between worktrees. Accept the first cold C++/PCH build.

DataPacker's mutex coordinates across worktrees and its shared chunks live under `%TEMP%\DataPacker\<Project>`; do not add another PC-global DataPacker lock or a checkout-local cache copy. Gaea raw and split intermediates use the single mutable `%TEMP%\DataPacker\<Project>\Gaea\Islands` cache; source-tree island leaves retain only tracked BC outputs.

Run normal builds in the foreground with an execution timeout of at least 15 minutes so AgentCli's 660-second lock wait cannot be preempted; extend it for cold builds or expected asset work. Use background execution only when a full DataPacker asset re-export is expected. Build client and server sequentially because they share the DataPacker pre-build step. Always preserve `/p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false`; the bundled VS2026 clang-tidy crashes on this codebase.

## Full-build commands

```powershell
# ThirdParty: choose requested configuration.
& $AgentCli build "$ROOT\ThirdParty\Prebuilts\Platforms\VisualStudio2026\ThirdParty.sln" '/p:Configuration=Debug' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
& $AgentCli build "$ROOT\ThirdParty\Prebuilts\Platforms\VisualStudio2026\ThirdParty.sln" '/p:Configuration=Profile' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
& $AgentCli build "$ROOT\ThirdParty\Prebuilts\Platforms\VisualStudio2026\ThirdParty.sln" '/p:Configuration=Release' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

# DataPacker: Release only.
& $AgentCli build "$ROOT\DataPacker\Platforms\VisualStudio2026\DataPacker.sln" '/p:Configuration=Release' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

# BrokenEngineSandbox client, then server. Substitute Profile/Release only when requested.
& $AgentCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.sln" '/p:Configuration=Debug' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
& $AgentCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.sln" '/p:Configuration=Debug' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

# AgentCli: Debug is optional for development.
& $AgentCli build "$ROOT\Tools\AgentCli\Platforms\VisualStudio2026\AgentCli.sln" '/p:Configuration=Debug' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

# Required after any AgentCli source change, even when the version remains 2.
& $AgentCli build "$ROOT\Tools\AgentCli\Platforms\VisualStudio2026\AgentCli.sln" '/p:Configuration=Release' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
if ($LASTEXITCODE -ne 0) { throw "AgentCli Release build failed: $LASTEXITCODE" }
& "$ROOT\Tools\AgentCli\Platforms\VisualStudio2026\Output\AgentCli.exe" install
if ($LASTEXITCODE -ne 0 -or (& $AgentCli --version) -ne '2') { throw 'AgentCli v2 installation verification failed' }
```

After any AgentCli source change, the Release build, `install`, and installed `--version` probe above are mandatory even when the version number did not change.

## Selective file compile

Use `--files` to invalidate specific project-member `.cpp` objects before a normal project build. Target a `.vcxproj`, supply both Configuration and Platform, and separate the source list from the target with `--`:

```powershell
& $AgentCli build --files "$ROOT\Engine\Source\Example.cpp" "$ROOT\Projects\BrokenEngineSandbox\Source\Example.cpp" -- `
	"$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.vcxproj" `
	'/p:Configuration=Debug' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
```

Only `.cpp` inputs already present in the target project are valid. After a header edit, list the affected project-member `.cpp` files. Add new files to the vcxproj/filters before compiling. Shader files cannot use `--files`; run a client build so DataPacker compiles them.

## Report results

- Final status per project: success or fail.
- Every error line verbatim.
- Warning lines verbatim only for files involved in the change.
- LNK1168 or EXE LNK2019 can mean a client/server process still holds the executable; report it rather than diagnosing unless asked.
- A prior killed build's `unsuccessfulbuild` marker clears on the next successful run; rerun instead of deleting tlogs.
- A lock timeout means another AgentCli build still owns that target. Retry after it finishes; never delete `.claude/build-locks/` manually.

End with:

```text
Files changed: none
Functions/regions touched: none
Residuals:
- <failed or skipped required build, or none>
```
