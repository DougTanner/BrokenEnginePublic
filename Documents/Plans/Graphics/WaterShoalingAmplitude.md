<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-03T16:27:15.589Z","dependsOn":[]} -->
# Replace Beach Fade with a shoaling/breaking amplitude curve and bake the Jacobian foam signal

## Context

The current shore treatment only shrinks waves toward shore, which is backwards relative to real shoaling: `WaterDisplacement.comp` line 48 computes `fShoreAmplitude = clamp((fTerrainElevation - globalLayout.fBeachFadeTop) * globalLayout.fBeachFadeInvRange, 0.0f, 1.0f)` and applies it to the low-band amplitude only, so waves fade out monotonically as water gets shallow. Real waves grow ~1.2-1.4x as depth drops (Green's law: amplitude scales with depth^-1/4) and then collapse abruptly in the breaker zone. From the RTS camera kilometers up, that grow-then-collapse profile is what makes a coastline read as active surf instead of a dead fade.

This plan replaces the Beach Fade pair with a two-factor curve — a capped depth^-1/4 shoaling gain times a breaker-zone collapse — and, nearly for free, bakes the horizontal-displacement Jacobian determinant into the spare `.w` of the normal image, using the `fA`/`fB`/`fD` accumulators the compute prepass already sums. `WaterShorelineFoamBands.md` consumes both the breaker collapse (as `(1 - fBreak)` recomputed from the same uniform) and the baked determinant as foam-intensity signals. The related manual design note `Documents/Features/Graphics/WaterFoam.md` (unimplemented) describes the same determinant bake; this plan implements only the bake, not that note's fragment-side foam.

Depth here is `-fTerrainElevation`, already computed in the prepass from `elevationTextureSampler` minus `globalLayout.fWaterHeight`. `TerrainElevation.frag` applies `pow(fT, fWaterUnderseaCompressionInv)` to underwater samples, so this is an artist-curved shore coordinate, not metric depth — acceptable for a visual prototype and documented as such.

Decision (medium band): the shoaling/breaking factor stays low-band only, exactly where `fShoreAmplitude` applies today. The medium band is fine chop that already reads correctly near shore, and widening the factor would change the documented low-band-only amplitude-fade contract in `Engine/Data/Shaders/Water/AGENTS.md` for no visible gain at gameplay altitude. The medium band is deliberately unchanged.

Risk tier: Tier 2 scoped rendering behavior — client-only visuals with no simulation, serialization, wire, or CRC exposure. Declared trigger for Step 1 review: the change edits `ShaderLayoutsBase.h`, the shared CPU/GLSL uniform layout compiled into both client and server builds; `GlobalLayout` is per-frame, never persisted and never CRC'd, but a reviewer may escalate under data-layout exposure.

## Scope contract

The listed scope is both target and ceiling: smallest complete change satisfying the acceptance criteria, plus only the mechanical necessities (includes, declarations) the named regions require. No foam rendering, no medium-band changes, no refactors of adjacent water code.

## In scope

Only these regions:

- `Engine/Data/Shaders/Water/WaterDisplacement.comp`: the `fShoreAmplitude` computation (line 48) and its uses (loop gate line 67, amplitude multiply line 73), replaced per Design; a new Jacobian-determinant computation after the band loops (after line 121) and the `normalImage` store's `.w` component (line 130); the file header comment describing the shore fade (lines 25-26). The cross-product normal math (lines 124-127), the over-land early-out (lines 52-57), and the medium-band loop stay untouched.
- `Engine/Data/Shaders/ShaderLayoutsBase.h`: replace `fBeachFadeTop` and `fBeachFadeInvRange` (lines 475-476) with `fWaterShoalMax`, `fWaterShoalDepthRefInv`, and `fWaterBreakDepthInv`, with field comments naming the CPU-folded reciprocals.
- `Engine/Source/Ui/WaterWrappersBase.h` (lines 74-75) and `.cpp` (lines 91-92): replace `gWaterBeachFadeTop`/`gWaterBeachFadeBottom` with `gWaterShoalMax`, `gWaterShoalDepthRef`, `gWaterBreakDepth` (ranges per Design).
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp`: replace the two `Beach Fade` registrar entries (lines 77-78) and the matching `Low` tab slider block with a three-slider `Shoaling` block (`Shoal Max`, `Shoal Depth Ref`, `Break Depth`), preserving registrar/slider order alignment.
- `Engine/Source/Graphics/Render/WaterUniforms.cpp` `PopulateWaterParameters`: replace the two uploads at lines 208-209 with the three new fields, folding `1.0f / gWaterShoalDepthRef.Get()` and `1.0f / gWaterBreakDepth.Get()` CPU-side.
- `Engine/Data/Shaders/Water/AGENTS.md`: via Step 6 `/update-claude-docs`, reword the shore-fade sentence (currently "Terrain amplitude fade applies only to the low-frequency Gerstner band") to name the shoaling/breaking curve, still low-band only.

## Out of scope

- Any foam rendering, new varyings, or `Water.frag`/`Water.vert` changes — `Water.vert`'s shore Z-taper (lines 71-74) and over-land path stay byte-identical; foam belongs to `WaterShorelineFoamBands.md`.
- The medium-band amplitude, both bands' Gerstner math, steepness, camera fades, and the flat-normal blend (`fWaterWaveNormalBlend`).
- `Documents/Features/Graphics/WaterFoam.md` (read-only reference), simulation code (`Engine/Source/Frame/IslandTerrain.cpp` and everything CRC-relevant), DataPacker, and terrain shaders.
- Backward compatibility for the removed Beach Fade fields, wrappers, sliders, or any persisted-settings migration.

## Design

In `WaterDisplacement.comp`, after the over-land early-out (so `fDepth > 0` is guaranteed):

```glsl
float fDepth = -fTerrainElevation;
// Green's law shoaling: amplitude ~ depth^-1/4, capped so shallow water grows waves ~1.2-1.4x.
float fShoal = clamp(pow(max(fDepth, 1e-4f) * globalLayout.fWaterShoalDepthRefInv, -0.25f), 1.0f, globalLayout.fWaterShoalMax);
// Breaker collapse: 1 at/beyond break depth, 0 at the shoreline.
float fBreak = clamp(fDepth * globalLayout.fWaterBreakDepthInv, 0.0f, 1.0f);
float fShoreAmplitude = fShoal * fBreak;
```

`fShoreAmplitude` keeps its name, its `> 0.0f` low-band loop gate, and its per-wave amplitude multiply, so the low-band-only application point is unchanged. Note the factor now exceeds 1.0 between the reference depth and the breaker zone — that is the point.

After the band loops, compute the determinant from the existing accumulators and store it in the spare normal-image alpha:

```glsl
float fJacobianDet = (1.0f - fA) * (1.0f - fD) - fB * fB; // → 0 as the surface compresses toward breaking
imageStore(normalImage, i2Coord, vec4(f3Normal, fJacobianDet));
```

Do not alter `f3Tangent`/`f3Bitangent`/`cross` or the `fWaterWaveNormalBlend` mix — the cross-product normal form and the single prepass flat-normal dampener are documented contracts. With both bands skipped (zoom-out fades), accumulators stay at identity so the determinant is 1.0 (no compression — correct no-foam signal). The over-land early-out keeps storing `.w = 0`; those texels are never fetched by `Water.vert` (it skips the fetch when `fTerrainElevation >= 0`), so the value is unobservable — note this in the store's comment.

Wrapper defaults/ranges: `gWaterShoalMax(1.3f, 1.0f, 2.0f)`; `gWaterShoalDepthRef` and `gWaterBreakDepth` are depths in the curved shore-coordinate units of the old fade (old defaults `gWaterBeachFadeTop(-0.06f, -0.1f, 0.0f)`, `gWaterBeachFadeBottom(-0.14f, -0.2f, -0.07f)`), so start with `gWaterShoalDepthRef(0.14f, 0.02f, 0.5f)` and `gWaterBreakDepth(0.06f, 0.01f, 0.3f)` — visually equivalent onset to today's fade window, tuned live from the Tweaks screen.

Implementation order: shader layout fields → wrappers/uniform upload → sliders → shader → repack + build → harness screenshots.

## Critical files

- `Engine/Data/Shaders/Water/WaterDisplacement.comp` — curve replacement and determinant bake.
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — shared CPU/GLSL field replacement (types, order, and comments kept in sync for both builds).
- `Engine/Source/Ui/WaterWrappersBase.h`/`.cpp`, `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp` — tunables and UI, order-aligned.
- `Engine/Source/Graphics/Render/WaterUniforms.cpp` — CPU-folded uploads.
- `Engine/Data/Shaders/Water/Water.vert` — read-only verification site: Z-taper and over-land path unchanged; its Gerstner-parity comment references the prepass fade and must still be true.
- `Engine/Data/Shaders/Water/AGENTS.md` — shore-fade contract sentence updated in Step 6.

## Risk triggers and invariants

- CRC safety: every changed file is client-render-only (`Engine/Data/Shaders/Water`, `Graphics/Render`, `Ui`); nothing reads from or writes to simulation state, so the per-tick CRC is unaffected by construction. `Engine/Source/Frame/IslandTerrain.cpp` must not change.
- Water shader contracts (`Engine/Data/Shaders/Water/AGENTS.md`): keep the full cross-product Jacobian normal (never the simplified first-order normal); the flat-normal dampener stays applied once, in the prepass; amplitude shaping stays low-band only.
- `ShaderLayoutsBase.h` is compiled into client and server (`BT_ENGINE` selects declarations, not affinity): field types, alignment, and order must stay CPU/GLSL identical, and the folded reciprocals belong CPU-side per the uniform-only-terms rule.
- Wrapper/registrar order alignment (`TweaksScreen` contract) — the debug audit reports missing or orphaned registrations.

## Acceptance criteria

- Repository-wide search shows zero remaining references to `fBeachFadeTop`, `fBeachFadeInvRange`, `gWaterBeachFadeTop`, `gWaterBeachFadeBottom`.
- Diff shows the normal-image store is `vec4(f3Normal, fJacobianDet)` with the tangent/bitangent/cross lines byte-identical.
- `/compile` builds the client Debug|x64 after DataPacker shader repack; the TweaksScreen registration audit reports no missing/orphaned water sliders.
- `/agent-harness`: at gameplay altitude over an island shoreline, a screenshot with default sliders shows wave crests visibly larger just seaward of the shore than in open water, collapsing to near-flat at the waterline (versus the current monotonic fade); an open-ocean screenshot is visually unchanged from a pre-change baseline. Raising `Break Depth` in the Tweaks screen visibly widens the flat nearshore band in a follow-up screenshot.

## Notes

- Reference: Green's law — shallow-water wave amplitude grows as depth^-1/4 before breaking; this plan caps that growth at `Shoal Max`.
- `WaterShorelineFoamBands.md` depends on this plan for `fWaterBreakDepthInv` and the baked determinant; keep both names stable.
