# Sampled Normals Snap-Step Audit

## Context

`WaterColorNoiseMultiplierSliderInvariant` introduced a snap-step parameter (`float fStep`) on the float-flavoured `engine::Wrapper` constructor in `Engine/Source/Ui/WrapperBase.h:16` (`Snap()` helper at `WrapperBase.h:196`). Two color-noise sliders (`gWaterColorNoiseMultiplierOne/Two` in `WaterWrappersBase.cpp:86,88`) opted into a `0.1f` step so `mult * 10` stays integer for the `fract()`-based UV-wrap precision pact in `Water.frag` (color noise block, `Water.frag:155-170`).

The same `* 10` integer-product pact governs the multi-sample normal stack. `GlobalUniforms.cpp:438-446` reduces per-sample camera origins via `std::fmod(dSizeBase * dRotCameraXY, 10.0)` for sample One/Two/Three, and the `SAMPLE_NORMAL_PRECISE` macro in `Water.frag:108-118` then does `fract(offset + fCallSize * f2InInitialPosition + sizeMult * reducedOrigin + ...)`. For `fract()` to absorb the wrap cleanly, `sizeMult * 10` must be integer. The per-octave `sizeMult` constants (`Water.frag:122-124,130-132,138-140`) are hardcoded at `{0.2, 1.1, 2.5, 0.3, 1.2, 3.0, 0.4, 1.3, 3.5}` — all integer-aligned on the 0.1 grid. But the slider-controlled per-sample `size` factor (`fSizeOne/Two/Three`, sourced from `gLightingSampledNormalsOneSize/TwoSize/ThreeSize`) flows through `dSizeBase` into the same reduced-origin modulus AND multiplies `sizeMult` inside the macro (`fCallSize = sizeMult * size`), so non-tenths slider values break the integer-product property the same way `gWaterColorNoiseMultiplier*` did pre-snap. `GlobalUniforms.cpp:448-453` already calls out the gotcha for the color path; sampled-normal sliders need the same protection.

Adjacent sliders considered and **excluded** by this audit:

- `gWaterNoiseFrequency` (`WrapperBase.cpp:25`): uploaded into `fWaterNoiseFrequency` (`ShaderLayoutsBase.h:343`, set by `GlobalUniforms.cpp:325`) but not read by any shader. No pact applies — currently dead. Drop or repurpose is out of scope.
- `gTerrainBeachNormalsSizeOne/Two/Three` (`TerrainWrappersBase.cpp:11-13`): consumed by `Terrain.frag:86-88` via the shared `SampleNormal` helper (`ShaderFunctions.h:49-56`). That helper does `texture(sampler, fOffset + fSize * f2Position + ...)` with **no `fract()` wrap and no reduced origin** — there is no integer-product invariant to protect. The same applies to `gTerrainRockNormalsSizeOne/Two/Three` (`TerrainWrappersBase.cpp:21-23`). No snap step needed.

## Design

Recommended: opt the three sampled-normal-size sliders into snap step `0.1f`, matching the pattern just landed for `gWaterColorNoiseMultiplierOne/Two`.

| Slider | File:Line | Current | Min | Max | Default-on-grid? |
|---|---|---|---|---|---|
| `gLightingSampledNormalsOneSize` | `WaterWrappersBase.cpp:13` | `0.2f` | `0.05f` | `1.0f` | yes (default 0.2 OK; min 0.05 snaps to 0.1) |
| `gLightingSampledNormalsTwoSize` | `WaterWrappersBase.cpp:23` | `0.05f` | `0.025f` | `0.1f` | **no** — 0.05 is on grid but min 0.025 / max 0.1 give an effective range of just 0.0 / 0.1 after snap |
| `gLightingSampledNormalsThreeSize` | `WaterWrappersBase.cpp:29` | `0.04f` | `0.025f` | `0.1f` | **no** — default 0.04 snaps to 0.0; range collapses |

The `Two` and `Three` sliders today operate in a sub-tenth tuning range. A 0.1 step would collapse their useful range. Two options:

1. **Recalibrate range, then snap step 0.1f.** Widen the min/max so a 0.1 grid produces a meaningful slider (e.g. `(0.1f, 0.0f, 1.0f, 0.1f)`). Risk: changes the working defaults the artists already tuned around.
2. **Use a finer step that still preserves integer wrap.** Step `0.05f` halves the integer-product invariant: `size * 10` becomes integer-or-half-integer; `sizeMult * size * 10` is still integer when `sizeMult` is on the 0.2 grid. The hardcoded macro constants `{0.2, 1.1, 2.5, 0.3, 1.2, 3.0, 0.4, 1.3, 3.5}` are NOT all multiples of 0.2 (e.g. 1.1, 2.5, 1.3, 3.5), so step 0.05 does NOT preserve the integer product.
3. **Single shared modulus widened.** Increase the `std::fmod` modulus in `GlobalUniforms.cpp:438-446` from `10.0` to `100.0` to absorb a 0.01-grid product. Knock-on: also requires the same modulus shift everywhere the wrap is consumed (the macro `SAMPLE_NORMAL_PRECISE` is wrap-agnostic — it only relies on `fract()`, so 100.0 works), but the fmod result fits at 100.0 wraps farther from origin and may erode float precision faster.

**Recommended path**: Option 1 for `gLightingSampledNormalsOneSize` (already snaps cleanly at default 0.2), and a session-time decision between Options 1 and 3 for `Two`/`Three`. Default to Option 3 (modulus 100, snap step 0.01) for `Two` and `Three`, which preserves the existing tuning ranges and defaults exactly. Confirm with user before landing.

The grill skill should resolve the Option 1 vs 3 choice for the two narrow sliders.

## Critical files

- `Engine/Source/Ui/WaterWrappersBase.cpp` — `gLightingSampledNormalsOneSize` decl (line 13), `gLightingSampledNormalsTwoSize` decl (line 23), `gLightingSampledNormalsThreeSize` decl (line 29). Add fourth ctor arg per chosen option.
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` — `dSizeBaseOne/Two/Three` reads (lines 393-395) and `std::fmod(..., 10.0)` reductions (lines 438-446). If Option 3 is taken for Two/Three, change those moduli to `100.0` and update the explanatory comment block at lines 448-453 to mention the per-sample modulus split.
- `Engine/Data/Shaders/Water/Water.frag` — `SAMPLE_NORMAL_PRECISE` macro (lines 108-118) and call sites (lines 122-124, 130-132, 138-140). No code change needed (macro is wrap-modulus-agnostic), but update the precision-pact comment block (lines 103-107) to mention slider snap step.
- `Engine/Data/Shaders/Water/CLAUDE.md` — "Precision-safe sampling pact" note already covers color-noise snap; extend to mention sampled-normal-size snap symmetry.

## Out of scope

- `gWaterNoiseFrequency` (dead-write — currently uploaded but unread in any shader). Removing it or wiring it back up is a separate plan.
- `gTerrainBeachNormalsSizeOne/Two/Three` and `gTerrainRockNormalsSizeOne/Two/Three`. They feed `Terrain.frag` via `SampleNormal` which has no `fract()` wrap, so no integer-product invariant applies to them.
- Any wider audit of every `* 10` integer-product pact in the codebase. The two color-noise sliders and these three sampled-normal-size sliders are the only known sites with a CPU-side `std::fmod(..., 10.0)` paired with a shader-side `fract()` consuming a slider product.
- Renaming the `gLightingSampledNormals*Size` sliders to live under `gWater*` despite consuming water-only state — naming alignment, not invariant protection.
- Behavioural change to default values. Defaults should round-trip identically (or as close to identical as the snap grid permits).

## Acceptance criteria

- Each of `gLightingSampledNormalsOneSize`, `gLightingSampledNormalsTwoSize`, `gLightingSampledNormalsThreeSize` is constructed with a non-zero `fStep` argument.
- For each slider, the chosen step divides the modulus (10 or 100) cleanly so `size * modulus` is integer for any in-range value.
- The hardcoded `sizeMult` constants in `SAMPLE_NORMAL_PRECISE` call sites either remain on the 0.1 grid (current) or are renormalized to match a wider modulus if Option 3 is chosen for any sample.
- No visible change at default slider values (eyeball test in BrokenEngineSandbox).
- No new compile warnings, no allocation-tracking trips at startup.
- `Documents/Plans/Order.md` row removed when this plan lands.

## Notes

- The `Wrapper` snap-step constructor already snaps default/min/max at construction (`WrapperBase.h:18-20`) and snaps subsequent `Set`/`Reset` (`WrapperBase.h:123,145`), so opting in is purely a fourth-argument addition per declaration.
- The `Engine/Source/Ui/CLAUDE.md` "Wrapper invariants" paragraph already documents the snap step as "used to enforce shader-side integer-product invariants on continuous sliders" — extending the pattern is consistent with that contract.
- If Option 3 is taken (modulus 100), the explanatory comment in `GlobalUniforms.cpp:448-453` (color-noise block) still describes modulus 10; the sampled-normals block at `GlobalUniforms.cpp:438-446` will need its own comment explaining why the per-sample sliders use a wider modulus than the color-noise sliders.
- Trivially revertable: revert the constructor argument additions and the optional modulus widening; no on-disk save format depends on the slider values.
