# Architecture: Collection & IslandTerrain Helper Dedup

## Context
Source: /external-architecture-review on Engine/Source (recursive). Cross-file duplication in the collection leaves and IslandTerrain: a subtle lighting-texel correctness rule duplicated verbatim in two render TUs, the controller-collection machinery triplicated one level below the shared `CollectionController.h` helpers, and in-file duplicated heightmap-sampling/normal math in IslandTerrain. Render-path only except where noted.

## Design

### Shared minimum-lighting-size helper
- The byte-identical 5-line comment + 4-line computation (which `DetailTextureSize` basis cancels in the shader texel formula) lives at `AreaLightsRender.cpp:62-70` and `PointLightsRender.cpp:64-72` — hoist into one `MinLightingDepositSize()` helper (next to `BuildAxisAlignedQuad` or in a lighting-render utility header) [~15m]

### Engine/Source/Frame/Collections/CollectionController.h
- Extend with a shared wrapper-scaled-interpolate helper: the per-frame controlled-update skeleton (load previous → copy `ControllerType` by value → per-keyframe `Wrapper*` scale loop → `InterpolateKeyframes` → save) is triplicated at `PointLightsUpdate.cpp:23-69`, `PuffsUpdate.cpp:16-54`, `WindRadialsUpdate.cpp:22-40` [~45m]
- Add an `AddControlled` spawn-seed helper parameterized on a keyframe→SOA-write callable: the Grow → Add → W=1 position → keyframe-0 × wrapper-0 seed → controller bookkeeping skeleton is triplicated at `PointLightsUpdate.cpp:132-159`, `PuffsUpdate.cpp:61-84`, `WindRadialsUpdate.cpp:47-70` [~45m]

### Engine/Source/Frame/IslandTerrain.cpp
- One placement-sample inline helper parameterized on the SinCos source: the world→local inverse-rotate / footprint-reject / UV / clamp / half-float sample block is duplicated between `GlobalElevation` (:254-279, `std::cos/sin`, render-only) and `BlendPlacementIntoGrid` (:308-363, `common::DeterministicSinCos`, CRC'd sim) — the SinCos split is intentional; the shared sampling math should not be able to drift independently of that intent [~30m]
- One normal template over an elevation callable: `FrameNormal` (:424-440) vs `GlobalNormal` (:442-462) are an identical 16-line 4-tap finite-difference differing only in the elevation callee (the comment says so itself) [~15m]

### SmokeTrails file layout
- Move `Update`/`Add`/`Remove` from `SmokeTrails.cpp` into a conventional `SmokeTrailsUpdate.cpp` TU, matching all sibling collections (vcxproj + filters entries for the new file) [~15m]

## Critical files
- `Engine/Source/Frame/Collections/CollectionController.h`
- `Engine/Source/Frame/Collections/{PointLights,Puffs,WindRadials,AreaLights,SmokeTrails}/` TUs
- `Engine/Source/Frame/IslandTerrain.cpp`
- Both vcxproj/.filters pairs (new `SmokeTrailsUpdate.cpp`)

## Out of scope
- The copy/zero-init contract work (`Frame/Architecture_CollectionCopyContract.md`) — land these two in separate sessions; they touch the same Update TUs
- `ExplosionsPostRender::Spawn` decomposition — determinism-sensitive RNG-order contract; accepted as-is
- `ForEach*` hook redesign (`Frame/Architecture_PhaseHookOptIn.md`)

## Notes
- Invariant exposure: `BlendPlacementIntoGrid` feeds the CRC'd sim elevation grid — the sampling-helper extraction must preserve exact float-op order (`/fp:strict`); parameterize on the SinCos callable, change nothing else. The controller/spawn helpers touch client-only collections (no `SharedMembers()`) — no CRC exposure, but keep `common::Random*` draw order unchanged where spawn seeding touches it. Everything else render-path or file-motion only
- Grill decision: helper granularity for the controller machinery — one templated helper vs two (interpolate + seed); recommend two small ones
