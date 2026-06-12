# Zero-Fleet Sync Indistinguishable From No Sync

## Context

The client applies fleet-sync packets in `ClientSession::PollNetwork` (`Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp`, the fleet-sync block ~`:97-108`):

```cpp
std::vector<Fleet> receivedFleets;
ParseFleetSync(mpClientNetwork->DrainReceivedGamePackets(), receivedFleets);
if (!receivedFleets.empty())
{
    ...
    gpGame->SyncFleets(std::move(receivedFleets));
    ...
}
```

`ParseFleetSync` (`Network/PlayerEvents.cpp`) signals "a sync was parsed" only through the content of its out-vector `rOutFleets`. It assigns `rOutFleets = std::move(parsedFleets)` on a successful parse (via `ParseFleetSyncPayload`), but never otherwise touches it. The caller therefore cannot distinguish three cases:

1. **No sync packet arrived this drain** — `receivedFleets` left empty.
2. **A valid zero-fleet sync arrived** — `ParseFleetSyncPayload` returns `true` with `iFleetCount == 0`, leaving `parsedFleets` empty; the move stores an empty vector. Out-vector ends empty.
3. **A malformed sync arrived** — rejected whole, out-vector untouched (empty if nothing valid preceded it).

The `!receivedFleets.empty()` gate collapses case 2 into case 1: a **valid zero-fleet sync is dropped**, so the client keeps its stale fleet list indefinitely.

### Verified premise (server legitimately sends zero-fleet syncs)

- `ServerFleetManager::SendFleetSyncToClient` (`Network/Server/ServerFleetManager.cpp` ~`:175-187`) sends `SendFleetSync(iClientId, {})` — an explicit **empty** fleet list — when the client's GUID has no entry in `mFleets`.
- `ProcessDeleteFleetRequests` (~`:66-96`) erases the fleet then calls `SendFleetSyncToClient`; deleting the last fleet leaves the map entry holding an empty vector, so the sync payload carries `fleetCount == 0`. Fleet deletion is client-initiated (`SendDeleteFleetRequest`), so fleets reaching zero during play is a normal path, not a corner only reachable at shutdown.
- The empty payload is well-formed on the wire: `SendFleetSync` always writes the type byte + `int64` count (`0`), and `ParseFleetSyncPayload` returns `true` for `iFleetCount == 0`.

### Cost of stale fleets

`FleetSelection::SyncFleets` (`FleetSelection.cpp`) already handles an empty input correctly — it clamps `miFocusedFleetIndex`/`miFocusedPlayerInFleetIndex` down (to `-1` once `mClientFleets` is empty). The fleet list and focus state drive camera-follow target, HUD fleet/ship display, and focus-cycle input. With the gate, after the client deletes its only fleet (or after any transition to zero fleets the client did not itself drive locally), the HUD/camera keep referencing a fleet the server no longer has until the next *non-empty* sync arrives. Selection/visual staleness only — no simulation, CRC, or wire-format involvement.

## Design

Make `ParseFleetSync` report whether a valid sync was applied, and have the caller key application on that signal instead of `!receivedFleets.empty()`.

Simplest correct option: change `ParseFleetSync`'s return type from `void` to `bool` — `true` when at least one valid sync (including a valid zero-fleet sync) was committed to `rOutFleets` during the drain, `false` otherwise. Internally set a `bApplied = true` flag at the existing `rOutFleets = std::move(parsedFleets)` commit point; return it. The malformed branch leaves the flag unset, preserving the existing "last valid sync wins, malformed never clobbers" contract.

Caller becomes:

```cpp
std::vector<Fleet> receivedFleets;
if (ParseFleetSync(mpClientNetwork->DrainReceivedGamePackets(), receivedFleets))
{
    engine::GridCoord preFleetCoord = gpGame->mClientGridCoord;
    gpGame->SyncFleets(std::move(receivedFleets));
    if (gpGame->mClientGridCoord != preFleetCoord)
    {
        UpdateDesiredCoords(SubscriptionChangeReason::kFleetSync);
    }
}
```

Update the declaration in `Network/PlayerEvents.h` to match.

Note: with multiple syncs in one drain, the existing loop already commits the *last* valid sync into `rOutFleets`; the bool just reflects "≥1 valid commit happened", which is the correct apply condition.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/PlayerEvents.cpp` — `ParseFleetSync`: change return to `bool`, set/return an applied flag at the `rOutFleets = std::move(parsedFleets)` commit. `ParseFleetSyncPayload` is unchanged (already returns `true` for the zero-fleet case).
- `Projects/BrokenEngineSandbox/Source/Network/PlayerEvents.h` — `ParseFleetSync` declaration return type.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` — `ClientSession::PollNetwork` fleet-sync block: gate `SyncFleets` on the return value instead of `!receivedFleets.empty()`.

## Out of scope

- `ParseFleetSyncPayload` bounds/count validation — already correct (the recently-landed `BoundedCursor` validation), not touched.
- The `ParsePlayerEvents` arena/no-heap path and the timespeed-packet block in `PollNetwork` — separate packet flows, unchanged.
- Server send-side gating in `ServerFleetManager` / `ServerFleetManagerUtils` — the server intentionally sends zero-fleet syncs; do not add a non-empty gate there (that would be the wrong fix, hiding legitimate "now zero" transitions).
- `FleetSelection::SyncFleets` empty-input handling — already correct; no change.
- The `kServerFleetSync` wire format and `GamePacketType` enum order — untouched; this is a caller-side signal change only.

## Notes

- **Invariant exposure**: client selection/UI state only. No determinism/CRC sim path, no `kiVersion`/`.pack` layout, no replay, no wire-format/`GamePacketType` change. The `PollNetwork` block already runs under `ScopedSuppressAllocationTracking`; the change adds no allocation. Client-only (`BT_CLIENT`) — the touched code is already inside the file's `#if defined(BT_CLIENT)` span.
- No open architectural decision: the bool-return shape is the minimal correct change and matches the "valid sync applied" semantics the caller needs.
