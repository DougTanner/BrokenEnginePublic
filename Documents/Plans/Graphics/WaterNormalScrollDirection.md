# Water Normal Scroll Direction (per-sample)

## Objective

Add a per-sample "Speed Direction" control to each of the three water normal samples so its
texture scrolls in a chosen world direction, independent of that sample's Rotation slider.

## Problem

The scroll direction is currently fixed. `GlobalUniforms.cpp::PopulateWaterReducedUv` accumulates
the per-sample `reducedTime` delta as `dDelta * R(-θ)·(1,1)` (coded as `(cosθ+sinθ, cosθ-sinθ)`),
where θ is the sample's Rotation. In world space the pattern's `R(θ)` cancels the delta's `R(-θ)`,
so every sample scrolls along the same fixed world direction (1,1) — rotating a sample re-orients its
pattern but not its scroll. There is no control to point scroll a different way per sample.

## Mechanism (no shader / uniform change)

Scroll is fully determined CPU-side by how `reducedTime{X,Y}` is accumulated; the shader only adds
`speedMult * reducedTime` (`Water.frag` `SAMPLE_NORMAL_PRECISE`). So the feature is implemented
entirely in `GlobalUniforms.cpp` plus a new author control. No `MainLayout`/`GlobalLayout` field, no
`Water.frag` edit, no `LightingUniforms.cpp` edit (rotation stays uploaded there for pattern orient;
speed/direction remain GlobalUniforms-only).

Design: introduce per-sample angle φ (`gWaterNormalSpeedDirection{One,Two,Three}`). World scroll
direction becomes `R(φ)·(1,1)` (an offset from today's baseline). UV-space accumulation:

    γ = φ − θ
    reducedTimeX += dDelta * (cos γ − sin γ)
    reducedTimeY += dDelta * (cos γ + sin γ)

- φ = 0 reproduces today exactly for every θ (at γ = −θ this collapses to the current
  `(cosθ+sinθ, cosθ−sinθ)`), so existing water is visually unchanged on ship.
- Magnitude is constant √2 for all φ (same as today's `(1,1)`), so changing direction never changes
  scroll speed — that stays owned by the Speed Min/Max sliders.
- Precision pact preserved: each component still wraps at 10.0 independently and `speedMult*10` stays
  integer, so `fract()` absorbs the wrap for any φ. φ only changes the X:Y increment ratio.
- Decoupled from rotation: world dir `R(φ)·(1,1)` has no θ term. θ still owns pattern orientation via
  the shader's `m2UvRot`/`m2NormalRot` (uploaded from `LightingUniforms.cpp`, untouched here).

## Open decision (for grill)

Angle framing / default:
- **Recommended — offset, default 0.0** (range −π..π like Rotation): baseline = current (1,1); 0 = no
  change, φ rotates scroll off that baseline. Safest (zero disturbance), √2 magnitude preserved.
- Alternative — absolute world angle, default π/4: slider reads as compass-like world direction
  (0 = +x East). Would use `√2 * (cos α, sin α)` to keep the √2 magnitude and match (1,1) at π/4.

## Files

1. `Engine/Source/Ui/WaterWrappersBase.h` — extern `gWaterNormalSpeedDirection{One,Two,Three}`, each
   after the matching sample's `...SpeedMax` extern; update the per-sample-row-layout comment.
2. `Engine/Source/Ui/WaterWrappersBase.cpp` — define the three, each after that sample's
   `gLightingSampledNormalsSpeed*Max` (range −XM_PI..XM_PI, default 0.0), matching declaration order.
3. `Engine/Source/Graphics/Render/GlobalUniforms.cpp` `PopulateWaterReducedUv` — read the three φ,
   compute `γ = φ − θ` per sample, replace the `(cos±sin)` scroll deltas per the mechanism above; keep
   the existing rotation `dCos*/dSin*` (still used by `RotatedCamera`). Update the block comment.
4. `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp` — add `{"Speed Direction N", &g...}`
   registrar entries after each `"Speed Max N"`, and `WrapperSlider("Speed Direction N", kiSection, 1.0f)`
   render calls after each `"Speed Max N"` in the Normals column (keep registrar/slider/decl order in
   lockstep per the TweaksScreen ordering convention).

## Docs (step 6)

- `Engine/Source/Graphics/Render/CLAUDE.md` — the "Camera-Relative Double Precision" bullet says scroll
  direction "follows the pattern rotation"; update to: scroll direction is an independent per-sample
  control (world dir `R(φ)·(1,1)`), decoupled from pattern rotation.
- `Engine/Data/Shaders/Water/CLAUDE.md` — the per-sample rotation / reduced-time bullet notes the
  reduced-time delta is pre-rotated by the sample rotation; note the delta direction now derives from
  the independent Speed Direction angle (still pre-rotated CPU-side for the fract pact).

## Non-scope

No new shader uniform or `Water.frag`/`ShaderLayoutsBase.h` change. No change to Speed Min/Max, weight,
or size behavior. No change to color-noise or wave (low/medium) scroll. Determinism N/A (client-only
water visuals, out of CRC).
