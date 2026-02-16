# Projects/BrokenEngineSandbox/Data/Shaders - Game Shader Configuration

Game-specific shader configuration headers that extend the engine's shader system.

## Files

### `ShaderLayouts.h`
Project-specific shader layout extensions. Includes `ShaderLayoutsBase.h` from engine. Provides the extension point for game-specific shader constants and struct additions.

## Architecture

This directory provides game-specific overrides and extensions to the engine's shader system. The engine's `ShaderLayoutsBase.h` is designed to be extended by game projects through this `ShaderLayouts.h` wrapper pattern.

## See Also

- [Engine/Data/Shaders/CLAUDE.md](../../../../Engine/Data/Shaders/CLAUDE.md) - Engine shader system
