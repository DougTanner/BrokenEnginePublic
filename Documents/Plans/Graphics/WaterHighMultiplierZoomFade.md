# Water High-Frequency Normal-Map Fade at Zoom-Out

## Context

We just shipped a CPU-side camera-height fade for the geometric Gerstner wave amplitudes (low + medium bands) in `Engine/Source/Graphics/Render/MainUniforms.cpp::RenderFrameMain()`: `fWaveAmplitudeScale` linearly ramps from 1.0 at `game::Camera::kfCameraEyeHeightDefault` (150) down to 0.0 at 2x default (300), and is multiplied into `pf4LowWavesTwo[i].y` and `pf4MediumWavesTwo[i].y`.

The geometric component is now flat at extreme zoom-out, but the high-frequency surface detail comes from a **fragment-shader normal-map blend** governed by `fWaterHighMultiplier`, `fWaterHighScaleOne`, `fWaterHighScaleTwo` (set in `Engine/Source/Graphics/Render/GlobalUniforms.cpp` lines ~289–291 from CVars `gHighMultiplier`, `gHighScaleOne`, `gHighScaleTwo`). At 300+ eye height the geometry is glassy but the normal map keeps its full per-pixel slope, so specular highlights still shimmer and alias against the now-flat surface — the visual mismatch is the most likely remaining artifact at far zoom.

## Approach

Apply a parallel CPU-side fade to `fWaterHighMultiplier` driven by the same camera-height ramp, written into `GlobalUniforms.cpp::RenderFrameGlobal()` (or the equivalent function that already populates the high-frequency CVars). Open questions for that plan:

- Same ramp endpoints (150 → 300) or a wider range so normal-map detail lingers a bit past the geometric fade-out and avoids a sharp visual transition?
- Linear vs smoothstep — geometry uses linear; normal-map intensity is more visually nonlinear (specular highlights), so smoothstep might read better.
- Should `fWaterHighScaleOne` / `fWaterHighScaleTwo` (which control normal-map UV scale, i.e. detail frequency) also fade, or only the blend `Multiplier`? Letting the multiplier alone do the work is the simplest path.

## Files Likely to Change

- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` — write `rGlobalLayout.fWaterHighMultiplier = gHighMultiplier.Get() * fNormalFadeScale;`
- Reuses `game::Camera::kfCameraEyeHeightDefault` and `game::gpCamera->mfCameraEyeHeight` — both already public.
- No shader changes needed; multiplier is already a CPU-supplied uniform.

## Reused Symbols

- `game::Camera::kfCameraEyeHeightDefault` (`Projects/BrokenEngineSandbox/Source/Graphics/Camera.h`) — anchor for the fade-start.
- `game::gpCamera->mfCameraEyeHeight` — current camera eye height.
- `Wrapper gHighMultiplier` — existing CVar source for the base value.

## Why This Is a Separate Plan

The current change was scoped explicitly to "low and medium" geometric waves. Bundling normal-map fade would have:
- Mixed CPU-side scope across two render files (`MainUniforms.cpp` and `GlobalUniforms.cpp`).
- Required tuning the curve shape independently — geometric and specular respond differently to linear/smoothstep falloffs.
- Risked over-darkening water at intermediate heights if both fade together too aggressively.

Best treated as a follow-up after the geometric fade is locked in and visually evaluated at zoom levels in the 250–400 range.
