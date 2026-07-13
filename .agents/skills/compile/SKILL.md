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
	& "$ROOT\.agents\scripts\Provision-WorktreeThirdParty.ps1" -RepositoryRoot $ROOT
	if ($LASTEXITCODE -ne 0) { throw "ThirdParty provisioning failed: $LASTEXITCODE" }
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
- ThirdParty builds only on explicit request. Missing source or library links are provisioning failures; never rebuild ThirdParty automatically.
- DataPacker builds Release only. AgentCli still supplies the normal worktree-local target serialization; DataPacker's `"BrokenEngineDataPacker"` mutex remains the only PC-global coordination exception.

Before any AgentCli, DataPacker, client, or server build, invoke `$ROOT\.agents\scripts\Provision-WorktreeThirdParty.ps1 -RepositoryRoot $ROOT` and stop on failure. Validated stable primary submodule trees and shared prebuilt Output are the only exception to worktree-local build artifacts.

The manager must supply three immutable lifecycle values: `$PRIMARY` (absolute primary checkout), `$ROOT` (absolute adopted session worktree), and `$BASELINE` (fixed session-start commit). Do not rediscover the primary checkout or move the baseline. Canonicalize both paths, require distinct directories with the same `git rev-parse --git-common-dir`, and never build in `$PRIMARY`. Missing or inconsistent lifecycle metadata is a hard stop. `Build/`, `Output/`, and `.claude/build-locks/` are worktree-local; do not share or seed them between worktrees. Accept the first cold C++/PCH build.

DataPacker's mutex coordinates across worktrees and its shared chunks live under `%TEMP%\DataPacker\<Project>`; do not add another PC-global DataPacker lock or a checkout-local cache copy. Gaea raw and split intermediates use the single mutable `%TEMP%\DataPacker\<Project>\Gaea\Islands` cache; source-tree island leaves retain only tracked BC outputs.

**Run every build synchronously in the foreground and stay in-turn until the `AgentCli build` call returns an exit code — only then report. NEVER background a build (`run_in_background`, a `Monitor` watcher, `Start-Job`, or a trailing `&`) and then end your turn to await completion.** A delegated subagent that yields its turn while a build runs is not reliably re-woken when the build finishes, so the workflow stalls half-done — a common failure is ThirdParty completing while the client/server targets are never started — and no result is ever reported. Give each foreground `AgentCli build` call the maximum execution timeout the tool allows so AgentCli's 660-second lock wait cannot be preempted; a cold first build of a fresh worktree can approach that limit. If a call times out with the build still running, **re-invoke the same `AgentCli build` command** — AgentCli serializes per target and its incremental/tlog state continues the same build to completion — rather than backgrounding it. If blocking is genuinely unacceptable, poll the build's own output to completion within the same turn (repeated short reads in a loop) and report only once you hold the final per-target exit code; never hand the wait to a fire-and-forget watcher and stop. Orchestrated worktree game builds never run DataPacker, Gaea, or texture export. Always preserve `/p:EnableClangTidyCodeAnalysis=false /p:RunCodeAnalysis=false`; the bundled VS2026 clang-tidy crashes on this codebase.

## Select runtime data mode

Every worktree game build uses one data mode and one canonical directory for both client and server:

- **Shared** is the default for ordinary code changes. Set `$GameDataDirectory` to `$PRIMARY\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data`; this mode consumes primary generated headers/packs and disables every DataPacker build/export step.
- **Local** is mandatory when tracked changes from `$BASELINE`, staged changes, unstaged changes, or untracked files touch `DataPacker/**`, `Engine/Data/**`, `Projects/BrokenEngineSandbox/Data/**`, `Common/DataFile.h`, generated-header logic, exporter versions/fingerprints, compression, chunk layout, or pack/manifest contracts. Set `$GameDataDirectory` to `$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\Output\Data`.
- A user may force Local. Never force Shared over a Local trigger. Local consumes only output the user explicitly prepared for the current worktree state; it never initiates export and never falls back to Shared data.

The game projects detect the canonical repository-root Git marker. A linked worktree has a `.git` file and defaults `RunDataPacker=false`; the primary checkout has a `.git` directory and preserves ordinary Local Visual Studio behavior by defaulting true. An explicit Local `RunDataPacker=true` permits deliberate worktree generation; Shared rejects true. This compile skill always passes false and never opts in.

Build the changed-path set from `git -C $ROOT diff --name-only $BASELINE --` plus `git -C $ROOT ls-files --others --exclude-standard`; normalize separators before matching. This single baseline diff includes committed, staged, and unstaged tracked changes. If a changed file may affect generated or serialized bytes and the path rules do not prove otherwise, select Local.

Set `$GeneratedDataIncludeRoot` to the canonical parent of `$GameDataDirectory` (consumers include `Data/...`) and pass these exact properties to every client/server `.sln` or `.vcxproj` build, including `--files`:

```powershell
$DataProperties = @(
	"/p:DataBuildMode=$DataBuildMode",
	'/p:RunDataPacker=false',
	"/p:GameDataDirectory=$GameDataDirectory",
	"/p:GeneratedDataIncludeRoot=$GeneratedDataIncludeRoot"
)
```

Before Shared compilation, require these ten nonempty headers: `Data.h`, `DataTypes.h`, `Audio.h`, `Font.h`, `Scene.h`, `Islands.h`, `Model.h`, `Shader.h`, `Texture.h`, `Raw.h`. Also require nonempty `.manifest` and `.pack` files for each of `Audio`, `Font`, `Scene`, `Islands`, `Model`, `Shader`, `Texture`, and `Raw`. The Shared directory must be absolute, outside `$ROOT`, and must remain byte-identical: snapshot the required files as normalized relative path + length + SHA-256 before the first build, recheck after each client/server build, and return that snapshot for the harness's pre-launch check. Any missing, added, removed, or changed required file fails the workflow.

Local mode may build the worktree Release DataPacker as a standalone compile check, but must never execute it. Before compiling either game target, require the same complete nonempty output set and explicit confirmation that the user prepared it after the current relevant worktree changes. If either check is absent, stop with `Local data is missing or stale; manual worktree DataPacker export required` and do not build, export, or fall back. Actual generation is a separate user-controlled action outside this skill.

Snapshot the primary required-file set before Local work and recheck it after each game build; any primary write fails the workflow. After the last game build, snapshot the complete selected Local output for the harness. Never copy worktree DataPacker/data source changes into `$PRIMARY` to test them.

## Full-build commands

```powershell
# ThirdParty: choose requested configuration.
& $AgentCli build "$ROOT\ThirdParty\Prebuilts\Platforms\VisualStudio2026\ThirdParty.sln" '/p:Configuration=Debug' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
& $AgentCli build "$ROOT\ThirdParty\Prebuilts\Platforms\VisualStudio2026\ThirdParty.sln" '/p:Configuration=Profile' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
& $AgentCli build "$ROOT\ThirdParty\Prebuilts\Platforms\VisualStudio2026\ThirdParty.sln" '/p:Configuration=Release' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

# DataPacker: Release only.
& $AgentCli build "$ROOT\DataPacker\Platforms\VisualStudio2026\DataPacker.sln" '/p:Configuration=Release' '/p:Platform=x64' '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

# BrokenEngineSandbox client, then server. Substitute Profile/Release only when requested.
& $AgentCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandbox.sln" '/p:Configuration=Debug' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
& $AgentCli build "$ROOT\Projects\BrokenEngineSandbox\Platforms\VisualStudio2026\BrokenEngineSandboxServer.sln" '/p:Configuration=Debug' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'

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
	'/p:Configuration=Debug' '/p:Platform=x64' @DataProperties '/p:EnableClangTidyCodeAnalysis=false' '/p:RunCodeAnalysis=false' '/verbosity:minimal'
```

Only `.cpp` inputs already present in the target project are valid. After a header edit, list the affected project-member `.cpp` files. Add new files to the vcxproj/filters before compiling. Shader changes require a separately prepared Local export before the client build; the build never compiles shaders through DataPacker automatically.

## Report results

- Report only after every build has returned an exit code; do not end your turn (or hand back to the caller) while any build is still running. If you delegated this skill, the build ran in the foreground of your turn per the rule above — its exit code is in hand before you report.
- Final status per project: success or fail.
- Every error line verbatim.
- Warning lines verbatim only for files involved in the change.
- LNK1168 or EXE LNK2019 can mean a client/server process still holds the executable; report it rather than diagnosing unless asked.
- A prior killed build's `unsuccessfulbuild` marker clears on the next successful run; rerun instead of deleting tlogs.
- A lock timeout means another AgentCli build still owns that target. Retry after it finishes; never delete `.claude/build-locks/` manually.
- For game builds, report `DataBuildMode`, `RunDataPacker=false`, canonical `GameDataDirectory`, canonical `GeneratedDataIncludeRoot`, the selected-data identity snapshot, and the primary identity snapshot when Local. Report every mode-selection trigger and the Local prepared-data confirmation. A harness run must use this exact mode/path; it must not infer or substitute one.

End with:

```text
Files changed: none
Functions/regions touched: none
Residuals:
- <failed or skipped required build, or none>
```
