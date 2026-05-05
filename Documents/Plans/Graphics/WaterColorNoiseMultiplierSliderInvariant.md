# Water Color Noise Multiplier Slider Invariant

## Context

A precision-safe sampling pact governs the water color-noise UV path in `Engine/Data/Shaders/Water/Water.frag` (~lines 155-172) and its CPU origin source in `Engine/Source/Graphics/Render/GlobalUniforms.cpp` (`fWaterReducedNoiseOriginX/Y`, computed as `fmod(freq*camera, 10.0)`). The pact mirrors the existing one used by `SAMPLE_NORMAL_PRECISE` in the same shader: `fract()`-wrapped UVs combined with `textureGrad` derivatives taken from the un-scaled local position keep mip selection stable across the wrap radius.

The wrap radius the CPU produces is exactly `10.0` in pre-multiplied frequency space. For the seam to be invisible, `mult * 10` must be integer for each per-sample multiplier — `fract(mult * 10 * (origin + freq*localPos))` is then phase-equivalent to `fract(mult * 10 * freq * world)` and the seam disappears.

Defaults satisfy this pact:
- `gWaterColorNoiseMultiplierOne = 0.2` → `0.2 * 10 = 2` (integer, valid)
- `gWaterColorNoiseMultiplierTwo = 1.0` → `1.0 * 10 = 10` (integer, valid)

But the UI sliders, declared in `Engine/Source/Ui/WrapperBase.cpp` lines 135-136, are continuous:

```
Wrapper gWaterColorNoiseMultiplierOne(0.2f, 0.0f, 1.0f);
Wrapper gWaterColorNoiseMultiplierTwo(1.0f, 0.0f, 4.0f);
```

A user dragging either slider to (e.g.) 0.15 or 1.5 silently re-introduces a visible jump at the noise wrap radius, because `0.15 * 10 = 1.5` and `1.5 * 10 = 15` modulo any non-integer fraction breaks the `fract()` identity. The fragment shader has a comment acknowledging this trap (Water.frag line 159-160: "Sliders allow values that break the integer-product property (e.g. 0.15 \* 10 = 1.5) — the seam will return at those tunings."), but no enforcement exists — neither at the UI layer nor at upload.

This plan picks one of three remediations.

## Design

Three options, in increasing cost and rigor:

### Option 1 — Snap-step UI (recommended default)

Add a minimum step to `Wrapper` (a third constructor parameter, or a new flavour) so the sliders can only land on a discrete grid. For these two wrappers, choose `0.1` so the grid is `{0.0, 0.1, 0.2, ... 1.0}` for `One` and `{0.0, 0.1, 0.2, ... 4.0}` for `Two` — every grid value satisfies `mult * 10` integer.

- **Cost**: small. One new optional parameter on `Wrapper` (engine-scope), thread the step through to the ImGui call site (`SliderFloat` already supports a step via `%.1f` format), and update the two declarations on `WrapperBase.cpp:135-136`. Half-day.
- **Pros**: cheapest; preserves ergonomic continuous-feeling tuning; the invariant becomes un-trip-overable for any user dragging the slider; `Wrapper` step is a generic feature other sliders can opt into later.
- **Cons**: invariant is enforced by UI policy, not by data. A future code path that calls `Set(0.15f)` directly bypasses the step. Step granularity is also a design call — `0.1` keeps current expressiveness, `0.5` is safer but coarser.

### Option 2 — Discrete enum

Replace the two `Wrapper` declarations with the discrete-enum constructor flavour already supported by `Wrapper` (per `Engine/Source/Ui/CLAUDE.md`: "Discrete-enum construction takes the allowed-value set and `DEBUG_BREAK`s on out-of-set values"). Allowed sets, e.g.:
- `One`: `{0.0, 0.1, 0.2, 0.5, 1.0}`
- `Two`: `{0.1, 0.2, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0}`

- **Cost**: small-to-medium. Same call-site change as Option 1 but UI control becomes a dropdown / stepper. Tweaks-screen entries in `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp:273-275` may need a discrete-control variant. Half- to full-day.
- **Pros**: invariant statically checkable — `DEBUG_BREAK` on construction catches any bad value; eliminates the trap entirely at the data layer.
- **Cons**: narrows tuning latitude; every future "I want to try 0.7" requires a code edit to extend the allowed set; loses fine-grained ergonomic feel. UI affordance shifts from slider to dropdown which is a stylistic departure from siblings.

### Option 3 — Auto-derive cancellation

Eliminate the constraint. CPU computes a per-multiplier reduced origin so the GPU sees an integer wrap at any multiplier. Add two new uniform fields (`fWaterReducedNoiseOriginOneX/Y`, `fWaterReducedNoiseOriginTwoX/Y`) populated as `fmod(mult * freq * camera, 1.0) / mult` (so that `mult * origin` lands on an integer modulus). Shader uses the per-sample origin directly:

```glsl
vec2 f2NoiseUvOne = fScaleOne * f2LocalDisplacedPos + fract(fMultOne * f2ReducedNoiseOriginOne);
```

- **Cost**: medium. Two new fields in `GlobalLayout` (`Engine/Source/Graphics/Render/GlobalUniforms.cpp:349-350` site), CPU population logic, shader UV-build rewrite (~6 lines), and a sweep of `ShaderLayoutsBase.h` (or wherever `GlobalLayout` is declared dual-language) to add the new members. Two extra `fmod` per sample on the CPU per frame (negligible). Half- to full-day, but cross-cuts shader+CPU+layout header.
- **Pros**: best from a robustness standpoint. Sliders become free; pact is no longer a slider invariant but a CPU implementation detail. Matches `Render/CLAUDE.md`'s stated camera-relative double-precision convention more faithfully ("Moduli chosen so shader-side size multipliers remain integer after reduction" — Option 3 makes this true by construction rather than by slider hygiene).
- **Cons**: highest implementation cost; introduces two new hot-path uniform fields; the symmetry between the two existing `fWaterReducedNoiseOriginX/Y` (single shared value) and the new per-sample variant is asymmetric until the older path is also migrated (out of scope here — see Out of scope).

### Recommendation

**Option 1** (snap-step, step = 0.1). It is the cheapest fix that closes the user-facing trap, preserves slider ergonomics, and adds a generic `Wrapper`-level capability that other invariant-bound sliders in `WrapperBase.cpp` can opt into later. Option 3 is architecturally cleaner but the invariant currently affects only these two sliders — paying medium cost for narrow surface fails the YAGNI test until a second multiplier-bound surface appears. Option 2 is rigid for limited rigour gain.

If the invariant later expands (e.g., the ocean-phase work introduces additional multiplier-bound noise samples per `Documents/Features/Graphics/ocean-phase-*`), revisit — Option 3 becomes the right call when the surface broadens.

## Critical files

- `Engine/Source/Ui/WrapperBase.cpp:135-136` — `gWaterColorNoiseMultiplierOne` and `gWaterColorNoiseMultiplierTwo` declarations. Slider step lands here.
- `Engine/Source/Ui/WrapperBase.h:332-333` — `extern Wrapper gWaterColorNoiseMultiplierOne/Two` declarations. Plus the `Wrapper` class signature itself (engine-scope `Wrapper` defined in this header) — Option 1 adds an optional step parameter to one constructor; Option 2 uses the existing discrete-enum flavour; Option 3 leaves both untouched.
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp:349-350` — `rGlobalLayout.fWaterColorNoiseMultiplierOne/Two` upload site. Option 3 only: rework into per-sample reduced-origin computation alongside `fWaterReducedNoiseOriginX/Y`.
- `Engine/Data/Shaders/Water/Water.frag:155-172` — color-noise UV block. Option 3 only: switch from shared `f2ReducedNoiseOrigin` to per-sample origins; also update the comment block (lines 156-160) once the invariant is no longer slider-fragile.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp:273-275` — the `"Water Color Noise Multiplier One/Two"` entries. No change for Options 1/3; Option 2 may require a discrete-control entry variant.

## Out of scope

- The hard-coded per-octave `sizeMult` literals inside `SAMPLE_NORMAL_PRECISE` in `Water.frag` are pinned in shader source and currently integer-aligned for the existing per-sample size multipliers; they are correct as written. Worth a one-line comment near the macro noting "values must keep `sizeMult * 10` integer for the precision pact" — not a code change.
- Migrating the existing shared `fWaterReducedNoiseOriginX/Y` path to per-sample origins (Option 3 done generally rather than for color-noise only). If Option 3 is chosen for color-noise, do not opportunistically also rewrite the normal-noise path — that is a separate, larger plan.
- Tightening any other `Wrapper` slider with implicit numerical invariants. Plenty exist in `WrapperBase.cpp` (e.g. `gWaterNoiseFrequency`, `gTerrainBeachNormalsSizeOne/Two/Three`) but each needs its own audit before snapping a step on them.
- The seam-vs-zoom interaction with the ocean-phase work (`Documents/Features/Graphics/ocean-phase-*`). Coordinate when ocean-phase lands; it may motivate Option 3 retroactively.

## Notes

- The fragment-shader comment at `Water.frag:159-160` already documents the invariant — fixing the slider closes the loop. After the change, update or remove that "Sliders allow values that break the integer-product property" sentence to reflect the new state.
- `Wrapper` step support (Option 1) is mechanically: add an optional fourth constructor param `float fStep = 0.0f`, store it, and pass it through to `ImGui::SliderFloat` (which honours steps via custom format strings or `ImGuiSliderFlags_AlwaysClamp` plus snap-on-release logic). The exact ImGui plumbing is a small implementation detail to settle during execution.
- Option 3 should reuse the existing `fmod` pattern in `GlobalUniforms.cpp` near the noise-origin computation; align with `Render/CLAUDE.md`'s "CPU computes phase / UV origins in `double`, `std::fmod` reduces to a small modulus, then `static_cast<float>`".
