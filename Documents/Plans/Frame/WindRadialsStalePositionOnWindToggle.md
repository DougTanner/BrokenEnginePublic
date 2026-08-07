<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:59:55.612Z","dependsOn":[]} -->
# WindRadials stale position across wind disable/enable toggle

## Context

`WindRadialsInterpolate::Update` (`Engine/Source/Frame/Collections/WindRadials/WindRadialsUpdate.cpp:13`) returns early while `gWindEnabled` is false, before carrying `pVecPositions` forward from the previous frame. `pVecPositions` is in `Members()` but not `PersistentMembers()` (`WindRadials.h:63`), so `CollectionMemory.h:225-249` leaves the column holding reused ring-slot data or fresh non-zero-initialized storage on every disabled frame. On re-enable, `WindRadialsUpdate.cpp:25` reads that poisoned previous-frame column and `WindRadialsRender.cpp:59` consumes it: a radial surviving the toggle resumes at an unrelated or indeterminate position and deposits wind there.

Rows survive the toggle because controller expiry keeps running (`CollectionController.h:169`; registered lifetime 0.3 s), and the wind toggle is runtime-reachable (`Projects/BrokenEngineSandbox/Source/Ui/Screens/GraphicsMenuScreen.cpp:199`). WindRadials is wholly `BT_CLIENT` and outside shared CRC state. `WindRadials/AGENTS.md` line 9 already documents that copy and early-out behavior must change together.

Found and confirmed by the 2026-08-06 `/external-deep-analysis` run over `Engine/Source/Frame/Collections` (Recursive); outside that session's active boundary, so recorded here.

## Design

Carry `pVecPositions` from the previous frame for every current row before the disabled early return in `WindRadialsInterpolate::Update`, leaving derived intensity/size animation and rendering disabled. This keeps the copy in the owning phase without adding a redundant per-frame persistent copy on enabled frames (the rejected alternative — adding `pVecPositions` to `PersistentMembers()` — is serialization-neutral but duplicates the carry the enabled path already performs).

Update the `WindRadials/AGENTS.md` invariant wording if the coupled stale-state caveat becomes obsolete.

## Critical files

- `Engine/Source/Frame/Collections/WindRadials/WindRadialsUpdate.cpp` — the fix site.
- `Engine/Source/Frame/Collections/WindRadials/WindRadials.h` — read-only member-tuple authority.
- `Engine/Source/Frame/Collections/WindRadials/AGENTS.md` — invariant wording.

## In scope

- `WindRadialsInterpolate::Update` in `WindRadialsUpdate.cpp`: position carry-forward ahead of the `gWindEnabled` early return.
- The stale-state sentence in `WindRadials/AGENTS.md` if the fix makes it obsolete.

## Out of scope

- Any `Members()`/`PersistentMembers()`/layout change (routes to `/add-collection-member`).
- WindTrails (sibling issue, own Plan), shaders, and the wind-deposit render path.

## Risk tier and invariants

Expected Change Workflow Tier 2: scoped client-only visual behavior in one subsystem; no CRC, wire, serialization, or threading exposure. Invariants: no main-loop heap allocation added; the mirrored per-effect file set stays parallel; `Update` stays correct at any delta time including zero.

## Acceptance criteria

- With live radials, disabling wind for several frames and re-enabling it never renders or deposits a radial at a position other than its own carried position.
- Client builds clean through `/compile`.
