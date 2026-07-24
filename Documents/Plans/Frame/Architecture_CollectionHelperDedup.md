<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T03:39:13.000Z","dependsOn":[]} -->
# Architecture: Collection & IslandTerrain Helper Dedup

## Context

Source: /external-architecture-review on Engine/Source (recursive). Cross-file duplication in the collection leaves and IslandTerrain: a subtle lighting-texel correctness rule duplicated verbatim in two render TUs, controller-collection machinery repeated one level below the shared `CollectionController.h` helpers, and duplicated heightmap-sampling/normal math in `IslandTerrain.cpp`. Render-path or client-only except the one CRC-feeding region called out below. Four independent work items; each is behavior-preserving extraction — no runtime behavior changes anywhere.

This plan is self-contained: implement exactly the four items below as specified. Line numbers are citations into the current tree; re-locate by the named symbols if lines have shifted.

## Design

### Item 1 — Shared minimum-lighting-size helper [~15m]

The comment block + 4-line computation deriving the 8-texel minimum lighting size (which the un-bumped `TextureManager::DetailTextureSize` basis makes correct — the lighting-headroom pre-size cancels in the shader texel formula) is duplicated at `AreaLightsRender.cpp:61-69` (consumed at :86 as `fMinLightingSize`) and `PointLightsRender.cpp:63-71` (consumed at :79 as `fMinLightingArea`).

- Extract one helper, `float MinLightingDepositSize()`, declared in `Engine/Source/Graphics/GraphicsUtils.h` next to `BuildAxisAlignedQuad` (the existing "Shared rendering helpers for collections" group, inside the same `BT_CLIENT` region) and defined in `GraphicsUtils.cpp`. Body: the existing `DetailTextureSize(gLightingDepositTextureMultiplier.Get())` + per-axis `std::ceil` texel-size + `std::max(...) * 8.0f` computation, with the full explanatory comment moved onto the helper (single copy).
- Both render TUs call it once before their per-element loop and keep their existing local variable names.

### Item 2 — `CollectionController.h` shared controller helpers [~45m + ~45m]

Two small helpers (not one combined template), added to `Engine/Source/Frame/Collections/CollectionController.h` next to `InterpolateKeyframes`:

**2a. Wrapper-scaled interpolate helper.** The controlled-update body — copy the controller type by value, run the per-keyframe `Wrapper*` scale loop, then `InterpolateKeyframes` — is duplicated at `PointLightsUpdate.cpp:44-65` (inside `PointLightsInterpolate::Update`, controlled branch :39-72) and `PuffsUpdate.cpp:34-47` (inside `PuffsInterpolate::Update`, controlled branch :29-53).

- Add a function template taking `(const TControllerType& rController, float fElapsedTime, scaleCallable)` and returning the interpolated keyframe. Body: copy `rController` by value, invoke `scaleCallable(scaledCopy, rController, j)` for each keyframe index `j < rController.uiKeyframeCount`, then return `InterpolateKeyframes(scaledCopy, fElapsedTime)`. The callable absorbs the per-type field set: PointLights scales `fVisibleArea`/`fVisibleIntensity`/`fLightingArea`/`fLightingIntensity` via `ppVisibleAreaScales` etc.; Puffs scales `fArea`/`fIntensity` via `ppAreaScales`/`ppIntensityScales`. Null-wrapper checks stay in the callable exactly as written today.
- Call sites keep their surrounding SOA load, `kuiInvalidControllerType` check, elapsed-time computation, field mapping, and SOA save.
- `WindRadialsInterpolate::Update` (`WindRadialsUpdate.cpp:22-40`) stays hand-written: it has no wrapper-scale loop, runs the controller unconditionally (no `kuiInvalidControllerType` check), and multiplies base magnitudes after interpolation. Do not force-fit it into the helper.

**2b. `AddControlled` spawn-seed helper.** The spawn skeleton — `GrowPairedCollections` → add element → W=1 position write → base type index from controller → keyframe-0 seed → controller bookkeeping (`puiControllerTypeIndices`, `pfStartTimes`) — repeats at `PointLightsUpdate.cpp:131-158`, `PuffsUpdate.cpp:67-90`, `WindRadialsUpdate.cpp:47-70`.

- Add a function template parameterized on a keyframe-0→SOA-write callable that receives the spawn index and writes the per-type seeded columns. Divergences the helper must absorb, all via its parameters/callable — not by branching inside the helper:
  - PointLights is indexable: `AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender)` plus the `rPostRender.puiIds[uiSpawnIndex] = newId` write. Puffs/WindRadials use bare `AddElement(rInterpolate, rPostRender)`. Absorb via the element-add path (e.g. a second callable or two thin overloads — implementer's choice, keep it minimal).
  - Wrapper scaling at seed time: PointLights/Puffs multiply keyframe-0 values by `pp*Scales[0]->Get()` when non-null; WindRadials seeds base value × keyframe-0 with no wrapper scaling. Lives in each type's write-callable.
  - Extra bookkeeping columns stay in the callable: PointLights writes `pfBaseRotations`; WindRadials writes `pfBaseIntensities`/`pfBaseSizes`.
- Each `AddControlled` becomes: fetch controller type, call helper with its callable(s). No public signature of any `AddControlled` changes.

### Item 3 — `IslandTerrain.cpp` sampling and normal dedup [~30m + ~15m]

**3a. Placement-sample helper.** The world→local inverse-rotate / footprint-reject / UV / clamp / half-float-sample block is duplicated between the `CellElevation` per-island loop body (`IslandTerrain.cpp:280-303`) and the `BlendPlacementIntoGrid` per-texel loop body (`IslandTerrain.cpp:387-409`).

- Extract one file-local (anonymous-namespace) inline helper used by both. Both call sites already consume precomputed cos/sin (`IslandRenderQuery::fCos/fSin` from `BuildRenderPlacementCache` or the `CellElevation` fallback; hoisted `common::DeterministicSinCos` locals in `BlendPlacementIntoGrid`), so the helper takes cos/sin values — trig stays outside it.
- One intentional divergence the helper must preserve exactly, parameterized (template parameter, callable, or precomputed-scale arguments — implementer's choice): the UV scale form. `CellElevation` divides by footprint (`fU = fLocalX / rQuery.fFootprintX + 0.5f`, :291-292) while `BlendPlacementIntoGrid` multiplies by hoisted reciprocals (`fU = fLocalX * fInvFootprintX + 0.5f`, :399-400), which is not bit-identical under `/fp:strict`. Likewise preserve `BlendPlacementIntoGrid`'s hoisted `fHeightmapMaxU/V` index scale (:378-379, :402-403) versus `CellElevation`'s per-sample `static_cast<float>(width - 1)` (:294-295). `BlendPlacementIntoGrid`'s float-op sequence must not change in any way.
- Do not migrate `CellElevation` onto the reciprocal-multiply form silently. If unification is chosen instead of parameterization, only the render-only `CellElevation` side may change (at most ulp-scale output shift) and the diff must say so explicitly; the grid-builder side is untouchable.

**3b. Normal template over an elevation callable.** `FrameNormal` (:494-510) and `GlobalNormal` (:512-559) end in an identical 4-tap finite-difference block (four `±fDistance` offsets, `XMVectorSetZ` per tap, `XMVector3Normalize(XMVector3Cross(...))`) at :499-509 and :549-558, differing only in the elevation callee (`FrameElevation(rStaticData, tap)` vs the local cell-caching `SampleElevation` lambda).

- Extract one file-local helper templated on an elevation callable: takes `vecPosition`, `fDistance`, and the callable; returns the normal. `FrameNormal` passes a `FrameElevation` lambda; `GlobalNormal` keeps its existing `SampleElevation` cell-cache lambda (:527-544) and passes it — the caching stays at the call site, unchanged.

### Item 4 — SmokeTrails file layout [~15m]

`SmokeTrails` is the only sibling collection without an Update TU. Move `SmokeTrailsInterpolate::Update`, `SmokeTrailsInterpolate::Sync`, `SmokeTrailsPostRender::Update`, `PreCollision`, `Add`, `Remove`, `PostCollision`, and `AreaDamage` (currently `SmokeTrails.cpp:40-124`, including the `kfSmoothingRate` constant used only by `Update`) from `SmokeTrails.cpp` into a new `SmokeTrailsUpdate.cpp`, mirroring `PuffsUpdate.cpp`'s structure (`#include "SmokeTrails.h"`, whole-file `#if defined(BT_CLIENT)`). `SmokeTrails.cpp` retains the explicit template instantiations, `Register`, `AllocateAndCopy` pair, and the `Spawn`/`Transfer`/`Destroy` stubs.

- Add the new file to the client project only — `BrokenEngineSandbox.vcxproj` + `.filters` (sibling Update TUs are not server members). Run `/update-vcxproj` for exact membership/filter mechanics.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change per item, add no abstractions beyond the named helpers, no configuration, no refactors or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, declarations, forward declarations) the named change requires.

In scope (exact regions):

- `Engine/Source/Graphics/GraphicsUtils.h` / `GraphicsUtils.cpp` — add `MinLightingDepositSize()` declaration + definition only.
- `Engine/Source/Frame/Collections/AreaLights/AreaLightsRender.cpp` — the :61-69 block and its `fMinLightingSize` consumption only.
- `Engine/Source/Frame/Collections/PointLights/PointLightsRender.cpp` — the :63-71 block and its `fMinLightingArea` consumption only.
- `Engine/Source/Frame/Collections/CollectionController.h` — add the two Item-2 helpers only.
- `Engine/Source/Frame/Collections/PointLights/PointLightsUpdate.cpp` — `PointLightsInterpolate::Update` controlled branch and `PointLightsPostRender::AddControlled` only.
- `Engine/Source/Frame/Collections/Puffs/PuffsUpdate.cpp` — `PuffsInterpolate::Update` controlled branch and `PuffsPostRender::AddControlled` only.
- `Engine/Source/Frame/Collections/WindRadials/WindRadialsUpdate.cpp` — `WindRadialsPostRender::AddControlled` only (its `Update` is explicitly untouched).
- `Engine/Source/Frame/IslandTerrain.cpp` — `CellElevation` loop body, `BlendPlacementIntoGrid` per-texel body, `FrameNormal`, `GlobalNormal`, plus the two new anonymous-namespace helpers only.
- `Engine/Source/Frame/Collections/SmokeTrails/SmokeTrails.cpp` / new `SmokeTrailsUpdate.cpp` — the Item-4 function moves only; no body edits to the moved functions.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` + `.filters` — new-file membership entries only.

Out of scope:

- The copy/zero-init contract documented in `Engine/Source/Frame/Collections/AGENTS.md` and implemented by `Engine/Source/Frame/Collections/CollectionMemory.h` — preserve it while changing the same Update TUs; no `CollectionMemory.h` edits.
- `ExplosionsPostRender::Spawn` decomposition — determinism-sensitive RNG-order contract; accepted as-is.
- `ForEach*` hook redesign (`Frame/Architecture_PhaseHookOptIn.md`).
- `WindRadialsInterpolate::Update` — stays hand-written per Item 2a.
- Any change to public signatures, SOA layouts, `SmokeTrails` function bodies, or `BlendPlacementIntoGrid` float-op ordering.

## Critical files

- `Engine/Source/Frame/Collections/CollectionController.h`
- `Engine/Source/Frame/Collections/{PointLights,Puffs,WindRadials,AreaLights,SmokeTrails}/` TUs named above
- `Engine/Source/Graphics/GraphicsUtils.h` / `GraphicsUtils.cpp`
- `Engine/Source/Frame/IslandTerrain.cpp`
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` + `.filters` (new `SmokeTrailsUpdate.cpp`)

## Risk tier and invariants

- **Tier 3** (classified at the highest applicable trigger): `BlendPlacementIntoGrid` feeds the CRC'd sim elevation grid — the Item-3a extraction touches a determinism/CRC surface even though it must be byte-behavior-preserving. All other items are Tier-1/2-shaped client-only render or file-motion work.
- Invariants: `/fp:strict` bit-determinism of the elevation grid (client and server build identical grids); the controller/spawn helpers touch client-only collections (no `SharedMembers()`) — no CRC exposure, but keep `common::Random*` draw order unchanged where callers spawn from RNG-driven code; XMVECTOR W=1 on position writes preserved as-is in moved/extracted code.

## Acceptance criteria

- Client and server targets compile.
- `BlendPlacementIntoGrid`'s emitted float-op sequence is unchanged — decisive by diff inspection of the extracted helper instantiation (parameterization preserves divide-vs-reciprocal and hoisted index scales exactly).
- Behavior-preserving everywhere else: no render-path output change intended; if the optional Item-3a unification path is taken, the diff explicitly states the ulp-scale `CellElevation` shift.
- A replay determinism harness check (`/agent-harness`) is the decisive runtime signal for the Tier-3 surface if the acceptance matrix calls for a runtime check.

## Coordination

- Never interleave with the live frame-collection series `Documents/Plans/Frame/Architecture_PhaseHookOptIn.md`, `Documents/Plans/Frame/CollectionReadIndexHardening.md`; each later landing must refresh shared collection-header/TU citations.
- `IslandTerrain.cpp` is also named by `Documents/Plans/Graphics/IslandHeightmapRouteDedup.md` (touches `WaitForElevationMaps` and the `IslandTerrain` ctor) — different functions, but refresh citations if co-scheduled.

## Notes

- Grill decision (resolved per this plan's own recommendation and the verified code asymmetry): two small helpers rather than one combined template, and WindRadials' wrapper-less interpolate stays hand-written.
- Re-verified against source 2026-07-24. Since the 2026-07-02 draft, the render elevation path was restructured: `GlobalElevation` now delegates to the anonymous-namespace `CellElevation` (precomputed `IslandRenderQuery` cache from `FrameStaticData::BuildRenderPlacementCache`, libm-trig fallback), and `GlobalNormal` gained a cell-caching `SampleElevation` lambda. The original SinCos-source divergence dissolved (both sample sites now consume precomputed cos/sin); the divide-vs-reciprocal UV divergence remains and is load-bearing for `/fp:strict`. Items 3a/3b above are grounded in the current shapes.
- The 2026-07-02 verification note stands for Item 2: the "triplicated" interpolate claim held only for PointLights/Puffs; WindRadials shares the outer skeleton but not the wrapper-scale loop or the controlled/uncontrolled branch, so the helper is not designed to a false symmetry.
