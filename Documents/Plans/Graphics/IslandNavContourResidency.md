# Island NavContour Residency

## Context

Surfaced by the `IslandResidentMemoryScaling.md` sibling-sweep: every `engine::IslandTemplate`
carries an `mNavContour` (`NavContour { std::vector<XMFLOAT2> vertices; std::vector<int32_t>
polygonOffsets; std::vector<int32_t> visEdgeA; std::vector<int32_t> visEdgeB; }`) that scales with
per-template topology and stays permanently resident server-side (built in
`WaitForElevationMaps`, freed only at `~IslandTerrain`). At 65 templates the estimated cost is
~5-50 KB per template depending on island shape complexity.

The texture LRU does not apply (NavContour is server-only, not a GPU resource). The mesh /
heightmap residency-strategy work is owned by `IslandResidentMemoryScalingStrategy.md` — this
plan covers NavContour separately because it has its own lifecycle (server-only, no GPU coupling,
no record-once CB invariant to respect).

## Design

Sequence after `IslandResidentMemoryScalingStrategy.md` so the residency model for the rest of
the template can inform the NavContour strategy:

1. **Measure first.** Extend the `[DEBUG-resmem]` LOG block (if still present) — or add a parallel
   server-side instrumentation block — to report `mNavContour` bytes per template and in
   aggregate. Skip if the strategy plan's measurements show the aggregate non-NavContour footprint
   is already negligible.
2. **Pick a strategy** (decision deferred to that grill):
   - **Adopt whatever the strategy plan picks.** If `IslandResidentMemoryScalingStrategy.md`
     landed (b) (lazy-load the `kIsland` chunk), the NavContour can attach to the same
     subscription-driven lifecycle — built on chunk-load, freed on chunk-eviction. Cleanest path.
   - **Independent lazy build.** Build NavContour on first-subscription server-side and free on
     eviction, even if the mesh/heightmap stays eager. Higher complexity for marginal saving.
   - **Compress / share.** Move the four `std::vector`s to a single arena allocation per template,
     with `int32_t` indices into a shared vertex pool if cross-template sharing is viable.
   - **Close.** If measurements show NavContour bytes are negligible at 65 templates, document and
     stop.

## Critical files

- `Engine/Source/Frame/IslandTerrain.h` — `IslandTemplate::mNavContour` member
  (`NavContour` struct from `Frame/NavBuild.h`).
- `Engine/Source/Frame/IslandTerrain.cpp` — `WaitForElevationMaps` builds `mNavContour`
  server-side; revisit this site for whatever lifecycle adopts.
- `Engine/Source/Frame/NavBuild.h` / `.cpp` — `NavContour` struct definition + builder.

## Out of scope

- Client-side mesh / heightmap residency — owned by `IslandResidentMemoryScalingStrategy.md`.
- Any change to `NavData` (the per-cell merged nav structure) or `NavBuild` algorithm correctness.
- Any change to `kiNavDataVersion` or save-compatibility for NavData.

## Acceptance criteria

- NavContour bytes are bounded by *active server-side residency* (per the strategy plan's chosen
  model) rather than *total template count*, OR measurement shows the cost is small enough to
  close the plan.
- Server-side determinism is preserved: NavContour built on demand must produce
  bit-identical output to the eager build (CRC-safe) — there is no client/server split here since
  NavContour is server-only.

## Notes

- Server-only — no `BT_CLIENT` guards required for the NavContour storage itself.
- This plan depends on `IslandResidentMemoryScalingStrategy.md` for the lifecycle decision. Do
  not pick a strategy here before that one lands (or unless that one closes via (e)).
