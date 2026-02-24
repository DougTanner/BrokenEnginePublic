# Projects/BrokenEngineSandbox/Data/Shaders - Game Shader Configuration

## Overview

Game-specific shader layout header that extends the engine's shader system. All engine and game shaders include `ShaderLayouts.h` from this directory rather than the engine's `ShaderLayoutsBase.h` directly, providing a project-level extension point for game-specific constants and struct additions.

## Key Files

- **ShaderLayouts.h** - Project wrapper that includes the engine's base shader layouts. Currently a pass-through; game-specific shader constants and struct definitions would be added here.

## Architecture

The engine's dual-language header system (`ShaderLayoutsBase.h`) defines data structures compatible with both C++ and GLSL. This directory's `ShaderLayouts.h` acts as the indirection layer that all shaders reference, allowing each game project to inject its own constants and types without modifying engine code.

## See Also

- [Engine/Data/Shaders/CLAUDE.md](../../../../Engine/Data/Shaders/CLAUDE.md) - Engine shader system, GLSL source files, and shader subdirectory documentation
