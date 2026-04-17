# Projects/BrokenEngineSandbox/Data/Shaders - Game Shader Configuration

Game-specific shader layout header extending the engine's shader system. All shaders include `ShaderLayouts.h` from this directory rather than the engine's `ShaderLayoutsBase.h`, providing a project-level extension point for game-specific constants and struct additions.

## Invariants

- **`BT_ENGINE` guards C++-only syntax**: defined only during C++ compilation, not GLSL. C++-only constructs (`#pragma once`, `#include`, `constexpr`) sit inside `#if defined(BT_ENGINE)`; GLSL-only syntax goes in `#else`.

## See Also

- [Engine/Data/Shaders/CLAUDE.md](../../../../Engine/Data/Shaders/CLAUDE.md) - Engine shader system and GLSL sources
