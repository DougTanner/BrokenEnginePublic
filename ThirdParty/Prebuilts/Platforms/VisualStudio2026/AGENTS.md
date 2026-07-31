# ThirdParty Visual Studio 2026 Project

## Overview

Solution and project that compile all compiled third-party library source into a single static library (`ThirdParty.<Config>.lib`) linked by the client, server, and DataPacker. Centralizing here avoids duplicating external compilation units across projects and guarantees one consistent flag set for code we do not modify. `Output/` and `Build/` hold build artifacts, not committed binaries.

## Build Configuration

- Output: Static library, per-configuration `TargetName` (`ThirdParty.Debug`, `ThirdParty.Profile`, `ThirdParty.Release`) in `Output/`; intermediates in `Build/<Config>/`
- Configurations: Debug, Profile, Release (x64 only)
- Compiler: C++23, `/fp:strict` (deterministic math, matching Engine/Game), static CRT (`/MTd` Debug, `/MT` otherwise — matches the executables), `/GL` in Profile/Release (codegen defers to the consumer's LTCG link), RTTI off, `/EHa`, OpenMP, `/bigobj`, `/Zc:__cplusplus`, no PCH, all warnings disabled (external code), MultiByte charset
- Defines: `USING_XINPUT`, `VK_NO_PROTOTYPES`, `VK_USE_PLATFORM_WIN32_KHR`
- Requires: `VK_SDK_PATH` environment variable for Vulkan SDK headers

## Consumer Provisioning

Supported wrapper scripts link stable primary submodule trees and primary `Output/` into linked worktrees. Consumer PreBuildEvents provision and require `Output/ThirdParty.$(Configuration).lib`; they never build ThirdParty. Primary Debug, Profile, and Release libraries must remain nonempty while worktrees are active. Wrapper session start (`.agents/scripts/Bootstrap-AgentTools.ps1`) incremental-rebuilds all three primary configurations so they track primary HEAD; beyond that, rebuild only as explicit primary maintenance.

## Source Organization

The `.vcxproj` filters split units by consumer (`Engine`, `DataPacker`, `DataPacker\zlib`), following the source root each unit comes from. Compilation units come from two roots: unity wrapper `.cpp`/`.c` files under `ThirdParty/Prebuilts/Source/{Engine,DataPacker}/`, and upstream sources referenced directly in their library directories (bc7enc_rdo, zlib).

When adding a new library:

1. Place a wrapper under `Source/Engine/` or `Source/DataPacker/` (or reference the upstream file directly) and register it in both `ThirdParty.vcxproj` and `ThirdParty.vcxproj.filters`.
2. Add any new include path to `AdditionalIncludeDirectories` in all three configurations — there is no shared `.props` sheet.
3. If the wrapper filename matches a unit in the other source root (both roots have a `DirectXTK.cpp` and an stb wrapper), set `<ObjectFileName>$(IntDir)Engine\</ObjectFileName>` on the Engine unit — the flat `IntDir` otherwise produces colliding `.obj` names.
