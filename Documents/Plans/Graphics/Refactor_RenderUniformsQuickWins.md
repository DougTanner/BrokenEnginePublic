# Refactor: Render Uniforms & Graphics Root Quick Wins

## Context
Source: /external-refactor-clean on Engine/Source (recursive). Graphics root + Render uniforms batch. Headline: a shader capability silently disabled by a copy-paste (the spread-decay Start→End `mix()` is a permanent no-op). Hot-path allocation came back fully clean across all five per-frame uniforms files.

## Design

### Engine/Source/Graphics/Render/LightingUniforms.cpp — no-op End-uniform lerp
- `fSpreadDecayEnd`/`fSpreadAccumulationDecayEnd` (:157-158) are written from the *same* wrappers as the Start block (:142-143) — no `gSpreadDecayEnd`/`gSpreadAccumulationDecayEnd` wrapper exists (grep-verified), while every other End-block field has one; `LightingSpread.frag:67-68` interpolates Start→End for both — always a no-op. Either add the two End wrappers (restoring the per-pass ramp the shader is built for; + TweaksScreen sliders) or collapse the shader to single values and drop the two dead layout fields [~15m + decision]

### Engine/Source/Graphics/AnimationData.cpp
- Extract a `FindKeyframePair` template — the clamp-to-ends + `std::upper_bound` search block appears twice in `InterpolateKeyframes` (:223-244 cubic, :286-307 compact), identical except element type [~30m]
- `ASSERT` + `std::clamp` back-to-back (:340-341): ASSERT throws in all builds, so the clamp is unreachable on failure and a no-op on success — keep the clamp (graceful `FindAnimation` −1 recovery) and drop the ASSERT, or vice versa [~5m]
- Convert the topological-order check (:127-131) to `throw common::CorruptStreamException` — ASSERT's `std::runtime_error` skips the `catch (const CorruptStreamException&)` at :494, losing the per-CRC corruption log every other path gets [~5m]

### Engine/Source/Graphics/CameraBase.cpp
- `f4RenderVisibleArea` expansion (:85-88): `.z`/`.w` recompute `(y − w)` *after* `.y` was expanded, so east/bottom use a larger height than west/top — looks accidental. **User confirmation required** (tunables may be calibrated around it); if intended, comment; if not, hoist `fHeight` first [~15m + visual check]
- Optional: decompose the 143-line `CalculateMatricesAndVisibleArea` (:24-166) into its four documented phases (`SelectVisibleAreaLod`, `LatchQuadSize`, ...) — conservative move-only [~1h]

### Engine/Source/Graphics/Graphics.cpp
- Single-source the duplicated block-snap loop (`SnapToDetailBlock` helper) — `FullDetail` (:30-40) and `WaterFullDetail` (:62-72) each run the identical snap loop twice; the comment at :17 already says they "cannot drift apart" [~15m]
- One-line comment documenting the deliberate `vkDeviceWaitIdle` result swallow in `Destroy` (:590) — `CheckVkFailed` escalation is exactly wrong during teardown [~5m]

### Engine/Source/Graphics/Screenshot.cpp
- Check `GetTempPathW` (:63) and `stbi_write_jpg` (:70) results — opaque OS/third-party boundary; today the LOG at :69 claims success unconditionally. kWarning-tier (dev-only feature) [~5m]

### Engine/Source/Graphics/Render/GlobalUniforms.cpp
- Replace the cryptic residue markers `// !=`, `// ++`, `// Start offset` (:321-323, :333-335) with real words or delete [~5m]
- Read the `mShadowTexture` extent once in `PopulateShadowParameters` (:342-343 float, :374-375 int — one read + two casts) [~5m]

### Engine/Source/Graphics/Render/MainUniforms.cpp
- Drop the redundant re-clamp of `miVisibleAreaLod` (:336) — only ever assigned already-clamped (`CameraBase.cpp:105-106`); no other writer [~5m]
- Hoist the twice-defined `kdTwoPi` (:162, :233) to one file-scope `inline constexpr` [~5m]
- Derive the mapped `GlobalLayout` pointer once (:345, :426 — same `reinterpret_cast` expression) [~5m]

## Critical files
- `Engine/Source/Graphics/Render/LightingUniforms.cpp`, `GlobalUniforms.cpp`, `MainUniforms.cpp`
- `Engine/Source/Graphics/AnimationData.cpp`, `CameraBase.cpp`, `Graphics.cpp`, `Screenshot.cpp`
- `Engine/Data/Shaders/Lighting/LightingSpread.frag` + `Ui/LightingWrappersBase.{h,cpp}` / `TweaksScreenLighting.cpp` (if End wrappers are added)
- `Engine/Data/Shaders/ShaderLayoutsBase.h` (if the dead End fields are dropped)

## Out of scope
- The shadow/lighting area-snapping + temporal-latch dedup (`Architecture_ShadowLightingUniformDedup.md` — same files, land in one session or refresh)
- `fWaterZOffsetTemp` "DT: TEMP" chain (water in-flight this session — decision deferred to the water work)
- Monitor-refresh-rate per-monitor lookup (observation only; consumer impact unassessed)
- Gerstner bank per-frame recompute (verified partially necessary; below action bar)

## Notes
- Invariant exposure: client/graphics-only; no determinism/CRC/wire. The L1 item changes visuals if the ramp is restored (that's the point) — shader repack either way; C2 is behavior-affecting (visible-area size) and gated on user confirmation
- Grill decisions: (a) L1 — add End wrappers (recommended; the shader is built for the ramp) vs collapse; (b) C2 — intended asymmetry or bug; (c) whether to take the optional CameraBase decomposition
