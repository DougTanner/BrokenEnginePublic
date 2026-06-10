# BrokenEngineSandbox Visual Studio 2026 Projects

## Overview

MSBuild project layer for the game: client and server `.vcxproj`s compile the same source tree (`Common/`, `Engine/Source/`, game `Source/`, generated `Output/Data` headers) with different defines. Each project has its own single-project `.sln` — building one never builds the other. `Build/`, `Output/`, `Temp/` are build artifacts.

## Client and Server Projects

- **BrokenEngineSandbox** (client): defines `BT_ENGINE;BT_CLIENT` plus the config define (`BT_DEBUG`/`BT_PROFILE`/`BT_RELEASE`)
- **BrokenEngineSandboxServer**: same but `BT_SERVER`

Both use `$(ProjectName)` in `IntDir`, so client and server builds can run simultaneously without trampling each other's intermediates. The server vcxproj contains no engine `Graphics/`, `Audio/`, or input `.cpp` files; it still compiles the shared Frame collections, terrain/collision/nav (`IslandTerrain`, `Collision`, `NavBuild`, `NavQuery`), and the Tweaks `*WrappersBase` layer (tweak values participate in the deterministic sim) — it is not a minimal physics binary.

## Build Configuration

- **Configurations**: Debug, Profile, Release (x64 only)
- **Compiler**: C++23, `Pch.h` force-included, `/fp:strict` in every config of both projects (cross-binary float determinism for client reconciliation/replay — see `Documents/FloatingPointDeterminism.txt`). Compile settings are mirrored between the two vcxprojs: when changing one, change the other.
- **PreBuildEvent**: builds the ThirdParty static lib and `DataPacker.exe` only if missing — existence check, not freshness; stale artifacts need a manual rebuild. DataPacker is always built Release regardless of game config. See [ThirdParty Prebuilts CLAUDE.md](../../../../ThirdParty/Prebuilts/Platforms/VisualStudio2026/CLAUDE.md).
- **CustomBuildStep**: runs DataPacker against `Engine/Data` + game `Data` into `Output/Data` (packed assets + generated headers, listed as `ClInclude`). Declared always out-of-date on purpose — DataPacker runs every build and does its own incremental checking.
- **Clang-Tidy**: server enables it in all configs; client in Debug and Release (off in Profile).

## vcxproj File Inclusion Rules

Each `.cpp` belongs in the client vcxproj, server vcxproj, or both, matching its preprocessor-guard scope (root rule). Shared files (no guards or partial guards) go in both; entirely one-sided files must be removed from the opposite vcxproj — guards alone leave empty translation units compiling there.

Headers follow the same affinity (Solution Explorer cleanliness; `ClInclude` has no build effect). The server omits the client-only generated `Output\Data\Shader.h`.

**Naming conventions that signal build affinity:**
- `*Render.cpp` — client-only
- `Network/Client/Client*.cpp` — client-only
- Game-layer `Network/Server/Server*.cpp` — server-only
- Engine `Server.cpp`/`ServerReceive.cpp`/`ServerSend.cpp` — shared (client hosts a local server)
- Engine collection files — check guards; many are client-only

**Shader sources** (`Engine/Data/Shaders/**`) are `<None>` items in the client project only — IDE visibility; DataPacker compiles them, not MSBuild. Adding a shader means adding a `<None>` entry to the client vcxproj and filters.

## vcxproj.filters Rules

Filter paths mirror the on-disk directory structure under three roots:
- `Engine/Source/<path>` → filter `Engine\<path>`; shader `<None>` items → `Engine\Data\Shaders\<sub>`
- `Projects/BrokenEngineSandbox/Source/<path>` → filter `Game\<path>`
- `Common[/<subdir>]` → filter `Common[\<subdir>]`

Exception: generated `Output\Data\*.h` headers live in a flat `DataFiles` filter. New filters need a `<Filter Include>` entry with a unique GUID, and every ancestor filter must exist. The server filters file has a few empty leftover filters (`Engine\Graphics`, `Engine\Input`) — cruft, not a signal that such files belong in the server project.
