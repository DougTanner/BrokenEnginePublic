# BrokenEngineSandbox Visual Studio 2026 Projects

## Overview

MSBuild project layer for the game: client and server `.vcxproj`s compile the same source tree (`Common/`, `Engine/Source/`, game `Source/`, generated data headers) with different defines. Each project has its own single-project `.sln` — building one never builds the other. `Build/`, `Output/`, `Temp/` are build artifacts.

## Client and Server Projects

- **BrokenEngineSandbox** (client): defines `BT_ENGINE;BT_CLIENT` plus the config define (`BT_DEBUG`/`BT_PROFILE`/`BT_RELEASE`)
- **BrokenEngineSandboxServer**: same but `BT_SERVER`

Both use `$(ProjectName)` in `IntDir`, so client and server builds can run simultaneously without trampling each other's intermediates. The server vcxproj contains no engine `Graphics/`, `Audio/`, or input `.cpp` files; it still compiles the shared Frame collections, terrain/collision/nav (`IslandTerrain`, `Collision`, `NavBuild`, `NavQuery`), and the Tweaks `*WrappersBase` layer (tweak values participate in the deterministic sim) — it is not a minimal physics binary.

## Build Configuration

- **Configurations**: Debug, Profile, Release (x64 only)
- **Compiler**: C++23, `Pch.h` force-included, `/fp:strict` in every config of both projects (cross-binary float determinism for client reconciliation/replay — see `Documents/FloatingPointDeterminism.txt`). Compile settings are mirrored between the two vcxprojs: when changing one, change the other.
- **Data properties**: `DataBuildMode` accepts only `Local` or `Shared`; ordinary Visual Studio builds default to Local. The canonical repository-root `.git` marker sets `RunDataPacker` default: false when it is a linked-worktree file, true for Local when it is the primary-checkout directory. Explicit Local true permits deliberate worktree generation; Shared requires false. `GameDataDirectory` defaults to `$(ProjectDir)Output\Data` in Local and must be absolute in Shared. `GeneratedDataIncludeRoot` is its parent, matching `#include "Data/..."`. Agent builds pass all four properties identically with `RunDataPacker=false`.
- **PreBuildEvent**: Local `RunDataPacker=true` incrementally rebuilds Release DataPacker before every linked-worktree export, preventing an existing stale executable from exporting. Primary-checkout Local builds preserve the missing-executable-only bootstrap; Shared and agent builds skip it. ThirdParty bootstrap remains independent. See [ThirdParty Prebuilts AGENTS.md](../../../../ThirdParty/Prebuilts/Platforms/VisualStudio2026/AGENTS.md).
- **CustomBuildStep**: DataPacker export runs only for Local with `RunDataPacker=true`. Primary Local Visual Studio builds preserve automatic export; linked worktrees default off and require explicit opt-in. Agent Local builds require already-prepared complete current output and fail without fallback.
- **Clang-Tidy**: server enables it in all configs; client in Debug and Release (off in Profile). Checks come from the repo-root `.clang-tidy`; VS appends the `clang-analyzer-*` suite after the config's Checks, so analyzer exclusions live in each vcxproj's `<ClangTidyChecks>`, not in `.clang-tidy`. Opt-in per project: *Properties → Code Analysis → Enable Clang-Tidy*. Agent builds via `/compile` force-disable it (the bundled `clang-tidy.exe` crashes on this codebase), so tidy diagnostics appear only in the IDE.

## vcxproj File Inclusion Rules

Each `.cpp` belongs in the client vcxproj, server vcxproj, or both, matching its preprocessor-guard scope (root rule). Shared files (no guards or partial guards) go in both; entirely one-sided files must be removed from the opposite vcxproj — guards alone leave empty translation units compiling there.

Entirely one-sided source files also carry a whole-file `#if defined(BT_CLIENT)` / `BT_SERVER` wrap; headers keep `#pragma once` outside that guard. `Engine.h` guard spans group includes but do not establish affinity. A file force-included by both builds but consumed on only one side stays unwrapped and documents that deliberate exception in its leaf `AGENTS.md`.

Headers follow the same affinity (Solution Explorer cleanliness; `ClInclude` has no build effect). Generated headers are property-based `$(GameDataDirectory)\*.h` items so Shared and Local show the selected source; the server omits the client-only generated `Shader.h`.

**Naming conventions that signal build affinity:**
- `*Render.cpp` — client-only
- `Network/Client/Client*.cpp` — client-only
- Game-layer `Network/Server/Server*.cpp` — server-only
- Engine `Server.cpp`/`ServerReceive.cpp`/`ServerSend.cpp` — server-only (`BT_SERVER`-wrapped; only the server build constructs `engine::Server`)
- Engine collection files — check guards; many are client-only

**Shader sources** (`Engine/Data/Shaders/**`) are `<None>` items in the client project only — IDE visibility; DataPacker compiles them, not MSBuild. Adding a shader means adding a `<None>` entry to the client vcxproj and filters.

## vcxproj.filters Rules

Filter paths mirror the on-disk directory structure under three roots:
- `Engine/Source/<path>` → filter `Engine\<path>`; shader `<None>` items → `Engine\Data\Shaders\<sub>`
- `Projects/BrokenEngineSandbox/Source/<path>` → filter `Game\<path>`
- `Common[/<subdir>]` → filter `Common[\<subdir>]`

Exception: generated `$(GameDataDirectory)\*.h` headers live in a flat `DataFiles` filter. New filters need a `<Filter Include>` entry with a unique GUID, and every ancestor filter must exist. The server filters file has an empty leftover filter (`Engine\Graphics`) — cruft, not a signal that such files belong in the server project.
