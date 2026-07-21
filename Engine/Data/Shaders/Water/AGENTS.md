# Water Shaders - Precision-Stable Ocean Rendering

Water renders a visible-area ocean grid. A compute prepass bakes Gerstner displacement and full-Jacobian normals into two RGBA16F textures; the vertex stage fetches one texel per active-grid vertex, and the fragment stage combines depth color, sampled normals, reflections, lighting, shadows, and smoke.

## Geometry and Shore Contracts

- Active LOD grid dimensions must match both displacement textures. Water-mesh recreation recreates the grid and textures together.
- Terrain amplitude fade applies only to the low-frequency Gerstner band. The shore Z taper applies to both low and medium bands; over-land vertices remain flat and fragments discard above the configured terrain threshold.
- Compute accumulates tangent and bitangent derivatives and uses their cross product for the wave normal. Do not replace the full Jacobian with the simplified first-order normal near the allowed steepness limit.

## Precision Contracts

- Normal and noise sampling uses camera-relative positions, `textureGrad`, and `fract`-wrapped UVs. Derivatives are formed before scale multiplication so mip selection remains stable.
- CPU code supplies reduced, pre-rotated origins and reduced time offsets. Do not rotate them again in the shader. Sampling multipliers must preserve the integer-product modulus pact documented by [Render](../../../Source/Graphics/Render/AGENTS.md).
- Independently rotated normal samples are transformed back into world orientation before weighted composition. Guard a zero weighted sum before normalization so fully faded samples cannot produce NaN.

## Lighting Contracts

- Analytic specular antialiasing is selected by `WATER_SPEC_AA_MODE`. Its mip-variance handoff is selected by `WATER_SPEC_AA_MIP_HANDOFF` and consumes DataPacker's BC5 per-mip variance tables. Changes to either switch require shader repacking; keep analytic LOD and water-normal sampler bias aligned.
- Lighting and smoke sample at a base-height projection derived from the undisplaced world position, keeping them stable under wave animation.
- Sun-driven energy and skybox sun/moon specular use full effective shadow. Sky ambient uses the separately controlled ambient-shadow mix; EWNS and ambient emitter deposits remain unshadowed.
- Terrain ray-march shadow attenuates the sun component but not moon light. Object shadow and smoke attenuation apply to both.
