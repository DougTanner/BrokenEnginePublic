# Reduce the smoke sim texture format R32_SFLOAT → R16_SFLOAT

## Context

The smoke ping-pong pair (`mSmokeTextureOne/Two`) stores a single-channel density field in
`shaders::keSmokeFormat` = `VK_FORMAT_R32_SFLOAT`. At a 3840×2160 framebuffer each texture is ≈ 8192×4608
(`SmokeSimulationPixels()`, default `gSmokeSimulationPixels` = 1.0) = **151 MB**. Before full-texture clear elimination,
every byte was touched by the per-frame clears; current bandwidth comes from spread reads/writes, deposit blend, and
consumer sampling (Water/Terrain/Model/Particles all sample smoke via the `ShaderFunctions.h` helpers).

Historical pre-clear-elimination measurement (built-in GPU timestamp profiler, Profile build, 120 fps, **idle scene**):
`kGpuTimerSmokeSpread` 1210 µs current / 1245 avg, of which ~1080 µs was the two full-texture clears
(≈ 14.3 µs per megatexel cleared, against `kGpuTimerWindSpread`'s 129 µs). That fixed per-frame clear cost is already
eliminated; the remaining spread/sample bandwidth this plan targets materializes with smoke content, plus residual
creation/edge clears.

**PRIOR ATTEMPT — REGRESSED (user report, must not be repeated naively):** R32F→R16F was tried before on this sim and
made it *slower* — the compute-shader 32↔16-bit conversions were heavy enough to outweigh the bandwidth savings. Treat
the prior regression as the null hypothesis: this plan is only viable with an approach that avoids per-texel ALU
conversion cost, and every step must be A/B-measured via the `kbProfilingDump` CSV loop before being kept.

A 32-bit float per texel is overkill for a visible density field in O(0..fSmokeMax) range that is decayed
multiplicatively and hard-zeroed below a threshold each frame; 16-bit float (10-bit mantissa) is the standard choice.
Halving bytes halves every bandwidth term above — *if* the conversion cost that sank the prior attempt can be avoided
(see Design step 0). Bonus: `R16_SFLOAT`'s `SAMPLED_IMAGE_FILTER_LINEAR` support is
spec-mandatory, so the existing smoke-sampler NEAREST downgrade for devices without linear-filterable `R32_SFLOAT`
(documented in `Managers/AGENTS.md`, built in `TextureManager` sampler creation) becomes dead — devices currently
falling to NEAREST get *better* smoke filtering.

Full-texture clears are already eliminated. This plan now targets the remaining content-proportional spread/consumer
bandwidth plus residual creation/edge clears. The same content-driven reasoning applies against
`Graphics/DisabledPassGatingPerfAudit.md`: this complements (does not replace) the audit; do not edit the audit.

## Design

0. **First: diagnose why the prior R16F attempt regressed, and pick a conversion-free route.** Recover what the prior
   attempt did (explicit `packHalf2x16`/`unpackHalf2x16` ALU packing? `float16_t` arithmetic via
   `VK_KHR_shader_float16_int8`? plain `r16f` storage qualifiers?) — the workaround depends on which conversion path
   was hot. Candidate routes, in preference order:
   - **Fragment-path spread instead of compute**: render the spread passes as fragment draws to an `R16_SFLOAT` color
     attachment — format conversion then happens in the ROP/export hardware, not shader ALU, and sampled reads return
     fp32 for free via the texture unit. The sim already has fragment-pipeline precedent (`kPipelineSmokeClearA/B`,
     `Smoke.frag` deposit blend).
   - **Keep fp32 ALU end-to-end, convert only at the image boundary** (`r16f` storage qualifier with all math in
     `float`): if the prior attempt used explicit fp16 arithmetic or manual pack/unpack, this alone may fix it — but
     it may also be exactly what was tried; confirm before trusting.
   - **`R16G16_SFLOAT` two-texels-per-lane processing** (halve invocations, amortize conversion) — larger shader
     rework, only if the simpler routes fail measurement.
   If no route beats R32F in the A/B dump, close this plan as "measured, rejected — conversion cost dominates" so it
   is not re-proposed.
1. `shaders::keSmokeFormat` (`Engine/Data/Shaders/ShaderLayoutsBase.h`) → `VK_FORMAT_R16_SFLOAT`. The dual-language
   constant is the single source of truth; `RenderTargetTextures::CreateSmokeTextures` and the smoke render passes pick
   it up.
2. Storage-image layout qualifiers in the two spread shaders: `r32f` → `r16f` in `SmokeSpreadOne.comp` and
   `SmokeSpreadTwo.comp` (`writeonly image2D outputImage`). Shader repack.
3. **Device support**: `R16_SFLOAT` is spec-required for SAMPLED (+linear filter), COLOR_ATTACHMENT and
   COLOR_ATTACHMENT_BLEND (covers `Smoke.frag`'s additive deposit and the `kPipelineSmokeClearA/B` fragment clears —
   the existing InstanceManager boot guard for blended special-format RTTs must extend to the new format per its own
   documented rule). STORAGE_IMAGE is *not* spec-required for `R16_SFLOAT` (unlike `R32_SFLOAT`) though it is universal
   on desktop hardware; probe it via the existing `GraphicsUtils` format-feature query at texture creation and fail
   loud (ASSERT + error log, matching the InstanceManager blend-guard precedent) rather than silently downgrading.
4. **Extinction-constant retune** (`SmokeSpreadTwo.comp`): `kfSmokeConstantDecay` = 1e-9 and `kfSmokeZeroThreshold` =
   1e-7 sit below `R16_SFLOAT`'s normal range (min normal ≈ 6.1e-5); stored densities under ~6e-5 become subnormal
   (flushed to zero on much hardware). Raise both so the exact-zero convergence stays *designed* rather than
   accidental — e.g. threshold ≈ 1e-3 (invisible density) and constant decay ≈ 1e-4. This slightly shortens the faint
   tail of dissipating smoke, which also tightens the active-tile set (a small perf win in itself).
5. Remove (or leave vestigial-but-documented) the smoke-sampler NEAREST downgrade path in `TextureManager` sampler
   creation — decide at grill; removal is one less special case, keeping it is zero-cost.

**Invariant exposure**: client/graphics-only — smoke is render-only, wall-clock driven, never in deterministic frame
state; no CRC / replay / wire / `kiVersion` / `.pack`-layout exposure (the format constant lives in the dual-language
header but only feeds runtime texture creation, not packed data). **Shader repack required** (two `.comp` qualifier
edits + constant retune). No allocation-tracked-path changes.

**Visual impact statement (mandatory)**: **(b) minor / imperceptible at gameplay camera heights.** A single-channel
density field rendered through soft color/alpha ramps; 10-bit mantissa quantization is far below the visible threshold
for this content, and the retuned extinction constants make faint (sub-1e-3-density) smoke die marginally sooner —
invisible at RTS camera distance. Not practically sliderable: format is a texture-creation-time property, so a runtime
toggle would need a `DestroyFlags::kSmokeTextures` destroy-tier recreate for a knob nobody would perceive — rejected
(YAGNI). The existing `gSmokeSimulationPixels` slider (0.5-1.5, `GraphicsSettingsWrappersBase`, TweaksSliderMap-exposed)
remains the user-facing smoke cost/quality lever.

Expected saving: ~half of the smoke sim's remaining content-proportional spread/consumer bandwidth, half the residual
creation/edge-clear bandwidth, and half its memory footprint (302 MB → 151 MB for the pair). The historical 1245 µs
average is not the current expected delta because its ~1080 µs per-frame clear share is already eliminated.

## Critical files

- `Engine/Data/Shaders/ShaderLayoutsBase.h` — `keSmokeFormat` constant.
- `Engine/Data/Shaders/Smoke/SmokeSpreadOne.comp` / `SmokeSpreadTwo.comp` — `r32f` → `r16f` image qualifiers;
  `kfSmokeConstantDecay` / `kfSmokeZeroThreshold` retune (in `SmokeSpreadTwo.comp`).
- `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp` — `CreateSmokeTextures` (consumes the constant; add the
  STORAGE_IMAGE format-feature probe here or at the shared query helper).
- `Engine/Source/Graphics/GraphicsUtils.cpp` — existing physical-device format-feature query helpers (probe site).
- `Engine/Source/Graphics/Managers/InstanceManager.cpp` — the blended-special-format-RTT boot guard (extend to
  `R16_SFLOAT` per its documented "new blended prepass on a novel format must extend that guard" rule).
- `Engine/Source/Graphics/Managers/TextureManager.cpp` — smoke-sampler NEAREST-downgrade path (delete or document).
- `Engine/Source/Graphics/Managers/TextureManager.AGENTS.md` / `Managers/AGENTS.md` — the R32_SFLOAT smoke-sampler note
  goes stale.

## Out of scope

- The wind texture format (`keWindFormat` is already `R16G16_SFLOAT`).
- Smoke sim resolution defaults / `gSmokeSimulationPixels` range changes.
- Any change to the smoke kernel math (`SmokeSpreadCommon.h`) beyond the two extinction constants.
- `Graphics/DisabledPassGatingPerfAudit.md` — untouched; its smoke row should be re-measured after the smoke plans land.

## Notes

- **Grill gate: the prior-regression question comes first** — if the user can recall or locate what the earlier R16F
  attempt changed (commit, branch, or memory of the shader edits), that determines Design step 0's route; without a
  conversion-free route that measures faster, this plan closes rejected.
- Pre-staged grill decision: **extinction-constant values** (threshold ≈ 1e-3, constant decay ≈ 1e-4 recommended —
  pick against `gSmokeColorMin`/`fSmokeColorMultiplier` so the threshold sits below first-visible density) and
  **NEAREST-downgrade path: delete vs keep-documented**.
- Full-texture clear elimination is complete; this plan's A/B comparison isolates format as the variable.
- If a future device without `R16_SFLOAT` STORAGE_IMAGE ever matters, the fallback is reverting the constant — the
  probe's fail-loud keeps that decision visible rather than silent.
