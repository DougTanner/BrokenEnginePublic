# BrokenEngineSandbox Visual Studio 2026 Projects

## Overview

Contains Visual Studio 2026 solution and project files for building BrokenEngineSandbox as either a client or server application. Both projects compile the same source files but with different preprocessor defines, producing separate executables.

## Client and Server Projects

- **BrokenEngineSandbox** (client): Defines `BT_CLIENT`. Builds the full game with graphics, audio, and input.
- **BrokenEngineSandboxServer** (server): Defines `BT_SERVER`. Builds a headless server without client-specific systems.

Each project has its own `.sln`, `.vcxproj`, and `.vcxproj.filters`. Both use `$(ProjectName)` in `IntDir` so their intermediate build artifacts go to separate directories, allowing simultaneous builds without conflicts.

## Build Configuration

- **Configurations**: Debug, Profile, Release (x64 only)
- **Floating point**: `/fp:strict` for deterministic math
- **Preprocessor**: `BT_ENGINE` plus either `BT_CLIENT` or `BT_SERVER`, plus configuration define (`BT_DEBUG`, `BT_PROFILE`, or `BT_RELEASE`)

## Conditional Compilation

Source code uses `BT_CLIENT` and `BT_SERVER` to conditionally compile features. The `Pch.h` header auto-defines `BT_CLIENT` if neither `BT_CLIENT` nor `BT_SERVER` is defined, so standalone compilation defaults to client mode. Code guarded by `BT_CLIENT` includes graphics rendering, audio playback, and input handling.
