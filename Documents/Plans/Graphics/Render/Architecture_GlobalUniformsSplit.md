# Architecture: GlobalUniforms Function Split

Source: /external-architecture-review on Engine/Source/Graphics/Render

## Changes

### Engine/Source/Graphics/Render/GlobalUniforms.cpp
- Extract sun color interpolation (lines 35-104) into a file-static helper `PopulateSunAndLighting(shaders::GlobalLayout& rGlobalLayout, float fSunAngle)` [~15m]
  - Computes vecSun, vecAmbient, fNoonPercent, fDayPercent based on sun angle
  - Returns or writes fDayPercent/fNoonPercent since they're used downstream (could use out params or a small struct)
- Extract shadow parameter setup (lines 106-225) into a file-static helper `PopulateShadowParameters(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent, float fNoonPercent)` [~15m]
  - Shadow feathering, sunrise/sunset stretch, elevation direction logic
- Extract terrain parameter setup (lines 227-258) into a file-static helper `PopulateTerrainParameters(shaders::GlobalLayout& rGlobalLayout, float fDayPercent, float fNoonPercent)` [~10m]
- Extract water parameter setup (lines 260-320) into a file-static helper `PopulateWaterParameters(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent)` [~10m]
  - Water depth, noise, sun visibility, fresnel, directional, wave steepness

This reduces RenderFrameGlobal from ~310 lines to ~40 lines of orchestration. All helpers are file-static (no header changes needed).

### Engine/Source/Graphics/Render/LightingUniforms.cpp
- Remove `[[maybe_unused]]` from `RenderLightingMain` parameter `rFrameInterpolate` (line 39) — either use the parameter or remove it from the signature [~5m]
  - If removing from signature, also update declaration in Render.h (line 25) and the call site in MainUniforms.cpp (line 22)

## Verification Notes
- All line ranges verified against GlobalUniforms.cpp source (2026-03-18): sun 35-104, shadow 106-225, terrain 227-258, water 260-322
- RenderFrameGlobal is ~310 lines, well above the 50-100 line function guideline
- `fDayPercent` and `fNoonPercent` are computed in the sun section and consumed by shadow, terrain, and water sections -- the split requires out-params or a small struct to pass these values
- `[[maybe_unused]]` on `rFrameInterpolate` in LightingUniforms.cpp:39 confirmed -- parameter is genuinely unused in the function body
- Call site for `RenderLightingMain` confirmed at MainUniforms.cpp:22, declaration at Render.h:25
