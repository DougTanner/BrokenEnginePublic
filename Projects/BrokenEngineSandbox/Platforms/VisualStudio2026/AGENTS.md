# BrokenEngineSandbox Visual Studio 2026 Projects

## Overview

MSBuild project layer for the game: client and server `.vcxproj`s compile the same source tree (`Common/`, `Engine/Source/`, game `Source/`, generated data headers) with different defines. Each project has its own single-project `.sln` — building one never builds the other. `Build/`, `Output/`, `Temp/` are build artifacts.

## Client and Server Projects

- BrokenEngineSandbox (client): defines `BT_ENGINE;BT_CLIENT` plus the config define (`BT_DEBUG`/`BT_PROFILE`/`BT_RELEASE`)
- BrokenEngineSandboxServer: same but `BT_SERVER`

Both use `$(ProjectName)` in `IntDir`, so client and server builds can run simultaneously without trampling each other's intermediates. The server vcxproj contains no engine `Graphics/`, `Audio/`, or input `.cpp` files; it still compiles the shared Frame collections, terrain/collision/nav (`IslandTerrain`, `Collision`, `NavBuild`, `NavQuery`), and the Tweaks `*WrappersBase` layer (tweak values participate in the deterministic sim) — it is not a minimal physics binary.

## Build Configuration

- Configurations: Debug, Profile, Release (x64 only)
- Compiler: C++23, `Pch.h` force-included, `/fp:strict` in every config of both projects (cross-binary float determinism for client reconciliation/replay — see `Documents/FloatingPointDeterminism.txt`). Compile settings are mirrored between the two vcxprojs: when changing one, change the other.
- Data properties: `DataBuildMode` accepts only `Local` or `Shared`; ordinary Visual Studio builds default to Local. The canonical repository-root `.git` marker sets `RunDataPacker` default: false when it is a linked-worktree file, true for Local when it is the primary-checkout directory. Explicit Local true permits deliberate worktree generation; Shared requires false. `GameDataDirectory` defaults to `$(ProjectDir)Output\Data` in Local and must be absolute in Shared. `GeneratedDataIncludeRoot` is its parent, matching `#include "Data/..."`. Agent builds pass all four properties identically with `RunDataPacker=false`.
- PreBuildEvent: Every configuration first provisions ThirdParty and requires its prebuilt configuration library; consumers never build ThirdParty recursively. Local `RunDataPacker=true` incrementally rebuilds Release DataPacker before linked-worktree export. Primary Local preserves missing-executable-only DataPacker bootstrap; Shared and agent builds skip it. See `../../../../ThirdParty/Prebuilts/Platforms/VisualStudio2026/AGENTS.md`.
	- Provisioning steps invoke `powershell.exe` (Windows PowerShell 5.1), never `pwsh.exe`. 5.1 ships with every Windows install, so an unassisted vcxproj or IDE build succeeds on a machine that has no PowerShell 7. This is the deliberate exception to the repository's PowerShell 7 standard, which governs agent-invoked helpers rather than the build itself; it binds all three consuming projects — client, server, and DataPacker. Keep the invoked `.ps1` scripts 5.1-compatible.
- CustomBuildStep: DataPacker export runs only for Local with `RunDataPacker=true`. Primary Local Visual Studio builds preserve automatic export; linked worktrees default off and require explicit opt-in. The build step does not pre-create `GameDataDirectory`; DataPacker must observe an absent output leaf so it can establish or materialize its worktree copy-on-write output. Agent Local builds with `RunDataPacker=false` require already-readable selected output.
- Microsoft code analysis: Release-only, with two paths selected by `RunCodeAnalysis`. Both fail the build on a diagnostic; they differ in rule surface and in what does the failing.
  - `RunCodeAnalysis=true` — the Release project default, so Visual Studio Release builds take this path, as does the `/compile` skill's explicit PREfast mode. The toolchain adds `/analyze:quiet`, applies `BrokenEngineAnalysis.ruleset`, and re-emits results after link through the `NativeCodeAnalysis` task. Because those diagnostics never reach cl's console, `TreatWarningAsError` cannot see them; `CodeAnalysisTreatWarningsAsErrors=true` in both Release configurations is what fails the build. This is the only path that applies the rule set, and so the only one reporting the C26xxx Core Guidelines codes.
  - `RunCodeAnalysis=false` — what agent builds pass outside PREfast mode. `<EnablePREfast>true</EnablePREfast>` in the Release `ClCompile` block sits outside the toolchain's `RunMsvcAnalysis` gate, so cl still runs `/analyze` with console output under `TreatWarningAsError`, against the compiler's default rules and no rule set. Never drop `EnablePREfast` to quiet a warning — that silently retires this gate.
  - `BrokenEngineAnalysis.ruleset` is `IncludeAll` minus an explicit per-code allow list, so a newly introduced code fails by default. Allow a code only by adding its own `<Rule>` entry, and never pass `CodeAnalysisNeverReportRuleErrors` — it disables error promotion silently.
- Clang-Tidy: server enables it in all configs; client in Debug and Release (off in Profile). Checks come from the repo-root `.clang-tidy`; VS appends the `clang-analyzer-*` suite after the config's Checks, so analyzer exclusions live in each vcxproj's `<ClangTidyChecks>`, not in `.clang-tidy`. Opt-in per project: *Properties → Code Analysis → Enable Clang-Tidy*. Agent builds via `/compile` force-disable it (the bundled `clang-tidy.exe` crashes on this codebase), so tidy diagnostics appear only in the IDE.

## vcxproj File Inclusion Rules

Each `.cpp` belongs in the client vcxproj, server vcxproj, or both, matching its preprocessor-guard scope (root rule). Shared files (no guards or partial guards) go in both; entirely one-sided files must be removed from the opposite vcxproj — guards alone leave empty translation units compiling there.

Entirely one-sided source files also carry a whole-file `#if defined(BT_CLIENT)` / `BT_SERVER` wrap; headers keep `#pragma once` outside that guard. `Engine.h` guard spans group includes but do not establish affinity. A file force-included by both builds but consumed on only one side stays unwrapped and documents that deliberate exception in its leaf `AGENTS.md`.

Headers follow the same affinity (Solution Explorer cleanliness; `ClInclude` has no build effect). Generated headers are property-based `$(GameDataDirectory)\*.h` items so Shared and Local show the selected source; the server omits the client-only generated `Shader.h`.

Naming conventions that signal build affinity:
- `*Render.cpp` — client-only
- `Network/Client/Client*.cpp` — client-only
- Game-layer `Network/Server/Server*.cpp` — server-only
- Engine `Server.cpp`/`ServerReceive.cpp`/`ServerSend.cpp` — server-only (`BT_SERVER`-wrapped; only the server build constructs `engine::Server`)
- Engine collection files — check guards; many are client-only

Shader sources (`Engine/Data/Shaders/**`) are `<None>` items in the client project only — IDE visibility; DataPacker compiles them, not MSBuild. Adding a shader means adding a `<None>` entry to the client vcxproj and filters.

## vcxproj.filters Rules

Filter paths mirror the on-disk directory structure under three roots:
- `Engine/Source/<path>` → filter `Engine\<path>`; shader `<None>` items → `Engine\Data\Shaders\<sub>`
- `Projects/BrokenEngineSandbox/Source/<path>` → filter `Game\<path>`
- `Common[/<subdir>]` → filter `Common[\<subdir>]`

Exception: generated `$(GameDataDirectory)\*.h` headers live in a flat `DataFiles` filter. New filters need a `<Filter Include>` entry with a unique GUID, and every ancestor filter must exist. The server filters file has an empty leftover filter (`Engine\Graphics`) — cruft, not a signal that such files belong in the server project.
