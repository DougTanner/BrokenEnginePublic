# Tech Debt: Dead Code & Unused Dependencies

Source: /external-tech-debt on Engine/Source/Graphics/Render

## Changes

### Engine/Source/Graphics/Render/GlobalUniforms.cpp
- Remove unused `#include "Input/Input.h"` (line 5) — no Input symbols referenced [~2m]
- Remove unused `#include "Profile/ProfileManager.h"` (line 6) — no Profile symbols referenced [~2m]
- Replace noop lerps on lines 72-73 (`XMVectorLerp(vecSunNoon, vecSunNoon, fLerp)`) with plain assignment `vecSun = vecSunNoon; vecAmbient = vecAmbientNoon;` [~2m]

### Engine/Source/Graphics/Render/MainUniforms.cpp
- Remove unused `#include "Profile/ProfileManager.h"` (line 5) — no Profile symbols referenced [~2m]
- Remove noop `std::pow(game::gpCamera->mfShake, 1.0f)` on line 60 — replace with just `game::gpCamera->mfShake` [~2m]

### Engine/Source/Graphics/Render/SmokeUniforms.cpp
- Remove dead `static XMFLOAT4 sf4SmokeArea {}` on line 10 and its write on line 71 (`sf4SmokeArea = rGlobalLayout.f4SmokeArea;`) — variable is written but never read [~2m]

### Engine/Source/Graphics/Render/Render.h + LightingUniforms.cpp
- Make `DirectionToDirectionMultipliers` file-static in LightingUniforms.cpp (remove declaration from Render.h line 23) — only used within LightingUniforms.cpp [~5m]

## Verification Notes
- All file paths and line numbers verified against source (2026-03-18)
- Confirmed no Input or Profile symbols used in GlobalUniforms.cpp or MainUniforms.cpp
- Confirmed `sf4SmokeArea` is write-only (written line 71, never read anywhere)
- Confirmed `DirectionToDirectionMultipliers` has zero callers outside LightingUniforms.cpp
- Noop lerp (noon-to-noon) and noop pow(x, 1.0f) both confirmed as identity operations
