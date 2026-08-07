<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:59:13.755Z","dependsOn":[]} -->
# Carry blaster wind-trail tuning and player shield scalars across authoritative transfer

## Context

The destination client applies the server-authored cross-cell transfer payload: client harvesting is disabled, the server harvests `TransferRequest`s and publishes them per destination coordinate, and the client applies the received `StatusChange` (`Projects/BrokenEngineSandbox/Source/Game.cpp:385`, `Network/Server/ServerBroadcaster.cpp:136`, `Network/Client/ReconcileReplayTick.cpp:128`). Two collections populate client-visual scalars into that payload only under `BT_CLIENT`, so the authoritative payload carries defaults:

- `BlastersPostRender::Transfer` (`Frame/Collections/Blasters/Blasters.cpp:216`) omits wind-trail intensity, width, and length multiplier on the server; serialization (`Network/NetworkSerialization.cpp:13`) emits 0/0/1 and destination spawn (`Blasters.cpp:190`) then creates no trail. This violates `Blasters/AGENTS.md` ("Cross-cell transfer carries wind-trail tuning").
- `PlayersPostRender::Transfer` (`Frame/Collections/Players/PlayersNavigation.cpp:99`) copies shield rotation and shrink only on the client; the server payload resets them to 0/1 and the destination installs those (`Players.cpp:423-436`, serialization at `NetworkSerialization.cpp:56`), producing a visible shield phase/size discontinuity. This violates the verbatim-restoration rule at `Frame/Collections/AGENTS.md` (transfer arrivals restore carried state verbatim).

Verified by /external-deep-analysis Phase-3 review (2026-08-06); pre-existing debt outside any active change.

## Design

Make the five scalars cross-build shared transfer state so both builds populate the payload identically, following the existing non-CRC `fAnimationTime` pattern: the scalar state is advanced/carried on both builds, declared in `SharedMembers()`, and explicitly excluded from `SharedCrcMembers()`. Wind-trail, shield, and GPU objects stay client-only; only the plain scalars cross builds. Every layout change follows the full `/add-collection-member` checklist. The `TransferData` payload already carries all five fields and `NetworkSerialization.cpp` already serializes and deserializes them symmetrically (`NetworkSerialization.cpp:19-21,69-70,86-88,134-135`); the defect is only that the source-side population sits under `BT_CLIENT`, so the serialization code is not changed — the fix moves population out of the client guard and backs it with the cross-build members above.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.cpp`, `Blasters.h`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp`, `Players.h`, `PlayersNavigation.cpp`
- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` (read-only — existing `TransferData` field serialization is reused unchanged)

## In scope

- Blaster wind-trail intensity/width/length-multiplier: cross-build storage, `BlastersPostRender::Transfer` population on both builds, destination spawn consumption.
- Player shield rotation and shrink scalars: cross-build advance/carry, `PlayersPostRender::Transfer` population on both builds, destination install at the `Players.cpp` spawn/transfer site.
- `SharedMembers()`/`SharedCrcMembers()` declarations for exactly these members.

## Out of scope

- Missile smoke-trail identity (client-local; owned by `Documents/Plans/Frame/MissilesTransferSmokeTrailIdentity.md`).
- Any edit to `NetworkSerialization.cpp` — the existing `TransferData` fields and their wire encoding stay byte-identical.
- Any addition to the CRC member set; any gameplay/steering/damage behavior change.
- Wind-trail/shield rendering, hydration of other collections, engine transfer machinery.

## Risk tier and invariants

Change Workflow Tier 3. Trigger: collection data layout — new cross-build `SharedMembers()` entries change the shared cross-build serialization identity, without touching the wire encoding in `NetworkSerialization.cpp`. Invariants: per-tick shared CRC unchanged (new members excluded from `SharedCrcMembers()`); replay determinism holds; `Frame::kiVersion`/serialization identity updated per `/add-collection-member`; both builds execute identical shared-state writes.

## Acceptance criteria

- /agent-harness scenario: a blaster and a player visible on the client crossing a cell boundary retain wind-trail tuning and shield phase/shrink at the destination.
- Client and server build clean through `/compile`; replay determinism check passes with matching per-tick CRC.
