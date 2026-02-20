# ThirdParty Static Library Project

Visual Studio 2026 project that compiles all third-party library source code into a single static library (`ThirdParty.lib`).

## Overview

Centralizes third-party code compilation into one static library linked by both the Engine and DataPacker projects. This avoids duplicating third-party compilation units across multiple projects and ensures consistent build settings for external code.

## Build Configuration

- **Output**: Static library (`.lib`) with per-configuration naming (`ThirdParty.Debug.lib`, etc.)
- **Configurations**: Debug, Profile, Release (x64 only)
- **Warning level**: All warnings disabled (`TurnOffAllWarnings`) since this is external code
- **Language standard**: C++23
- **Compiler flags**: `/bigobj` for large translation units, `/fp:strict` for deterministic floating point, no RTTI, async exceptions

## Source Organization

The project compiles source files from two locations using filters:

- **Engine** filter: `Engine/Source/ThirdParty/*.cpp` (DirectXTK, ImGui, Stb, Vma, Volk)
- **DataPacker** filter: `DataPacker/Source/ThirdParty/*.cpp` (bc7enc_rdo, cmft, DirectXTK, SPIRV-Cross, StackWalker, stb, tinygltf, tinyobjloader, openexr)
- **DataPacker\zlib** filter: `ThirdParty/zlib/*.c` (zlib compression library)

## Dependencies

Requires `VK_SDK_PATH` environment variable for Vulkan SDK headers.
