# Architecture: Collection & IslandTerrain Helper Dedup

## Context
Source: /external-architecture-review on Engine/Source (recursive). Cross-file duplication in the collection leaves and IslandTerrain: a subtle lighting-texel correctness rule duplicated verbatim in two render TUs, the controller-collection machinery repeated one level below the shared `CollectionController.h` helpers, and in-file duplicated heightmap-sampling/normal math in IslandTerrain. Render-path only except where noted.

## Design

### Shared minimum-lighting-size helper
- The byte-identical 5-line comment + 4-line computation (which `DetailTextureSize` basis cancels in the shader texel formula) lives at `AreaLightsRender.cpp:62-70` and `PointLightsRender.cpp:64-72` — hoist into one `MinLightingDepositSize()` helper (next to `BuildAxisAlignedQuad` or in a lighting-render utility header) [~15m]

### Engine/Source/Frame/Collections/CollectionController.h
- Extend with a shared wrapper-scaled-interpolate helper: the per-frame controlled-update skeleton (load previous → copy `ControllerType` by value → per-keyframe `Wrapper*` scale loop → `InterpolateKeyframes` → save) is duplicated at `PointLightsUpdate.cpp:23-69` and `PuffsUpdate.cpp:16-54`. `WindRadialsUpdate.cpp:22-40` shares only the outer skeleton — it has no wrapper-scale loop, runs the controller unconditionally (no `kuiInvalidControllerType` check), and multiplies base magnitudes after interpolation — so either parameterize the helper to cover it or leave it out; do not force-fit [~45m]
- Add an `AddControlled` spawn-seed helper parameterized on a keyframe→SOA-write callable: the Grow → Add → W=1 position → keyframe-0 seed → controller bookkeeping skeleton repeats at `PointLightsUpdate.cpp:132-159`, `PuffsUpdate.cpp:61-84`, `WindRadialsUpdate.cpp:47-70`. Divergences the helper must absorb: PointLights is indexable (`AddVisualIndexableElement` + `puiIds` write) while Puffs/WindRadials use bare `AddElement`; WindRadials seeds base × keyframe-0 with no wrapper scaling [~45m]

### Engine/Source/Frame/IslandTerrain.cpp
- One placement-sample inline helper for the world→local inverse-rotate / footprint-reject / UV / clamp / half-float sample block shared by `GlobalElevation` (:254-279) and `BlendPlacementIntoGrid` (:344-361 per-texel body). Two intentional divergences the helper must preserve exactly: (a) SinCos source — `std::cos/std::sin` in the render-only GlobalElevation vs `common::DeterministicSinCos` in the CRC-feeding grid builder; (b) UV scale form — GlobalElevation divides by footprint (`/ fFootprintX`, :268) while BlendPlacementIntoGrid multiplies by a hoisted reciprocal (`* fInvFootprintX`, :353), which is not bit-identical under `/fp:strict`. Parameterize on both (SinCos values and the UV scale op or precomputed reciprocals); `BlendPlacementIntoGrid`'s float-op sequence must not change [~30m]
- One normal template over an elevation callable: `FrameNormal` (:424-440) vs `GlobalNormal` (:442-462) are an identical 16-line 4-tap finite-difference differing only in the elevation callee (the comment says so itself) [~15m]

### SmokeTrails file layout
- Move `Update`/`Sync`/`Add`/`Remove` and the PostRender phase stubs from `SmokeTrails.cpp` into a conventional `SmokeTrailsUpdate.cpp` TU, matching all sibling collections (only SmokeTrails lacks an Update TU; vcxproj + filters entries for the new file) [~15m]

## Critical files
- `Engine/Source/Frame/Collections/CollectionController.h`
- `Engine/Source/Frame/Collections/{PointLights,Puffs,WindRadials,AreaLights,SmokeTrails}/` TUs
- `Engine/Source/Frame/IslandTerrain.cpp`
- Both vcxproj/.filters pairs (new `SmokeTrailsUpdate.cpp`)

## Out of scope
- The copy/zero-init contract work (`Frame/Architecture_CollectionCopyContract.md`) — land these two in separate sessions; they touch the same Update TUs
- `ExplosionsPostRender::Spawn` decomposition — determinism-sensitive RNG-order contract; accepted as-is
- `ForEach*` hook redesign (`Frame/Architecture_PhaseHookOptIn.md`)

## Coordination

- Never interleave with the live frame-collection series `Documents/Plans/Frame/Architecture_CollectionCopyContract.md`, `Documents/Plans/Frame/Architecture_PhaseHookOptIn.md`, `Documents/Plans/Frame/CollectionReadIndexHardening.md`, `Documents/Plans/Frame/CollectionDeserializationHardening.md`; each later landing must refresh shared collection-header/TU citations.

## Notes
- Invariant exposure: `BlendPlacementIntoGrid` feeds the CRC'd sim elevation grid — the sampling-helper extraction must preserve its exact float-op order (`/fp:strict`); parameterize, change nothing else. If `GlobalElevation` is instead migrated onto the reciprocal-multiply form its render-only output shifts by at most one ulp-scale rounding difference — acceptable, but say so in the diff. The controller/spawn helpers touch client-only collections (no `SharedMembers()`) — no CRC exposure, but keep `common::Random*` draw order unchanged where spawn seeding touches it. Everything else render-path or file-motion only
- IslandTerrain.cpp is also named in the Order.md island-residency File Group (`IslandNavContourResidency`, `IslandHeightmapRouteDedup` touch `WaitForElevationMaps`) — different functions, but refresh citations if co-scheduled
- Grill decision: helper granularity for the controller machinery — one templated helper vs two (interpolate + seed); recommend two small ones, and decide whether WindRadials' wrapper-less interpolate joins or stays hand-written

## Verification Notes
- Verified against source 2026-07-02. The original "triplicated" claim held fully for PointLights/Puffs; WindRadials shares the skeleton but not the wrapper-scale loop or the controlled/uncontrolled branch — wording adjusted so the helper isn't designed to a false symmetry. The IslandTerrain divide-vs-reciprocal divergence was not in the original draft and is load-bearing for the `/fp:strict` constraint
