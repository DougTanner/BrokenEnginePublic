# ThirdParty Visual Studio 2026 Project

## Overview

Visual Studio 2026 solution and project that compile all third-party library source into a single static library (`ThirdParty.<Config>.lib`), linked by both the Engine and DataPacker. Centralizing here avoids duplicating external compilation units across projects and guarantees consistent build settings for code we do not modify. The `Output/` and `Build/` directories hold build artifacts, not committed binaries.

## Build Configuration

- **Output**: Static library, per-configuration `TargetName` (`ThirdParty.Debug`, `ThirdParty.Profile`, `ThirdParty.Release`) in `Output/`; intermediates in `Build/<Config>/`
- **Configurations**: Debug, Profile, Release (x64 only)
- **Compiler**: C++23, `/fp:strict` (deterministic math, matching Engine/Game), RTTI off, `/EHa`, OpenMP, `/bigobj`, `/Zc:__cplusplus`, no PCH, all warnings disabled (external code), MultiByte charset
- **Defines**: `USING_XINPUT`, `VK_NO_PROTOTYPES`, `VK_USE_PLATFORM_WIN32_KHR`
- **Requires**: `VK_SDK_PATH` environment variable for Vulkan SDK headers

## Source Organization

The `.vcxproj` filters split units by consumer:

- **Engine** filter — runtime dependencies (e.g. DirectXTK, ImGui, Vma, Volk, Mimalloc, Lz4, Enet, Clipper2)
- **DataPacker** filter — offline asset-pipeline dependencies (e.g. bc7enc_rdo, cmft, SPIRV-Cross, tinygltf, tinyobjloader, meshoptimizer, openexr), with zlib under a `DataPacker\zlib` sub-filter

Compilation units come from two roots: thin wrapper `.cpp`/`.c` files under `ThirdParty/Prebuilts/Source/{Engine,DataPacker}/`, and upstream sources referenced directly in their library directories (e.g. `bc7enc_rdo/`, `zlib/`).

When adding a new library, place a wrapper under the appropriate `Source/Engine/` or `Source/DataPacker/` directory (or reference its upstream file), register it in both `ThirdParty.vcxproj` and `ThirdParty.vcxproj.filters`, and add any new include path to the `AdditionalIncludeDirectories` of all three configurations. See the parent [ThirdParty/CLAUDE.md](../../../CLAUDE.md) for the license policy and the `lz4/lib/**`-only caveat — never reference files outside `lz4/lib/` from this project.
