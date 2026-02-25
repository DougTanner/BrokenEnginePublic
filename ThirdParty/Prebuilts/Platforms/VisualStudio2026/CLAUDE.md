# ThirdParty Static Library Project

Visual Studio 2026 project that compiles all third-party library source code into a single static library (`ThirdParty.lib`).

## Overview

Centralizes third-party code compilation into one static library linked by both the Engine and DataPacker projects. This avoids duplicating third-party compilation units across multiple projects and ensures consistent build settings for external code. All warnings are disabled since this is external code we do not modify.

## Build Configuration

- **Output**: Static library (`.lib`) with per-configuration naming (`ThirdParty.Debug.lib`, etc.)
- **Configurations**: Debug, Profile, Release (x64 only)
- **Floating point**: Uses `/fp:strict` for deterministic math, matching the Engine and Game projects

## Source Organization

The project compiles source files from `ThirdParty/Prebuilts/Source/` using filters that separate Engine and DataPacker dependencies:

- **Engine** filter: Runtime libraries (graphics, audio, memory, GPU allocation, LZ4 compression, ENet networking)
- **DataPacker** filter: Asset pipeline libraries (texture compression, model loading, shader cross-compilation, image formats)
- **DataPacker\zlib** filter: zlib compression (compiled from `ThirdParty/zlib/*.c` source directly)

When adding a new third-party library, add a compilation unit under the appropriate `Source/Engine/` or `Source/DataPacker/` directory and register it in both `ThirdParty.vcxproj` and `ThirdParty.vcxproj.filters`.

## Dependencies

Requires `VK_SDK_PATH` environment variable for Vulkan SDK headers.
