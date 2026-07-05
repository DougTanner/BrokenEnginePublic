# Refactor: Split Water Uniform Population out of GlobalUniforms.cpp

## Context

`Engine/Source/Graphics/Render/GlobalUniforms.cpp` (669 lines) mixes the day-cycle / sun-moon / shadow / terrain global-uniform population with a self-contained water-uniform block. The `Render/` directory otherwise follows a per-subsystem split (`LightingUniforms.cpp`, `WindUniforms.cpp`, `SmokeUniforms.cpp`); the water block is the one subsystem still living inside the shared `GlobalUniforms.cpp`.

Flagged **optional / RECOMMEND (not required)** by the 2026-07 repo-code-review — the file is cohesive and *under* the 1000-line hard threshold, so this is debt reduction to match the sibling convention, not a bug fix. The water block is a clean responsibility group with no callers outside `PopulateWaterParameters`, which `RenderFrameGlobal` invokes exactly once per frame.

## Design

Move the water responsibility group verbatim into a new sibling TU `Engine/Source/Graphics/Render/WaterUniforms.cpp`. Functions to move (byte-identical bodies — no logic change):

- `PopulateWaterSunsetFade` — water depth-LUT sunset fade (the review's approximate `~432` start actually begins here, one function earlier; it is part of the group).
- `PopulateWaterDirectional` — water directional term.
- `PopulateWaterReducedUv` — camera-relative UV reduction. **Carries its function-local `static double sdReducedTimeOne{X,Y}` / `Two{X,Y}` / `Three{X,Y}` and `static float sfPrevElapsedTime` accumulators**; they move *inside* the function and stay function-local statics — do NOT promote them to `Render.h` `inline` globals (Render `CLAUDE.md`: single-file latches stay function-local). The once-per-frame latch invariant is preserved because the sole caller `PopulateWaterParameters` is still called exactly once per frame by `RenderFrameGlobal`.
- `PopulateWaterParameters` — the group entry point.

**Entry-point wiring.** `PopulateWaterParameters` becomes the one non-static function; declare it in `Render.h` (engine namespace, inside the existing `BT_CLIENT` span) alongside the sibling `Render*Global` entries. Keep its current signature `(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent)` so `RenderFrameGlobal`'s call site stays byte-identical:

```
PopulateWaterParameters(rGlobalLayout, fSunAngle, fDayPercent);
```

`RenderFrameGlobal` already holds the mapped `rGlobalLayout` at that point (mapped once near the top of the function), so passing the ref avoids a redundant re-map. The other three functions stay file-static in `WaterUniforms.cpp`.

**New file structure** mirrors `WindUniforms.cpp`: whole-file `#if defined(BT_CLIENT)` wrap, `#include "Render.h"`, then the water block's include needs — at minimum `Graphics/Camera.h` (`game::gpCamera`, `game::Camera::kfCameraEyeHeightDefault` / `kfWaveFadeEndHeight`, and the free `engine::LerpAtHeight`), `Ui/WaterWrappersBase.h`, and `Ui/LightingWrappersBase.h` (the `gLightingSampledNormals*` wrappers `PopulateWaterReducedUv` reads). Include exactly the block's transitive needs and let the client build confirm; no new `WaterUniforms.h` (declarations live in `Render.h`, matching the siblings).

**`Render.h`.** Add the `PopulateWaterParameters` declaration. If `shaders::GlobalLayout` is not already visible there, forward-declare `namespace shaders { struct GlobalLayout; }` (the sibling `Render*Global` entries sidestep this by taking only `iCommandBuffer` — see the Notes decision).

**`GlobalUniforms.cpp` cleanup after the move.** Remove the now-unused `#include "Ui/WaterWrappersBase.h"` — but only after verifying no `gWater*` reference remains outside the moved block (the `gLightingWaterSkybox*` reads in `PopulateSunAndLighting` are *Lighting* wrappers, not Water; `common::kfUnderwaterMaskThresholdMeters` in `RenderFrameGlobal` is a Common constant, not a wrapper).

**vcxproj.** Add `WaterUniforms.cpp` to the **client** `.vcxproj` + `.vcxproj.filters` only (client-only per the Render directory convention: whole-file `BT_CLIENT` wrap + client-vcxproj membership is the established complementary guard). Use `/update-vcxproj` add mode. Never add it to the server vcxproj.

## Critical files

- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` — remove the four water functions + the unused `WaterWrappersBase.h` include; `RenderFrameGlobal` call site unchanged.
- `Engine/Source/Graphics/Render/WaterUniforms.cpp` — NEW; receives the moved block.
- `Engine/Source/Graphics/Render/Render.h` — add the `PopulateWaterParameters` declaration (+ `shaders::GlobalLayout` forward-decl if needed).
- Client `.vcxproj` + `.vcxproj.filters` — add the new TU via `/update-vcxproj`.

## Out of scope

- **No behavior change** — bodies move verbatim; values written to `GlobalLayout` must be bit-identical.
- **No uniform-layout change** — `shaders::GlobalLayout` / `ShaderLayoutsBase.h` untouched; no shader repack.
- **Do not touch the non-water populate functions** — `PopulateSunMoonDirection`, `PopulateDayCycleColors`, `PopulateDayCycleFeatherWindows`, `PopulateSunAndLighting`, `PopulateShadowStretch` / `PopulateShadowArea` / `PopulateShadowSunExtension` / `PopulateShadowParameters`, `PopulateTerrainParameters`, and the `ComputeNight*` / `ComputeMoonAmount` helpers stay put in `GlobalUniforms.cpp` (logic and citations unchanged).
- **No refactor of the water math itself** — the rotation / double-precision `fmod` reduction invariants are relocated as-is, not revisited.
- **Not a `/reduce-file` multi-way split** — one cohesive group to one sibling; the remaining ~470 lines of `GlobalUniforms.cpp` stay as one file.
- **No server-side change** — the file is client-only.

## Acceptance criteria

- Client builds; `WaterUniforms.cpp` compiles under the client vcxproj and is absent from the server vcxproj.
- `RenderFrameGlobal` still calls `PopulateWaterParameters(rGlobalLayout, fSunAngle, fDayPercent)` with the identical argument list, at the same point in the sequence.
- No `gWater*` reference remains in `GlobalUniforms.cpp`; its `Ui/WaterWrappersBase.h` include is gone.

## Notes

- **Determinism / invariant exposure: none.** `RenderFrameGlobal` and the water population are render-side uniform population (client-only, explicitly outside the CRC / PostRender determinism path). No `kiVersion` / `.pack` / wire / replay / allocation-tracked exposure. The only correctness-relevant invariant is the once-per-frame call of `PopulateWaterReducedUv` (preserved — the accumulators move inside the function and its sole caller's cadence is unchanged).
- **Open decision for `/external-grill-plan`** (single) — entry-point signature style:
  - **(A, recommended)** keep `PopulateWaterParameters(shaders::GlobalLayout&, float fSunAngle, float fDayPercent)`, declared in `Render.h` (with a `shaders::GlobalLayout` forward-decl if the layout type is not already visible). Minimal diff; `RenderFrameGlobal` passes its already-mapped ref, no redundant re-map.
  - **(B)** match the sibling `Render*Global` entry style: `RenderWaterGlobal(int64_t iCommandBuffer, float fSunAngle, float fDayPercent)` that re-maps `gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer)` internally. More consistent with siblings, but adds a redundant map + `reinterpret_cast` (write-combined memory, no readback, so cheap) for a ref `RenderFrameGlobal` already holds. Either way `fSunAngle` / `fDayPercent` must still be passed — re-deriving them in the water TU would duplicate `PopulateDayCycleFeatherWindows` (divergence risk), so they cannot be recomputed locally.
- **Co-scheduling.** `GlobalUniforms.cpp` sits in a File Group with `Architecture_ShadowLightingUniformDedup`, `Refactor_RenderUniformsQuickWins`, and `WindowedLightingShadowDispatch`. Those edit the shadow / lighting / temporal regions (earlier in the file) and are **disjoint** from the water block moved here — but this split shifts `GlobalUniforms.cpp` structure, so refresh their `GlobalUniforms.cpp` line citations if landed together. `Architecture_ShadowLightingUniformDedup` extracts a shared temporal-area latch across `GlobalUniforms` / `LightingUniforms`; it does not touch the water accumulators, so there is no interaction with the moved statics.
