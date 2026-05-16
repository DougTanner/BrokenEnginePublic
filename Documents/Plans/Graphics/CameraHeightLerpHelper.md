# Camera-Height Clamped-Lerp Helper — DRY Three Call Sites

## Context

Three render-uniform population sites in `Engine/Source/Graphics/Render/` now hand-roll the same clamped-linear-blend over `game::gpCamera->mfCameraEyeHeight`. The pattern is documented under "Camera-Height-Conditional Uniforms" in `Engine/Source/Graphics/Render/CLAUDE.md` but has no shared implementation.

1. `LightingUniforms.cpp:71-77` — `RenderLightingGlobal`, four user-tweakable Wrappers (`gSpreadDistanceEndStartHeight`, `gSpreadDistanceEndEndHeight`, `gSpreadDistanceEndLow`, `gSpreadDistanceEndHigh`); guards `fSpan = std::max(end - start, 0.001f)`, clamps `fT` to `[0,1]`, writes `rGlobalLayout.fSpreadDistanceEnd`.
2. `LightingUniforms.cpp:124-127` — `RenderLightingMain`, hard-coded endpoints `kfCameraEyeHeightDefault` and `2.0f * kfCameraEyeHeightDefault`, then `fCameraHeightZoomFactor = 1.0f - fWaveAmplitudeScale` written to `rMainLayout`. Note the inverted direction: high-altitude maps to `1.0`, default-altitude to `0.0`.
3. `GlobalUniforms.cpp:399-402` — byte-identical block to #2, producing a local `fCameraHeightZoomFactor` consumed by the water-speed lerp.

Sites #2 and #3 emit `1 - t` rather than the lerped target. Passing `fLow = 1.0f, fHigh = 0.0f` to a single `lerp(low, high, t)` helper yields the same scalar — no separate variant is needed. The `0.001f` floor in #1 must be preserved so authors who set `EndHeight <= StartHeight` in the live wrappers do not divide by zero.

## Design

Add a free function to `Engine/Source/Graphics/Render/Render.h` (the pattern is render-uniform-population-specific per the CLAUDE.md section, and `Render.h` is already the shared header for those TUs):

```
float CameraHeightLerp(float fStartHeight, float fEndHeight, float fLow, float fHigh);
```

Implementation (place in a new `Render.cpp`, or in `LightingUniforms.cpp` if a `.cpp` for `Render.h` does not yet exist — verify at execution time):

```
float CameraHeightLerp(float fStartHeight, float fEndHeight, float fLow, float fHigh)
{
    const float fSpan = std::max(fEndHeight - fStartHeight, 0.001f);
    const float fT = std::clamp((game::gpCamera->mfCameraEyeHeight - fStartHeight) / fSpan, 0.0f, 1.0f);
    return std::lerp(fLow, fHigh, fT);
}
```

Migration:

1. Site #1 — `rGlobalLayout.fSpreadDistanceEnd = engine::CameraHeightLerp(gSpreadDistanceEndStartHeight.Get(), gSpreadDistanceEndEndHeight.Get(), gSpreadDistanceEndLow.Get(), gSpreadDistanceEndHigh.Get());`
2. Site #2 — `rMainLayout.fCameraHeightZoomFactor = engine::CameraHeightLerp(game::Camera::kfCameraEyeHeightDefault, 2.0f * game::Camera::kfCameraEyeHeightDefault, 0.0f, 1.0f);` (inversion folded into low/high arguments, the `kfWaveFadeEnd` constant and `fWaveAmplitudeScale` temporary both delete).
3. Site #3 — same call shape as #2, assigned to the local `fCameraHeightZoomFactor`.

Behavior identical; net code shrinks ~15 lines.

## Critical files

- `Engine/Source/Graphics/Render/Render.h` — declare `CameraHeightLerp` in `namespace engine`.
- `Engine/Source/Graphics/Render/Render.cpp` (new) or fold into an existing `Render/*.cpp` — definition needs `game::gpCamera` from `Game.h`.
- `Engine/Source/Graphics/Render/LightingUniforms.cpp` — `RenderLightingGlobal` spread-distance-end block and `RenderLightingMain` zoom-factor block.
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` — water-speed zoom-factor block in the water precision section.
- `Engine/Source/Graphics/Render/CLAUDE.md` — update "Camera-Height-Conditional Uniforms" to point at the helper.

## Out of scope

- Promoting the helper to `Common/` — it depends on `game::gpCamera`, a render-time singleton; nothing in `Common/` references game globals.
- A separate inverted-direction variant (`1 - t`) — folded into the `fLow`/`fHigh` arguments instead.
- Touching the GPU-side uniform layouts or shaders — pure CPU refactor.
- Auditing other camera-height-driven code (e.g., audio attenuation, particle culling) for the same pattern. Scope is the three render-uniform sites.

## Notes

- Effort 1, Impact 2, Risks 1, Score 0. Tier Quick Win.
- If `Render.h` already gains a `.cpp` from another in-flight plan, fold the definition there rather than creating a fresh TU.
