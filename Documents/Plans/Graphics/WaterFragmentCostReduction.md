# Water Fragment Cost Reduction (fetch diet + uniform hoists)

## Context

`kGpuTimerWater` is the most expensive GPU pass: **2329 µs current / 2248 avg / 2335 max** (Profile build, 120 fps, idle connected scene — ocean + islands, no combat), ~35% of the ~6.6 ms GPU frame (Terrain 998 µs, Objects 229 µs, Image pass total 3606 µs). Idle-scene caveat: no smoke/combat was present, which is exactly the case item 1 below exploits.

`Water.frag` performs **22 texture fetches per fragment** at near-full-screen coverage: 1 elevation, 9 wave-normal octaves (3 groups x 3 `textureGrad`), 2 color noise, 1 depth LUT, 1 skybox, 1 smoke (in `SmokeShadow`), 1 object shadows, 1 terrain shadow, 3 EWNS lighting (`ReadLighting`), 1 ambient, 1 smoke (final `BlendSmokePrecomputed` feed) — plus heavy ALU (three analytic-AA specular lobes, the EWNS pow loop, the reflected base-height projection, and three per-group `cos`/`sin` rotation pairs). Several of these are payable only when their feature is actually active; today they are unconditional.

## Design

Ranked items, cheapest-per-µs first. Each is independently landable; all are `Water.frag` + uniform-population edits (shader repack).

### 1. Smoke-inactive gate (2 fetches + blend ALU when no smoke)

Wrap the `SmokeShadow(...)` call and the final smoke fetch + `BlendSmokePrecomputed` block in a warp-coherent `if` on a new uniform (e.g. `GlobalLayout::fSmokeActive`), following the exact precedent of the existing `if (fWeight* > 0.0f)` normal-group gates. CPU signal must be conservative because smoke decays after its sources die: set active while any smoke deposit occurred within the last N seconds (grill: tracking site — the smoke deposit path / `SmokeUniforms` knows per-frame deposit activity — and the linger threshold; a too-long window is safe, merely forfeiting the win). When inactive the smoke texture is uniformly zero, so skipping is exact.

- **Visual impact: (a) no visual change** (gate is exact when the smoke texture is zero; conservative window guarantees it). No new slider needed; the linger threshold can be a Tweaks slider (suggested 0–30 s, default ~10 s) if the grill prefers tunable over constant.
- Idle-scene saving: 2 of 22 fetches plus pow/mix ALU ≈ 5–10% of the pass ≈ **100–230 µs** (measure).

### 2. Hoist per-group normal-rotation `cos`/`sin` into uniforms

`Water.frag` computes `cos`/`sin` of `MainLayout::fWaterNormalRotation{One,Two,Three}` and builds two `mat2`s per group **per fragment** — six transcendentals plus matrix construction for values that are per-frame constants. Upload the pairs precomputed (e.g. `f4WaterNormalRotation{One,Two,Three}` = (cos, sin, -sin, cos) per group, or vec2 pairs) from the same population site that uploads the rotation angles today (the water-normal block in `Engine/Source/Graphics/Render/` — `LightingUniforms.cpp` resolves the group weights and already rotates the reduced origins CPU-side with the same angles, so the values are on hand). Shader consumes the uniforms directly; the angle uniforms become unused and are removed.

- **Visual impact: (a) no visual change** (same rotation values, computed once on CPU instead of 8M times on GPU). No slider.
- Saving: pure ALU, small — likely **10–40 µs**; bundle with item 1's repack rather than landing alone.

### 3. Far-zoom shutdown of the finest normal group (tuning, zero code)

The per-group camera-height weight fade (`LightingUniforms.cpp` → `fWaterNormalWeight{One,Two,Three}`) already skips a group's 3 fetches when its resolved weight is 0 — but the shipped Min endpoints are all nonzero (`gLightingSampledNormalsWeightThreeMin` = 0.5, `TwoMin` = 0.25), so all 9 fetches run at every zoom. Recommend defaults: `ThreeMin` → 0.0 (its 0.02-size octaves are deep sub-pixel at far zoom), optionally `TwoMin` → 0.0. If the fade visibly flattens far-water roughness to gloss, flip `WATER_SPEC_AA_FADE_HANDOFF` (compile-time define in `Water.frag`, currently 0) — it exists precisely to hand the faded group's statistical roughness to the specular kernel.

- **Visual impact: (b) minor/imperceptible at the heights where the weight reaches 0** — the octaves being dropped are sub-pixel there; the FADE_HANDOFF define restores their specular-roughness contribution analytically. Slider: **already exists** (`gLightingSampledNormalsWeight{Two,Three}Min`, range 0–4; suggested defaults 0.0 / 0.0 with Max endpoints unchanged).
- Saving: 3 of 22 fetches, only in the faded-out zoom regime ≈ **up to ~150 µs at far zoom**, 0 at close zoom.

### 4. Rider: gate the reflected-projection ALU when disabled

The reflected base-height projection (reflect, ray-project, falloff `pow`, Fresnel) runs unconditionally; when the tuning product `fLightingWaterReflectedAmount * fLightingWaterReflectedIntensity` is 0 the result is fully discarded by the `mix`. Wrap in a warp-coherent uniform check. **Visual impact: (a)** when disabled, nothing otherwise. Pure-ALU micro-win; take only because the file is already open.

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` — `SmokeShadow` call site, final smoke block, `SAMPLE_NORMAL_PRECISE` group prologues (rotation `mat2` builds), reflected-projection block
- `Engine/Data/Shaders/ShaderLayoutsBase.h` (or the game `ShaderLayouts.h` extension) — new `fSmokeActive` + rotation-pair uniforms, removal of the raw angle uniforms if fully migrated
- `Engine/Source/Graphics/Render/LightingUniforms.cpp` — water-normal weight/rotation population (items 2, 3)
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` / `SmokeUniforms` population site — `fSmokeActive` write (item 1; grill picks the activity source)
- `engine::gLightingSampledNormalsWeight{Two,Three}Min` — `Engine/Source/Ui/WaterWrappersBase.cpp` (item 3 default change only)

## Out of scope

- Mesh density / overshading (see `WaterMeshDensityOvershading.md`)
- Half-resolution water shading (see `WaterHalfResolutionShading.md`)
- Replacing the fragment elevation fetch with a vertex varying — **considered and rejected**: the vertex grid is ~4x coarser than the elevation texture, so interpolated elevation would visibly soften the shore fade/depth-LUT band, a signature look
- Stencil/depth masking of water under islands — **already achieved**: terrain draws before water in the same pass with depth write, so under-island water fragments are early-Z-killed; the in-shader `discard` only covers the shore fringe
- Reducing EWNS `pow` work — the `fPowerMode` extremes already skip the discarded branch warp-coherently
- Any change to the specular AA mode/machinery

## Notes

- Invariant exposure: client/graphics-only; **shader repack required** (Water.frag + layout header); scalar-block-layout append rules apply to the new uniforms; no determinism/CRC/`kiVersion`/wire exposure; no per-frame allocation-path changes.
- Item 1's `fSmokeActive` must also be honored (or deliberately not) by `Terrain.frag`'s smoke path for consistency — decide at grill; the win there is smaller (Terrain is 998 µs total) but the uniform is shared.
- Pre-staged grill decisions: (a) smoke-activity tracking site + linger threshold (constant vs slider); (b) whether item 3 ships as default change or stays a documented tuning recommendation.
