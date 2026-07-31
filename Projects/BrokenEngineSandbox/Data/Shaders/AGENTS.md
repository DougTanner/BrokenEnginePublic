# Projects/BrokenEngineSandbox/Data/Shaders - Game Shader Layout Wrapper

Project-level extension point for the engine's shader layout system. The sole file, `ShaderLayouts.h`, is the per-project wrapper nearly every shader includes; it currently just forwards to the engine's `ShaderLayoutsBase.h` and adds nothing game-specific yet. Game-only shader constants, or struct additions written once in a form both C++ and GLSL compile, go after the include, so engine definitions are in scope.

## Why this indirection exists

Nearly every shader (engine and game) writes `#include "ShaderLayouts.h"`. The DataPacker passes both the engine and game `Data/Shaders` dirs as include roots, but only the game dir contains a file named `ShaderLayouts.h` (the engine ships `ShaderLayoutsBase.h`), so the unqualified include always resolves here regardless of include-search order. This wrapper then pulls in the engine base via a relative path. C++ resolves the same way: the game `Data` dir is on the vcxproj include paths and the game precompiled header includes this file, so anything added here is visible to both GLSL and C++ with no extra wiring. The seam lets the game inject layout extensions without forking engine shaders.

## Invariants

- `BT_ENGINE` guards C++-only syntax: defined only during C++ compilation, not GLSL. C++-only constructs (`#pragma once`, `constexpr`) sit inside `#if defined(BT_ENGINE)`; GLSL-only syntax goes in `#else`. Anything added to this wrapper must observe the same split.

## See Also

- `../../../../Engine/Data/Shaders/AGENTS.md` - Engine shader system, shared GLSL includes, and all shader-family sources
