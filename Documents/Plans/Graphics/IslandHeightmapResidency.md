# Island Heightmap Residency (Both-Builds, Sim-Coordinated)

**Architectural.** Follow-up split out of the island resident-memory strategy pick (`IslandResidentMemoryScalingStrategy.md`) when the heightmap was found to be the dominant resident bucket **and** a deterministic sim dependency on both builds — i.e. not the client-only FileManager-lifecycle tweak the original (b) option imagined.

## Context

The `[DEBUG-resmem]` boot capture (70 templates) measured the per-template **heightmap at 181.5 MiB** — the single largest resident bucket (41% of ~437 MiB), scaling with total template count (`kiMaxIslands=128` → ~800 MiB headroom). It is sliced from the monolithic lazy chunk pool (`mpfHeightmapData`, assigned in `WaitForElevationMaps`, `IslandTerrain.cpp:163`) and held resident for the process lifetime.

Unlike the mesh CPU slice (reclaimed by `IslandResidentMemoryScalingStrategy.md`), the heightmap is **live-read at runtime on both client and server**:
- `BuildElevationGrid` (`IslandTerrain.cpp:355`) — per-cell, at the top of `RunFrameTick` (deterministic sim hot path, both builds).
- `GlobalElevation` (`IslandTerrain.cpp:267`) — render/client.
- Client elevation-texture re-mint `CreateElevationTextureFromHeightmap` (`IslandTerrainResidency.cpp:98`).
- Server NavContour build (`IslandTerrain.cpp:199`, boot).

So bounding it by concurrent residency requires a **reload-on-access residency model coordinated with the deterministic per-tick sim, on both executables** — the server has no texture LRU and keys residency on the simulation ring, not on-screen visibility.

## Design (options to resolve at grill)

The core requirement: heightmap bytes for a template must be resident before any sim/render/nav access, reloadable byte-identically (CRC-safe — same chunk bytes), and evictable when the template leaves the active set.

- **A — Per-template heightmap residency keyed on the simulation ring.** Track which templates are referenced by active sim cells; load the heightmap slice on ring-entry, free on ring-exit. Requires a per-chunk release path in `FileManager` (the monolithic committed pool has none today — see `IslandResidentMemoryScalingStrategy.md` Context) or a heightmap-out-of-pool relocation. Sim access (`BuildElevationGrid`) must assert/await residency.
- **B — Out-of-pool compact heightmap arena with LRU.** Move heightmaps out of the lazy pool into a managed arena that supports eviction/reload independent of the chunk system.
- **C — Compress resident heightmaps** (e.g. quantize / share across routes cropping the same source) — incremental, doesn't bound by concurrent residency.

## Critical files

- `Engine/Source/Frame/IslandTerrain.cpp` — `WaitForElevationMaps` slice (`:163`), `BuildElevationGrid` (`:355`), `GlobalElevation` (`:267`), server NavContour (`:199`).
- `Engine/Source/Frame/IslandTerrainResidency.cpp` — elevation re-mint (`:98`), eviction/restoration sweeps (`:278`/`:378`).
- `Engine/Source/File/FileManager.{h,cpp}` — lazy pool ownership (`:367`); a per-chunk/per-slice release path must be designed.
- `Engine/Source/Frame/CLAUDE.md` — residency-model + determinism docs.

## Out of scope

- Mesh CPU / mesh GPU / SSBO buckets (their own plans).
- Any change to the heightmap *bytes* or `kiNavDataVersion` (reload must be byte-identical — CRC-safe).

## Acceptance criteria

- Resident heightmap bytes bounded by concurrent (sim-ring) residency, not total template count, on both builds.
- Determinism preserved: reloaded heightmap is bit-identical; `BuildElevationGrid` output unchanged (dual-CRC clean).
- Sim never reads a non-resident heightmap (assert/await residency before access).

## Notes

- **Determinism-sensitive, both-builds, sim hot path** — this is the high-risk piece; design carefully. Reload is CRC-safe only if byte-identical.
- Depends on a `FileManager` per-chunk/per-slice release capability that does not exist today (monolithic committed pool) — coordinate with the `File/Architecture_FileManagerSplitDecision.md` chunk-seam work.
- `IslandNavContourResidency.md` (server NavContour) can attach to whatever residency model this plan establishes.
- **Decision plan**: option A vs B vs C for `/external-grill-plan`; gate on whether the mesh-CPU reclaim alone is enough for the working budget before committing to this.
