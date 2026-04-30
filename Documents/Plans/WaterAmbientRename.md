# Refactor: Water Ambient Uniform Naming Cleanup

Source: follow-up from session that added `fLightingWaterNewAmbient*` (additive ambient term in `Water.frag`).

## Context

Three new uniforms feed a terrain-style additive ambient term in `Water.frag`:

- `fLightingWaterNewAmbient`        (intensity)
- `fLightingWaterNewAmbientPower`   (exponent)
- `fLightingWaterNewAmbientPowerMode` (lum/avg pow blend mode)

These call `AmbientLighting()` on a base-height lighting sample taken at the unreflected projection — a true ambient term, mirroring the terrain shader's ambient path.

The pre-existing pair `fLightingWaterAmbientPower` / `fLightingWaterAmbientPowerMode` (`ShaderLayoutsBase.h:456-457`, consumed `Water.frag:195-211`) is **NOT** an ambient term. It is the exponent + mode for a hue-preserving `pow` that mutates the existing `pf4LightingBaseHeight[3]` array in place before EWNS `WaterLighting()`. Despite the name, it has nothing to do with ambient lighting — it is a per-direction brightness curve on the EWNS lighting samples.

Result: a future reader sees five "Ambient" uniforms in the water path with two completely different semantics, and the "New" prefix on the new trio falsely implies temporary/migration naming when in fact both are intended to coexist permanently.

## Design

Two coupled renames:

1. **Pre-existing pair** (mis-named) — rename to reflect what it actually does (hue-preserving exponent on EWNS lighting samples, not ambient). Candidate names:
   - `fLightingWaterEwnsPow` / `fLightingWaterEwnsPowMode` (terse, ties to EWNS sampling concept used elsewhere in `Water/CLAUDE.md`)
   - `fLightingWaterSampleExponent` / `fLightingWaterSampleExponentMode` (verbose but self-documenting; no EWNS jargon)

   Recommendation: `fLightingWaterEwnsPow` / `fLightingWaterEwnsPowMode`. Matches existing project shorthand (EWNS is documented in `Engine/Data/Shaders/CLAUDE.md` and `Water/CLAUDE.md`); consistent with `Pow`/`PowMode` suffix used by the terrain shader's hue-preserving curves.

2. **New trio** (currently `*New*`-prefixed) — drop the `New` prefix:
   - `fLightingWaterNewAmbient`           → `fLightingWaterAmbient`
   - `fLightingWaterNewAmbientPower`      → `fLightingWaterAmbientPower`
   - `fLightingWaterNewAmbientPowerMode`  → `fLightingWaterAmbientPowerMode`

   The names `fLightingWaterAmbientPower` / `fLightingWaterAmbientPowerMode` become free once step 1 lands. The terrain shader's `fLightingNewAmbient*` is stuck with a similar legacy `New` prefix from a prior migration — that is a separate cleanup tracked elsewhere; this plan does not touch it.

Order: do both renames in the same session — step 1 frees the names step 2 wants. Doing them separately requires a temporary intermediate name, which is wasted churn.

### Secondary cleanup (already partly done)

In `WrapperBase.h`, before this session, `gLightingWaterAmbientPower` / `gLightingWaterAmbientPowerMode` were declared in the "New Lighting" cluster (~lines 382-384) while `WrapperBase.cpp` defined them in the "Water specular lighting" cluster. The .h declarations were moved into the "Water specular lighting" cluster (lines 395-400) to align with the .cpp — that part is already done. Preserve that alignment when renaming; do not split the cluster again.

## Critical files

- `Engine/Data/Shaders/ShaderLayoutsBase.h` — field renames (lines 456-460)
- `Engine/Data/Shaders/Water/Water.frag` — consumer (lines 195-211 for the EWNS pow; the new ambient call site)
- `Engine/Source/Ui/WrapperBase.h` — `extern Wrapper g*` declarations (lines 396-400, "Water specular lighting" cluster)
- `Engine/Source/Ui/WrapperBase.cpp` — `Wrapper g*` definitions (matching cluster)
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp` — slider label strings
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` — map keys + pointer targets
- `Engine/Source/Graphics/Render/LightingUniforms.cpp` — field-to-field copy into the uniform struct

## Notes

- All five names appear only in the seven files above — no shader includes outside `Water.frag`, no game-side references, no save-format references.
- The hue-preserving pow code (`Water.frag:198-211`) keeps its current shape; only the uniform names change. The local `fWaterAmbientPowerMode` shadow at line 199 should rename in lockstep with the uniform.
- After landing, consider whether the `fLightingWaterEwnsPow` block in `Water.frag` should move into a small helper function to mirror `AmbientLighting()`'s call shape — out of scope here, but the rename makes that future extraction obvious.
