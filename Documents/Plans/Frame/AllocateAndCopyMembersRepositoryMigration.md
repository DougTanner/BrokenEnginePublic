<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-28T16:19:42.576Z","dependsOn":[]} -->
# Migrate Eligible Collection Copy Paths to AllocateAndCopyMembers

## Context

`engine::AllocateAndCopyMembers()` already allocates to the previous frame's exact
capacity and copies `PersistentMembers()` when present, otherwise the full
`Members()` tuple. Its tuple-subset assertion protects the relationship between
those tuples. Nevertheless, 19 of the repository's 32 collection
`AllocateAndCopy` definitions still reproduce that behavior manually or through
the narrower ID-only/allocation helpers. The manual forms make a newly added
carry-forward member easy to omit.

The completed Targets tuple-copy migration established the intended use, but
this is pre-existing repository-wide debt outside that Plan's accepted scope.
The false required condition is that every collection copy path which has an
equivalent full or persistent member tuple uses the shared helper, while a
deliberate phase-owned custom case remains explicit.

## Design

First inventory every `void *::AllocateAndCopy(...)` definition below
`Engine/Source/Frame/Collections` and
`Projects/BrokenEngineSandbox/Source/Frame/Collections`. Classify every one as
helper-backed, exact migration, or documented custom. Stop rather than force a
migration if current source exposes a non-equivalent row, tuple ordering/width,
conditional-build membership, allocation, ID-map, or capacity contract. No
definition may remain unclassified.

For exact cases, replace only the allocation/copy body with
`engine::AllocateAndCopyMembers(rCurrent, rPrevious)`. Preserve declaration
signatures, any surrounding profiling scope (notably
`SpaceshipsInterpolate`), and all `BT_CLIENT`/`BT_SERVER` membership. Add a
`PersistentMembers()` tuple only where its ordered contents exactly reproduce
the existing manual subset; server tuples must be empty where the current
server build copies no members.

| Classification | Definitions and required disposition |
| --- | --- |
| Direct full-member migration | `BlastersPostRender` (five existing copied members); `AreaLightsPostRender`, `WindTrailsPostRender`, `PointLightsPostRender`, `HexShieldsPostRender`, `SoundsPostRender`, `SmokeTrailsPostRender`, `BillboardsPostRender`, and `PushersPostRender` (their `Members()` tuple is exactly `puiIds`); `WindRadialsPostRender`, `ExplosionsPostRender`, and `PuffsPostRender` (empty `Members()` tuples). Replace their bodies only. |
| Persistent-tuple migration | `BlastersInterpolate` (`puiTypeIndices` plus its existing client-only child IDs/tuning); `SpaceshipsInterpolate` (`puiPushers`, `puiTargets`, plus client `puiWindTrails`); `SpaceshipsPostRender` (`pVecDamageDirections`, `pAlignments`); `PlayersInterpolate` (`puiPushers`, plus client `pWindTrails` and `pHexShields`); `PlayersPostRender` (`puiIds`, `pAlignments`, `pClientGuids`, `pGlobalPlayerIds`); `MissilesInterpolate` (client `puiAreaLights`, `puiSmokeTrails`, empty on server); and `MissilesPostRender` (`pFlags`, `pVecExplosionDirections`, `pfDeltaRotationMax`, `pfAccelerations`, `pfPitches`, client `puiSounds`, `pAlignments`). Declare each exact existing subset in tuple order, then replace only the manual sequence. |
| Already helper-backed audit | Leave unchanged unless current source contradicts this classification: both `Targets` halves; `AreaLightsInterpolate`, `WindTrailsInterpolate`, `PointLightsInterpolate`, `HexShieldsInterpolate`, `SoundsInterpolate`, `SmokeTrailsInterpolate`, and `BillboardsInterpolate`; and `WindRadialsInterpolate`, `ExplosionsInterpolate`, and `PuffsInterpolate` using existing persistent tuples. |
| Intentional custom exclusion | Leave `PushersInterpolate::AllocateAndCopy` allocation-only. Its scoped contract places member carry-forward in Interpolate `Update`, not allocation/copy. `PushersPostRender` is an exact ID-only migration under the direct group. |

Reuse the existing helper unchanged. Do not remove `AllocateAndCopyIds` or
other specialized helpers unless a separate approved minimum-scope change first
proves repository-wide zero use. Do not change SOA pointers, tuple layout or
order, shared/CRC tuples, IDs or ID maps, serialization, versions, wire/save or
replay formats, phase participation, target affinity, Add/Remove/transfer/
hydration behavior, or the custom Update ownership.

## Critical files

- `Engine/Source/Frame/Collections/CollectionMemory.h` — existing helper and
  full-versus-persistent tuple contract; do not change it.
- `Engine/Source/Frame/Collections/{AreaLights,WindTrails,PointLights,HexShields,Sounds,SmokeTrails,Billboards,WindRadials,Explosions,Puffs,Pushers}/*.cpp`
  and their headers — engine direct, helper-backed, persistent, and custom
  classifications.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/{Blasters,Spaceships,Players,Missiles,Targets}/*.{h,cpp}`
  — game tuple declarations and manual/helper copy paths, including conditional
  client members.
- `Engine/Source/Frame/Collections/AGENTS.md`,
  `Engine/Source/Frame/Collections/Pushers/AGENTS.md`, and
  `Projects/BrokenEngineSandbox/Source/Frame/Collections/AGENTS.md` — tuple,
  phase-carry-forward, identity, and client/server invariants to preserve.

## Out of scope

- Changing `AllocateAndCopyMembers`, `CollectionMemory`, or helper semantics.
- Any collection layout, member/tuple order, shared/CRC boundary, persistence,
  wire/save/replay compatibility, version, frame phase, project membership, or
  client/server affinity change.
- Deleting specialized helpers, adding tests, inventing harness capabilities,
  or migrating `PushersInterpolate`'s Update-owned copy.

## Risk triggers and invariants

This is Change Workflow Tier 3: it spans engine and game frame subsystems,
deterministic CRC/replay state, persistent carry-forward tuple semantics, and
conditional client/server variants. Preserve previous-frame capacity, zero-row
behavior, allocation and ID-map behavior, and one-to-one member row copy width
and order. `PersistentMembers()` must remain an ordered subset of `Members()`;
members omitted from it retain their existing phase owner. A discovered
non-equivalence is a stop condition, not authority to change a format or bump a
version.

## Acceptance criteria

- A static exhaustive inventory classifies all 32 current `AllocateAndCopy`
  definitions, with no unclassified site. Each migrated definition proves its
  full or persistent tuple is one-to-one equivalent to its prior copy list in
  both client and server variants, including row count, order, width, zero
  count, allocation, capacity, and ID-map behavior.
- The final diff proves no collection layout, `SharedMembers()`/
  `SharedCrcMembers()`, serialization, version, phase, or project membership
  change. Do not bump a version unless a proven non-equivalence stops this Plan
  for a separately authorized decision.
- Build Debug x64 client and server through WorktreeCli in the appropriate
  shared/no-DataPacker mode because no data paths change.
- Use the agent harness where achievable to connect representative non-empty
  Players, Spaceships, Missiles, Blasters, and Targets state; advance at least
  128 ticks; complete a replay loop; observe no new persistence/read/CRC/
  checksum/confirmed-desync diagnostics; and cancel cleanly. An existing debug
  collection fixture may be used only when it decisively exercises an affected
  contract. For engine client-visual-only collections with no current harness
  query, static tuple-equivalence evidence plus client compilation is the
  decisive signal; do not require every migrated type live simultaneously.
- Run affected-code propagation, fresh C++ correctness review, Tier-3
  adversarial review, C++ style review, AGENTS/CLAUDE documentation sync, and
  final verification/finalization under the Change Workflow.

## Notes

No live executable Plan owns this root cause or implementation boundary. The
deleted `Documents/Plans/Frame/TargetsTupleCopyMigration.md` is a completed
exemplar, not a directional prerequisite, so this Plan has no dependency or
reciprocal Coordination entry.
