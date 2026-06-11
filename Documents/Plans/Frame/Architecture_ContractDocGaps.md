# Architecture: Frame Contract Doc Gaps

## Context

Source: /external-architecture-review on `Engine/Source/Frame` (non-recursive). Three verified-intentional contracts in this directory are undocumented (or documented only at one buried site), each a known trap for the next reader. Doc-only plan; zero code change.

## Design

### Engine/Source/Frame/CLAUDE.md (IslandTerrain bullet)
- Add one line documenting the two intentional shader/CPU divergences: `TerrainElevation.frag:38-47` applies an undersea depth-compression `pow` curve the CPU `FrameElevation`/`GlobalElevation` deliberately do not mirror (visual-only; sim never consumes curved values), and the GPU samples through `kSamplerElevation` (LINEAR where the device can filter R32_SFLOAT, else NEAREST — `PipelineManager.cpp:250` comment) vs the CPU's nearest-texel lookup — neither affects CRC [~5m]

### Engine/Source/Frame/CLAUDE.md or Engine/Source/Frame/Collections/CLAUDE.md
- Document the `FrameInterpolate::Update` dual-dt contract where collection authors look: the same Update serves sim phase 1 at fixed dt inside `RunFrameTick` (`FrameTick.cpp:52`, `kfDeltaTime`) and client render interpolation at variable dt (`GameBase.cpp:377`, `fCoordDeltaTime`), distinguished only by `frameFlags` bits — every new interpolate-phase collection must be safe under both regimes. Today the stale-ring-memory pitfall is documented only at the flag-handling site (`FrameBase.cpp:177-186`) [~10m]

### Engine/Source/Frame/FrameBase.cpp
- Add a comment at `FramePostRenderBase::Write` (lines 105–120) / `Read` (lines 122–137): the client-only UUID counters serialize under `BT_CLIENT` (`:109-112`, `:126-129`), so a client-written stream is structurally unreadable by a server `Read` — safe today only because save/load is server-only; the guard is convention, not structure [~5m]

## Critical files
- `Engine/Source/Frame/CLAUDE.md`
- `Engine/Source/Frame/Collections/CLAUDE.md` (alternative home for the dual-dt note)
- `Engine/Source/Frame/FrameBase.cpp` (comment only)

## Out of scope
- Any code/behavior change (including making `Write`/`Read` layouts symmetric — YAGNI while save/load stays server-only)
- The `FrameUpdatePipeline.md` 64→32 Hz doc fix (owned by `Common/StaleDocClaimsSweep.md` item 11)

## Notes
- Doc/comment-only; no invariant exposure. Route the CLAUDE.md edits through the normal doc process at execution.

## Verification Notes (2026-06-10)
- `TerrainElevation.frag:38-47` verified: `if (fRaw < 0.0)` gates a `pow(fT, 1.0 / fWaterUnderseaCompression) * fSeaFloorElevation` curve; land passes through. CPU `GlobalElevation` (`IslandTerrain.cpp:207-268`) max-blends raw heightmap texels with truncation-based nearest-texel sampling (`:256-259`) and no curve — divergence claims hold.
- **Correction**: "GPU samples bilinear" was overstated — the elevation sampler chooses LINEAR or NEAREST per device capability (`PipelineManager.cpp:250`, `kSamplerElevation`). Reworded; the divergence (and its visual-only nature) stands either way.
- Dual-dt contract verified: fixed-dt call at `FrameTick.cpp:52` (`kfDeltaTime`), variable-dt render-interpolation call at `GameBase.cpp:377` (`fCoordDeltaTime`); the stale-ring-memory comment is `FrameBase.cpp:177-186`. Cites refreshed.
- Write/Read asymmetry verified: `Write` emits `uiNextSoundUuid`/`uiNextVisualUuid` under `BT_CLIENT` (`FrameBase.cpp:109-112`), server-build `Read` skips them (`:126-129` compiled out), `ServerRead` (`:139-151`) documents the skip. Save/load is server-only per game `Source/CLAUDE.md` — convention-only guard confirmed. Cites split into the correct per-function ranges.
- Out-of-scope dedupe confirmed: the `FrameUpdatePipeline.md` 64→32 Hz fix is `Common/StaleDocClaimsSweep.md` item 11.
