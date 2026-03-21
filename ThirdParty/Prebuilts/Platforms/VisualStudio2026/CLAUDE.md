# ThirdParty Static Library Project

Visual Studio 2026 project that compiles all third-party library source code into a single static library (`ThirdParty.lib`).

## Purpose

Centralizes third-party code compilation into one static library linked by both the Engine and DataPacker projects. This avoids duplicating third-party compilation units across multiple projects and ensures consistent build settings for external code. All warnings are disabled since this is external code we do not modify.

## Build Configuration

- **Output**: Static library (`.lib`) with per-configuration naming (`ThirdParty.Debug.lib`, etc.)
- **Configurations**: Debug, Profile, Release (x64 only)
- **Floating point**: Uses `/fp:strict` for deterministic math, matching the Engine and Game projects
- **Requires**: `VK_SDK_PATH` environment variable for Vulkan SDK headers

## Source Organization

The project compiles source files from `ThirdParty/Prebuilts/Source/` using filters that separate Engine and DataPacker dependencies. DataPacker dependencies currently include meshoptimizer (offline mesh optimization). When adding a new third-party library, add a compilation unit under the appropriate `Source/Engine/` or `Source/DataPacker/` directory and register it in both `ThirdParty.vcxproj` and `ThirdParty.vcxproj.filters`.
