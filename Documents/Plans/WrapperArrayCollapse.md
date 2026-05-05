# Plan: Collapse Per-Target Wrapper Quartets Into Indexed Arrays

## Context

The per-target Sun/Moon Intensity slider work (see `~/.claude/plans/tweaks-menu-sun-moon-virtual-bentley.md`) introduced 8 nearly-identical `Wrapper` declarations: `gSunMoonSunIntensity{Terrain,Water,Objects,Smoke}` and `gSunMoonMoonIntensity{Terrain,Water,Objects,Smoke}`. The same 4-way fan-out repeats verbatim through six different files, producing 48 lines of mechanical copy-paste:

| File | Per-target lines |
|------|------------------|
| `Engine/Source/Ui/WrapperBase.h` | 8 declarations |
| `Engine/Source/Ui/WrapperBase.cpp` | 8 definitions |
| `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` | 8 map entries |
| `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenSunMoon.cpp` | 8 `WrapperSlider` calls |
| `Engine/Source/Graphics/Render/GlobalUniforms.cpp` | 8 `Wrapper.Get()` populations |
| `Engine/Data/Shaders/ShaderLayoutsBase.h` (`GlobalLayout`) | 8 scalar fields |

The same 4-way pattern will recur for any future per-target control (per-target Ambient, per-target Skybox, per-target Fog, etc.). Every quartet added compounds the cost.

## Approach

Introduce a single render-target enum and replace each scalar quartet with a 4-element array indexed by it.

```cpp
enum eRenderTarget : uint32_t { kTerrain, kWater, kObjects, kSmoke, kRenderTargetCount };

// WrapperBase.h
extern Wrapper gSunMoonSunIntensity[kRenderTargetCount];
extern Wrapper gSunMoonMoonIntensity[kRenderTargetCount];

// GlobalLayout (ShaderLayoutsBase.h) - scalar block layout permits arrays
float fSunIntensity[kRenderTargetCount] INIT;
float fMoonIntensity[kRenderTargetCount] INIT;
```

Each duplication site collapses to a `for (uint32_t i = 0; i < kRenderTargetCount; ++i)` loop driven by a parallel `kRenderTargetNames[]` table (`{"Terrain", "Water", "Objects", "Smoke"}`) for slider labels.

Shader read sites use `globalLayout.fSunIntensity[kTerrain]` etc. — no glsl loop needed; the index is a compile-time constant per shader.

## Files to Modify

- `Engine/Source/Ui/WrapperBase.h` / `.cpp` — array declarations + loop-based init
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` — loop-driven entry insertion (or move the 8 entries into a new `TweaksSliderMapSunMoon.cpp` per the companion `TweaksSliderMapReduce.md` plan)
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenSunMoon.cpp` — loop over `kRenderTargetNames`
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` — single loop populates both arrays
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — `float[4]` arrays in `GlobalLayout`
- All shader read sites in `ShaderFunctions.h`, `Water.frag`, `Model.frag` — switch to `[kTarget]` indexing
- New tiny header (e.g. `Engine/Source/Graphics/RenderTarget.h`) for the `eRenderTarget` enum + `kRenderTargetNames` table, shared by C++ and (via a parallel `#define` set) GLSL

## Risks / Open Questions

- **KISS/YAGNI tension**: Eight wrappers exist today; one future quartet (12 wrappers) is the break-even where the abstraction cost is paid back. If no additional quartets land, the array refactor is a net loss. Decision should be deferred until a second per-target control is actually proposed — file this plan as the implementation recipe to use *when* that happens.
- **Wrapper declaration order constraint**: `Engine/Source/Ui/Screens/TweaksScreen/CLAUDE.md` requires wrapper declaration order to match UI render order. An array enforces this naturally — index `kTerrain=0` means terrain renders first in the slider list — which is a small but real win.
- **Slider label uniqueness**: Map keys must remain globally unique. Loop generation must concatenate (e.g., `"Sun " + kRenderTargetNames[i]` -> `"Sun Terrain"`) to preserve the existing label scheme exactly.
- **GLSL parity**: GLSL has no shared header with C++ enums today. Either define `kTerrain`/`kWater`/etc. as `#define` constants in a `.h` included by both, or hard-code the literal indices in the shader read sites and rely on a comment to document the mapping. Pick whichever the engine already does for similar shared constants.
- **Scalar block layout for arrays**: `GL_EXT_scalar_block_layout` packs `float[4]` identically to four loose floats, so the GPU memory footprint is unchanged. Verify CPU `INIT` macro handles array members or extend it.
- **Naming cleanup**: With the array form, `gSunMoonSunIntensity` reads awkwardly (`SunMoon` + `Sun` is redundant). Consider renaming to `gSunIntensity[kTerrain]` / `gMoonIntensity[kTerrain]` while doing the refactor, since every reference is changing anyway.
