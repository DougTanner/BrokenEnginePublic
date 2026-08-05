# Water Shaders - Precision-Stable Ocean Rendering

Water renders a visible-area ocean grid. A compute prepass bakes Gerstner displacement and full-Jacobian normals into two RGBA16F textures; the vertex stage fetches one texel per active-grid vertex, and the fragment stage combines depth color, sampled normals, reflections, lighting, shadows, and smoke.

## Geometry and Shore Contracts

- Active LOD grid dimensions must match both displacement textures. Water-mesh recreation recreates the grid and textures together.
- Low-frequency Gerstner amplitudes come directly from the base values in `MainLayout.pf4LowWavesTwo[i].y`; displacement applies no depth-based Green's-law shoaling multiplier.
- The low-frequency Gerstner band's breaker boundary weight transitions from `Break Start Depth` (`S`) to `Break End Depth` (`E`). When `S < E`, normalized depth `t` is shaped as `pow(t, exponent)` by `Break Blend Curve` (range `0.125`–`8`, default `1` is linear; values below `1` introduce low earlier and values above `1` retain medium longer); the clamped endpoints remain exact. When `S >= E`, the boundary is a hard step at `E`.
- The medium-frequency band starts with the complementary boundary weight `1 - low`. After both boundaries are calculated, one smooth shore envelope over physical depth `[0, Medium Shore Softness]` multiplies the medium weight only, taking medium to zero at terrain while leaving the low boundary unchanged; `Medium Shore Softness` `0` bypasses the envelope. Both weights remain bounded. The shore Z taper applies to both bands; over-land vertices remain flat and fragments discard above the configured terrain threshold.
- Compute accumulates tangent and bitangent derivatives and uses their cross product for the wave normal. Do not replace that full cross-product form with the simplified first-order normal: as a band's summed steepness x frequency x amplitude approaches 1 — the point where Gerstner waves start folding over on themselves — the simplified normal drifts away from the true one and eventually tilts the wrong way, while the cross product stays correct.
- The flat-normal dampener is applied once, in the compute prepass, where the baked wave normal is blended toward straight up. Tune it at that source; do not add a second fade in the vertex or fragment stage.

## Precision Contracts

- Normal and noise sampling uses camera-relative positions, `textureGrad`, and `fract`-wrapped UVs. Derivatives are formed before scale multiplication so mip selection remains stable.
- CPU code supplies reduced, pre-rotated origins and reduced time offsets. Do not rotate them again in the shader. Sampling multipliers must preserve the integer-product modulus contract documented by Render (`../../../Source/Graphics/Render/AGENTS.md`).
- Independently rotated normal samples are transformed back into world orientation before weighted composition. Guard a zero weighted sum before normalization so fully faded samples cannot produce NaN.

## Lighting Contracts

- Analytic specular antialiasing is selected by `WATER_SPEC_AA_MODE`. How it passes the per-mip variance from one stage to the next is selected by `WATER_SPEC_AA_MIP_HANDOFF`, which consumes DataPacker's BC5 per-mip variance tables. Changes to either switch require shader repacking; keep analytic LOD and water-normal sampler bias aligned.
- Shoreline skybox-lobe reductions are applied once before every specular antialiasing branch: zero preserves a lobe, one removes it at the shoreline, and deep water remains unchanged.
- Specular beach-lobe reduction uses `Break End Depth` as its depth threshold; it does not use the displacement blend interval or `Medium Shore Softness`.
- Lighting and smoke sample at a base-height projection derived from the undisplaced world position, keeping them stable under wave animation.
- Sun-driven energy and skybox sun/moon specular use full effective shadow. Sky ambient uses the separately controlled ambient-shadow mix; EWNS (defined in the shader hub) and ambient emitter deposits remain unshadowed.
- Terrain ray-march shadow attenuates the sun component but not moon light. Object shadow and smoke attenuation apply to both.
