<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:59:56.451Z","dependsOn":[]} -->
# WindTrails over-long first deposit quad after wind re-enable

## Context

`WindTrailsInterpolate::Render` (`Engine/Source/Frame/Collections/WindTrails/WindTrailsRender.cpp:57`) returns early while wind is disabled, before the per-frame `sPreviousPositions.insert_or_assign` snapshot at lines 142-146. Trail owners keep publishing current positions during the disabled interval (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp:339` calls `WindTrailsInterpolate::Sync`), so the cached previous positions freeze. On re-enable, lines 84-95 subtract the frozen cached position from the current position and build one deposit quad spanning the entire disabled interval's displacement, contradicting `WindTrails/AGENTS.md` line 8: each quad spans exactly one render frame of motion. Severity scales with owner displacement; the quad injects wind across the whole path for one frame.

The toggle is runtime-reachable (`GraphicsMenuScreen.cpp:199`). The cache and render path are client-only and outside CRC state.

Found and confirmed by the 2026-08-06 `/external-deep-analysis` run over `Engine/Source/Frame/Collections` (Recursive); outside that session's active boundary, so recorded here.

## Design

In `WindTrailsInterpolate::Render`, snapshot every live trail's current position each rendered frame even while wind is disabled, before the early return; draw generation stays skipped. The `insert_or_assign` can allocate, so the snapshot stays inside `ScopedSuppressAllocationTracking` exactly as the existing enabled path does at lines 65-67. No collection or layout change.

## Critical files

- `Engine/Source/Frame/Collections/WindTrails/WindTrailsRender.cpp` — the fix site.
- `Engine/Source/Frame/Collections/WindTrails/AGENTS.md` — read-only one-frame-deposit authority.

## In scope

- `WindTrailsInterpolate::Render` in `WindTrailsRender.cpp`: position snapshot ahead of the wind-disabled early return, under the existing allocation suppression.

## Out of scope

- WindRadials (sibling issue, own Plan), collection layout, shaders, and the wind-deposit ping-pong pipeline.

## Risk tier and invariants

Expected Change Workflow Tier 2: scoped client-only render behavior in one subsystem; no CRC, wire, serialization, or threading exposure. Invariants: no untracked main-loop heap allocation (snapshot stays suppressed with its existing `// Heap:` rationale); the mirrored per-effect file set stays parallel.

## Acceptance criteria

- Move an active wind-trail owner, disable wind for several frames, re-enable: the first deposit spans at most one render frame of displacement, never the disabled interval.
- Client builds clean through `/compile`.
