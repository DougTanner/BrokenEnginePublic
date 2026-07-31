<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-25T16:27:43.844Z","dependsOn":[]} -->
# Re-anchor Client Fleet Focus by FleetGuid

## Context

`FleetSelection::miFocusedFleetIndex` (`FleetSelection.h:33`) is a client-session-lifetime index into `mClientFleets`. It is an index into a vector whose element *order* is chosen by the server and can change under the client at any time.

`FleetSelection::SyncFleets` (`FleetSelection.cpp:110-180`) replaces the whole vector on every fleet resync (`mClientFleets = std::move(fleets);`, `:123`). It re-anchors the focus by stable identity in exactly one case — the reconnect-style first sync, guarded by `iPrevFleetCount == 0` at `:128`, which matches `gpGame->mRememberedFleetGuid` against `Fleet::guid`. Every other resync falls into the `!bRestoredRemembered` branch at `:165-180`. That branch bounds-clamps the index (`:168-171`) and, when the fleet count *grew*, auto-focuses the newest fleet (`:174-179`). Neither is an identity re-resolution: on a resync that does not grow the vector, an index still in range is reused verbatim against contents that may have shifted.

```cpp
if (miFocusedFleetIndex >= std::ssize(mClientFleets))
{
    miFocusedFleetIndex = std::ssize(mClientFleets) - 1;
}
```

The shift originates server-side. `ServerFleetManager::ProcessDeleteFleetRequests` erases from the middle of the client's `std::vector<Fleet>`; `SendFleetSync` (`ServerFleetSerialization.cpp:12-43`) serializes that vector **in order**; `ClientSession.cpp:122` feeds the decoded vector straight into `SyncFleets`.

Reaching it requires a specific temporal sequence, because the HUD only ever deletes the *currently focused* fleet, and only when it is empty (`HudScreen.cpp:284-296`, where `bCanDelete` requires `pFleet != nullptr && pFleet->members.empty()`). The reproduction is therefore:

1. With three or more fleets, focus an empty fleet that is **not** last and delete it. The request is now in flight.
2. Before the resync arrives, focus a *later* fleet.
3. The resync lands. The deleted fleet is gone, every later fleet has shifted down one, `miFocusedFleetIndex` is still in range and unchanged, and `FocusedFleet()` (`FleetSelection.cpp:68-75`) returns a **different fleet** than the one the player just focused.

The window is one network round trip, which is ample for a fleet-cycle keypress.

Observable consequences: the camera follows the wrong fleet, and fleet-directed UI actions issued through `FocusedFleet()` target the wrong fleet.

The completed FleetGuid request re-key removed the *server-side* half of this bug class by keying fleet requests and server queues on `FleetGuid`; it explicitly left `FleetSelection`/`Game` focus state out of scope. This plan closes the remaining client-side half. The two are independent: after the wire re-key, requests always target whichever fleet `FocusedFleet()` returns, so a wrong focus produces a wrong-but-consistent request rather than a second, separate misresolution.

## Design

Extend the existing guid re-anchor so it runs on **every** `SyncFleets` call, not only the reconnect path.

- Capture the focused fleet's `FleetGuid` from `mClientFleets` **before** `:123` replaces the vector (alongside the existing `iPrevFocusedFleetMemberCount` capture at `:117-122`).
- After the swap, resolve that captured guid against the new `mClientFleets` by matching `Fleet::guid`, and set `miFocusedFleetIndex` to the found position. This is the same match the reconnect path already performs at `:130-137`; reuse rather than duplicate it.
- Ordering with the existing branches must be preserved deliberately:
  - The reconnect restore (`iPrevFleetCount == 0`) keeps priority — it restores from disk-persisted state that the in-memory capture cannot supply.
  - The "auto-activate newly created fleet" branch (`:173-179`) must still win when the fleet count grew, so creating a fleet still focuses it.
  - The clamp remains the fallback for the genuine case where the previously focused fleet is **gone** from the new vector (the player deleted the focused fleet itself). Resolve-then-clamp, not clamp-then-resolve.

The focused *member* needs no equivalent treatment. Members are appended, replaced in place, or marked dead — never reordered — so once the fleet is re-anchored correctly, `miFocusedPlayerInFleetIndex` still denotes the same member, and the existing clamp / newest-member / first-alive handling at `:186-213` remains correct as written. Do not add member re-resolution.

The implementer chooses whether to factor the guid-match loop into a small private helper shared with the reconnect path or to resolve inline; there is no third caller.

## Critical files

- `Projects/BrokenEngineSandbox/Source/FleetSelection.cpp` — `SyncFleets` (`:110-180+`) is the whole change; `FocusedFleet()` (`:68-75`) is the consumer that exposes the fault.
- `Projects/BrokenEngineSandbox/Source/FleetSelection.h` — `miFocusedFleetIndex` (`:33`), `miFocusedPlayerInFleetIndex`, and any helper declaration the change adds.
- `Projects/BrokenEngineSandbox/Source/Fleet.h` — read-only: `FleetGuid` (`:8-15`), `Fleet::guid` (`:34`), `FleetMember::globalPlayerId`.
- `Projects/BrokenEngineSandbox/Source/Game.h` — read-only: `mRememberedFleetGuid` (`:143`) and the focus accessors that forward to `FleetSelection`.

## In scope

- `Projects/BrokenEngineSandbox/Source/FleetSelection.cpp` — `SyncFleets` (`:110-180+`) only: capture the focused fleet's `FleetGuid` before the `mClientFleets` replacement at `:123` (alongside the existing `iPrevFocusedFleetMemberCount` capture at `:117-122`), then after the swap resolve that guid against the new vector by `Fleet::guid` and set `miFocusedFleetIndex`, reusing the reconnect path's match at `:130-137`. The reconnect restore (`iPrevFleetCount == 0`, `:128`) keeps priority, the newly-created-fleet auto-focus (`:173-179`) still wins when the count grew, and the clamp (`:168-171`) remains the resolve-then-clamp fallback.
- `Projects/BrokenEngineSandbox/Source/FleetSelection.h` — `miFocusedFleetIndex` (`:33`) and, if the implementer factors the guid-match loop out, the one private helper declaration that change adds.

## Out of scope

- The server-side fleet vector, its mid-vector `erase`, and the in-order fleet-sync wire layout — the ordering is legitimate; the client must tolerate it, not constrain it.
- The completed request/queue re-key. That work is landed and independent.
- Persisted client settings layout and `mRememberedFleetGuid`/`mRememberedFocusedShipId` semantics — reused as-is; no versioned-file change.
- Replacing `miFocusedFleetIndex` with a stored `FleetGuid` member as the focus representation. Re-anchoring on sync is the smaller complete fix; changing the member's type would touch every accessor and `HudScreen` caller for no additional correctness.
- `Fleet::iFlagshipIndex` and `iMemberIndex` addressing generally — members are appended and marked dead, never reordered.

## Risk tier and invariants

**Tier 2** — client-only runtime behavior in one subsystem. No wire/protocol, serialization, save/replay, determinism/CRC, threading, or trust-boundary exposure: `FleetSelection` is client-only state and `SyncFleets` runs on received data that is already validated at the parse boundary.

- Focus must remain stable across a resync that does not remove the focused fleet.
- Creating a fleet must still auto-focus the new fleet.
- Deleting the focused fleet itself must still fall back gracefully (clamp), never index out of range.
- `SyncFleets` runs under `ScopedSuppressAllocationTracking` (`:112-113`) for the vector rebuild and LOG formatting; the added capture and lookup must not introduce heap allocation outside that existing suppression.

## Acceptance criteria

- Running the three-step sequence in Context — delete a non-last empty focused fleet, focus a later fleet before the resync lands, then let it land — leaves focus on the fleet the player selected, verified by `Fleet::guid` rather than by index.
- Deleting the focused fleet and letting the resync land without re-focusing clamps to a valid remaining fleet, with no out-of-range access.
- Creating a new fleet still focuses the newly created fleet.
- Reconnecting still restores the disk-remembered fleet and ship, unchanged from current behavior.
- Across a resync that shifts fleet positions, the focused member index still denotes the same member of the re-anchored fleet.

## Notes

- Root cause found during the `FleetRequestsByGuid` Tier-3 change by a repository-wide scan for stored container positions used as entity identity. That scan cleared every other candidate: id-based `Collection` access resolved and discarded within a tick, epoch-paired network slot indices, ring-buffer offset arithmetic, and lookups that rescan on every call.
- The fix pattern already exists in the target function; this plan generalizes it rather than introducing a new mechanism.
