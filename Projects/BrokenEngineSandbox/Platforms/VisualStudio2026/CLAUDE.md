# BrokenEngineSandbox Visual Studio 2026 Projects

## Overview

Contains Visual Studio 2026 solution and project files for building BrokenEngineSandbox as either a client or server application. Both projects compile the same source files but with different preprocessor defines, producing separate executables.

## Client and Server Projects

- **BrokenEngineSandbox** (client): Defines `BT_CLIENT`. Builds the full game with graphics, audio, and input.
- **BrokenEngineSandboxServer** (server): Defines `BT_SERVER`. Builds a headless server without client-specific systems.

Each project has its own `.sln`, `.vcxproj`, and `.vcxproj.filters`. Both use `$(ProjectName)` in `IntDir` so their intermediate build artifacts go to separate directories, allowing simultaneous builds without conflicts. The server vcxproj excludes engine Graphics `.cpp` files entirely (except Islands, which provides terrain collision data needed by server-side physics).

## Build Configuration

- **Configurations**: Debug, Profile, Release (x64 only)
- **Floating point**: `/fp:strict` for deterministic math
- **Preprocessor**: `BT_ENGINE` plus either `BT_CLIENT` or `BT_SERVER`, plus configuration define (`BT_DEBUG`, `BT_PROFILE`, or `BT_RELEASE`)

## Conditional Compilation

Source code uses `BT_CLIENT` and `BT_SERVER` to conditionally compile features. The `Pch.h` header auto-defines `BT_CLIENT` if neither `BT_CLIENT` nor `BT_SERVER` is defined, so standalone compilation defaults to client mode. Code guarded by `BT_CLIENT` includes graphics rendering, audio playback, and input handling.

## vcxproj File Inclusion Rules

Each `.cpp` file belongs in the client vcxproj, server vcxproj, or both, based on its preprocessor guards:

- **Client-only**: Files fully wrapped in `#if defined(BT_CLIENT)` — include only in `BrokenEngineSandbox.vcxproj`
- **Server-only**: Files fully wrapped in `#if defined(BT_SERVER)` — include only in `BrokenEngineSandboxServer.vcxproj`
- **Shared**: Files with no guards or partial guards — include in both vcxproj files

Do not rely on preprocessor guards alone to exclude code — if a `.cpp` is entirely one-sided, remove it from the opposite vcxproj to avoid compiling empty translation units.

Headers (ClInclude) for client-only or server-only systems should also only appear in the matching vcxproj for IDE Solution Explorer cleanliness.

**Naming conventions that signal build affinity:**
- `*Render.cpp` — client-only (rendering)
- `Client*.cpp` in `Network/Client/` — client-only
- Game-layer `Server*.cpp` in `Network/Server/` — server-only
- Engine `Server.cpp`/`ServerReceive.cpp`/`ServerSend.cpp` — shared (client hosts a local server)
- Engine collection base files (`AreaLights.cpp`, `Billboards.cpp`, etc.) and their `*Update.cpp`/`*Render.cpp` — check guards; many are client-only

## vcxproj.filters Rules

Filter paths in `.vcxproj.filters` must mirror the on-disk directory structure:
- `Engine/Source/<path>/File.h` -> filter `Engine\<path>`
- `Projects/BrokenEngineSandbox/Source/<path>/File.h` -> filter `Game\<path>`
- `Common/File.h` -> filter `Common`

When adding a file in a subdirectory that doesn't have a filter yet, create a new `<Filter Include="...">` entry with a unique GUID in the filter definitions `<ItemGroup>`. Every ancestor filter must also exist.
