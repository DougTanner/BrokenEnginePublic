# Projects/BrokenEngineSandbox/Data/Shaders - Game Shader Configuration

Game-specific shader configuration headers that extend the engine's shader system.

## Files

### `ShaderLayouts.h`
Project-specific shader layout extensions. Includes `ShaderLayoutsBase.h` from engine and defines `kiMaxTextureCount` for the game's texture descriptor array sizing. Fragment shaders use this constant to declare their texture array size. Texture array indices are assigned lazily at runtime by `CrcToIndex()` in TextureManager rather than at compile time.

## Architecture

This directory provides game-specific overrides and extensions to the engine's shader system. The engine's `ShaderLayoutsBase.h` is designed to be extended by game projects through this `ShaderLayouts.h` wrapper pattern.

## See Also

- [Engine/Data/Shaders/CLAUDE.md](../../../../Engine/Data/Shaders/CLAUDE.md) - Engine shader system
