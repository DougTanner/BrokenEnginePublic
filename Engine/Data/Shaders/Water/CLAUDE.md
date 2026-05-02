# Water/ - Ocean Water Shaders

Vertex-animated ocean surface with Gerstner wave simulation, Fresnel-based skybox reflections, and depth-based coloring.

## Overview

Tessellated quad covering the visible area. Vertex shader sums Gerstner waves at two frequency bands; fragment shader composites depth-LUT color, skybox reflection, directional lighting, shadows, specular, and additive smoke. Normal and noise UVs use camera-relative positions combined with CPU-reduced double-precision origins to survive far-from-origin floating-point precision.

## Architecture Notes

- **Shore-fade asymmetry**: amplitude fade from terrain elevation is applied only to low-frequency waves; medium waves run at full amplitude everywhere.
- **Zoom-out amplitude fade**: CPU populates Gerstner geometric amplitudes (low- and medium-frequency) scaled by a camera-height factor that fades to zero between the default eye height and 2x. Applied after steepness clamps so the steepness invariant is preserved. Eliminates aliasing of submillimeter wave geometry at far zoom; shader is unaware.
- **Z flattening near shore**: once terrain elevation exceeds the negative water-terrain threshold, displaced Z is rescaled toward zero so waves taper against the beach rather than clipping through terrain.
- **Early-out divergence**: vertex shader emits a degenerate clip-space position to kill culled verts; fragment shader writes zero when its own (looser) elevation threshold is exceeded. Thresholds and mechanisms are intentionally different.
- **Varying conventions**: camera-relative initial position drives normal/noise UV math; fully-displaced world position drives lighting/shadow/smoke world-space lookups; a separate visible-area texcoord drives elevation, shadow, and object-shadow sampling.
- **Precision-safe normal sampling**: `textureGrad` with derivatives of the camera-relative position and `fract()`-wrapped UVs — derivatives are taken before per-octave scaling so mip selection stays stable across summed octaves.
- **Decoupled normal blends**: wave-vs-sampled normal blend for lighting and for skybox reflection sampling are independent, letting the two use different smoothness.
- Fresnel uses a `(1 - cosθ)^4` falloff (not Schlick's canonical `^5`) — intentional visual tuning.
- Base-height projection projects the undisplaced world position down to the lighting plane for both EWNS lighting sampling and the smoke texcoord, keeping lighting stable under wave animation. EWNS sampling (3-sample directional, used by `WaterLighting`) optionally blends in a reflected projection — the eye→water ray reflected about the water normal, projected to the same base-height plane — to suggest sky/sun light bouncing off the surface. A separate additive ambient term samples the precomputed `mAmbientCombineTexture` once at the unreflected projection (replacing three EWNS samples) to mirror the terrain shader's ambient path; the smoke blend reuses the same single ambient fetch via `BlendSmokePrecomputed` (multiplied by 4 to recover the un-averaged sum), so smoke is decoupled from the wave normal entirely — matching `Terrain.frag`.
- Hue-preserving pow on base-height lighting samples blends luminance-based and average-based exponents via a mode uniform, applied before lighting.
- Time-of-day multiplier applies linearly after lighting's internal pow, scaling output without re-exponentiation.
- Moon brightness scalar (`fLightingWaterMoonBrightness`, range [0.5, 4.0], default 1.0) is CPU-lerped from 1.0 to the slider value via a "night amount" derived from `fShadowMoonMultiplier` — 0 throughout day, 1 throughout night, smooth in narrow sunrise/sunset windows. Reusing `fShadowMoonMultiplier` keeps the moon-brightness gate in lockstep with the shadow night-gate. The result is multiplied into `f3LightingColor` immediately before the shadow application, scaling the full sky-disk-derived path (sun/moon directional, scalar `fSunlight` tint, sun-tinted skybox reflection) as a single block; the EWNS-deposited paths (`WaterLighting()` and `AmbientLightingPrecomputed()`) are intentionally untouched, and the ambient floor below the shadow line stays at its anti-black level.
- Output alpha encodes water transparency from terrain elevation, clamped to a minimum to keep deep water opaque.
- Ambient floor clamps output below by a fraction of ambient * skybox so fully-shadowed pixels never go black.
