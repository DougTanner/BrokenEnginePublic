# Water Normal-Weight CPU-Resolve (Decision 5 spin-out)

## Context

Spun out of `Common/DocCanonicalHomeReconciliation.md` Decision 5 (the user chose the **refactor** branch over documenting an exception). The `Render/CLAUDE.md` "Camera-Height-Conditional Uniforms" rule states: *"When a uniform varies with camera eye height, lerp CPU-side and upload the single resolved float — never pass endpoint heights and low/high targets to the shader."* `Water.frag` violates this for the three normal-weight samples: it uploads six min/max endpoints plus a shared `fCameraHeightZoomFactor` and runs the `mix(min, max, factor)` lerp **in-shader**.

Current state (verified):
- **Shader lerp** — `Water.frag:157-159`:
  ```
  float fWeightOne   = mix(mainLayout.fWaterNormalWeightOneMin,   mainLayout.fWaterNormalWeightOneMax,   mainLayout.fCameraHeightZoomFactor);
  float fWeightTwo   = mix(mainLayout.fWaterNormalWeightTwoMin,   mainLayout.fWaterNormalWeightTwoMax,   mainLayout.fCameraHeightZoomFactor);
  float fWeightThree = mix(mainLayout.fWaterNormalWeightThreeMin, mainLayout.fWaterNormalWeightThreeMax, mainLayout.fCameraHeightZoomFactor);
  ```
  Mirrored in `WaterSkyboxOne.frag` (the pre-pass duplicates the main pass's multi-sample normal composition block verbatim — both must change in lockstep).
- **CPU population** — `LightingUniforms.cpp:108-118`: writes `fWaterNormalWeight{One,Two,Three}{Min,Max}` from the `gLightingSampledNormalsWeight{One,Two,Three}{Min,Max}` wrappers and computes `fCameraHeightZoomFactor` via `engine::LerpAtHeight(mfCameraEyeHeight, kfCameraEyeHeightDefault, kfWaveFadeEndHeight, 0.0f, 1.0f)`.
- **Layout struct** — `ShaderLayoutsBase.h` (`MainLayout`): the six `fWaterNormalWeight*{Min,Max}` members + `fCameraHeightZoomFactor`.
- **Doc** — `Water/CLAUDE.md:17` documents the in-shader form as deliberate ("Per-sample weights interpolate between min/max endpoints by a CPU-resolved camera-zoom factor … without a separate pipeline").

## Design

Resolve the three weights CPU-side and upload three single floats, matching the Render rule and the existing `HeightLerpWrapperQuartet::Resolve(fEyeHeight)` pattern (`Engine/Source/Ui/HeightLerpWrapperQuartet.h`) that the rule prescribes:

1. In `LightingUniforms.cpp` replace the six `fWaterNormalWeight*{Min,Max}` writes with three resolved writes: `rMainLayout.fWaterNormalWeightOne = std::lerp(gLightingSampledNormalsWeightOneMin.Get(), gLightingSampledNormalsWeightOneMax.Get(), fFactor)` (and Two/Three), where `fFactor` is the existing `LerpAtHeight(...)` value. Prefer migrating the three Min/Max wrapper pairs to a `HeightLerpWrapperQuartet` per sample (or one shared-factor quartet group) so the resolve goes through `Resolve(fEyeHeight)` — confirm at execution whether the quartet abstraction fits three samples sharing one factor, or whether the inline `std::lerp` is the cleaner KISS choice.
2. In `ShaderLayoutsBase.h` (`MainLayout`) replace the six `fWaterNormalWeight*{Min,Max}` members with three `fWaterNormalWeightOne/Two/Three`. **Audit every consumer of `fCameraHeightZoomFactor` first** — it is read by both `Water.frag` and `WaterSkyboxOne.frag`, and a same-named local is recomputed for the speed lerp at `GlobalUniforms.cpp:549` (separate, not this uniform). Drop the `fCameraHeightZoomFactor` *uniform* member only if the three-weight lerp is its sole shader consumer; if any other shader site reads it, keep the member and only collapse the weight pairs.
3. In `Water.frag` replace the three `mix(...)` lines with direct reads of `mainLayout.fWaterNormalWeightOne/Two/Three`; apply the identical change to the duplicated block in `WaterSkyboxOne.frag`.
4. Update `Water/CLAUDE.md:17` to describe the resolved-CPU-side form (remove the "interpolate between min/max endpoints … CPU-resolved camera-zoom factor" deliberate-in-shader wording); confirm the `Render/CLAUDE.md` rule now needs no Water carve-out.

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` (`:157-159` lerp → direct reads)
- `Engine/Data/Shaders/Water/WaterSkyboxOne.frag` (mirrored multi-sample normal block — change in lockstep)
- `Engine/Data/Shaders/ShaderLayoutsBase.h` (`MainLayout`: 6 min/max members → 3 resolved; `fCameraHeightZoomFactor` removed only if sole-consumer)
- `Engine/Source/Graphics/Render/LightingUniforms.cpp` (`:108-118` population → CPU lerp)
- `Engine/Data/Shaders/Water/CLAUDE.md` (`:17` deliberate-in-shader note → resolved-CPU-side)
- Read-only: `Engine/Source/Ui/HeightLerpWrapperQuartet.h` (the prescribed resolve pattern), `Engine/Source/Graphics/Render/CLAUDE.md` (the rule)

## Out of scope

- The other Water camera-height-conditional uniforms already compliant with the rule (only the three normal-weight samples are at issue).
- `GlobalUniforms.cpp:549`'s local `fCameraHeightZoomFactor` for the speed lerp — a separate computation, not the `MainLayout` uniform.
- Any visual retuning of the weights — the resolved values must be numerically identical to the previous in-shader `mix` at every eye height (this is a layout/location refactor, not a feel change).
- Other shader↔CPU layout constants (owned by the Objects/Managers shader-CPU-consistency plans).

## Acceptance criteria

- `Water.frag` and `WaterSkyboxOne.frag` no longer reference `fWaterNormalWeight*{Min,Max}`; they read three resolved `fWaterNormalWeight{One,Two,Three}` floats.
- The `MainLayout` uniform surface drops the six min/max members (and `fCameraHeightZoomFactor` if it had no other consumer).
- `Render/CLAUDE.md`'s rule holds for Water with no exception carve-out; `Water/CLAUDE.md` describes the CPU-resolved form.
- Water surface renders identically before/after at near and far zoom (visual smoke-test — not compile-checked).

## Notes

- **Invariant exposure**: client/graphics-only. Touches the `MainLayout` shader uniform layout (`ShaderLayoutsBase.h`) → **requires a DataPacker shader recompile**. No shared-CRC/determinism/`kiVersion`/`.pack`-save-layout exposure (shader uniform layout is not the save/replay version). Not compile-checked end-to-end — needs a visual smoke-test at multiple eye heights.
- **Grill decisions pre-staged**: (a) `HeightLerpWrapperQuartet::Resolve` migration vs inline `std::lerp` for the three shared-factor samples; (b) whether `fCameraHeightZoomFactor` survives as a uniform (sole-consumer audit).
- Coordinate with any in-flight plan editing `ShaderLayoutsBase.h` `MainLayout` members or `LightingUniforms.cpp` to share the DataPacker recompile.
