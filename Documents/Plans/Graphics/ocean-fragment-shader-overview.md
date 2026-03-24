# Ocean Fragment Shader Rewrite - Overview

## Context

Rewrite `Water.frag` from a custom hacked-together solution to a physically-based ocean shader inspired by World of Warships (Bruneton's geometry-to-BRDF cascade). The camera views from kilometers above, looking down on islands - an RTS perspective. The existing Gerstner wave vertex shader (`Water.vert`) is kept as-is.

## Research

Full research document: `C:\Users\dougt\.claude\plans\concurrent-exploring-ladybug.md`

Primary reference: World of Warships (US Patent US10290142B2, Bruneton 2010)
User favorite visual reference: Ubisoft "Making Waves" HPG 2024

## Phase Summary

| Phase | Name | Impact | Dependencies |
|-------|------|--------|-------------|
| 1 | PBR Foundation | Largest change - new shading model | None |
| 2 | Multi-Octave Slope Mapping | Better normals + anti-tiling | Phase 1 |
| 3 | Foam and Whitecaps | Visible from altitude | Phase 1 |
| 4 | Sun Glitter | Sparkle effect | Phase 1 |
| 5 | Caustics | Shallow water light patterns | Phase 1 |
| 6 | Subsurface Scattering | Crest/trough color variation | Phase 1 |
| 7 | Legacy Technique Recovery | Re-integrate effective existing techniques | Phase 1 |

Phases 2-7 depend only on Phase 1. They are independent of each other and can be done in any order. Recommended order is as listed (highest visual impact first).

## Tweaks Screen

See `ocean-phase-tweaks-screen.md` for the complete Tweaks slider plan covering all phases.

## Files Modified (All Phases Combined)

- `Engine/Data/Shaders/Water/Water.frag` - Fragment shader (primary target)
- `Engine/Data/Shaders/ShaderLayoutsBase.h` - New uniform floats for tweakable parameters
- `Engine/Source/Ui/WrapperBase.h` - New Wrapper globals
- `Engine/Source/Ui/WrapperBase.cpp` - New Wrapper definitions
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` - Register new sliders
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h` - New render section declarations
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp` - Section toggle enum + registration
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterPBR.cpp` - New file (Phase 1)
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterNormals.cpp` - New file (Phase 2)
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterFoam.cpp` - New file (Phase 3)
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterGlitter.cpp` - New file (Phase 4)
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterCaustics.cpp` - New file (Phase 5)
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterSSS.cpp` - New file (Phase 6)
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` - Upload new uniform values
- `Engine/Source/Graphics/Render/LightingUniforms.cpp` - Upload new uniform values (if MainLayout)
- `.vcxproj` / `.vcxproj.filters` - Register new .cpp files

## Preserved Systems (Must Work Across All Phases)

- Shadow mapping: SmokeShadow(), shadowTextureSampler, objectShadowsTextureSampler
- Smoke integration: BlendSmoke() at base height
- Lighting system: ReadLighting(), SpecularLighting() at base height
- Terrain elevation early-out
- Water transparency alpha (terrain fade)
- Skybox sampling (simplified usage)
