# Network/Server/ - Server Session and Managers

## Overview

Server-side game networking (`BT_SERVER`). `ServerSession` (via `gpServerSession`) is a thin orchestrator over four domain managers — fleet, transfer, broadcaster, client — each owning a `Process*`/`Queue*`/`ResetState` request-queue lifecycle. The fleet manager further delegates flagship-direction logic and the pending-flagship-update queue to a `FleetNavigationController`. `GameBase::ServerUpdate` drives the per-tick sequence (`PreTickNetwork` parse+request-drain, `PrepareTick` active-set recompute, frame tick, transfer harvest, then `BroadcastTick`); the manager methods are the steps, not a single ServerSession flow.

## Invariants

- **Parallel-vector invariant**: `ServerSession::mClientOwnedPlayerIds[iClientId]` and `ClientConnection::authorizedCoords` are index-aligned; every spawn / transfer / death / relink must mutate both in lockstep.
- **Active set**: rebuilt each tick from client subscriptions plus any coord with players plus pending destroy coords; `kOriginCoord` always included.
- **Client-dead gating**: a client is only marked dead when all owned players are gone; clients mid-transfer are skipped to avoid false positives while a player crosses a coord boundary.
- **Fleet ownership by `ClientGuid`**: fleets are keyed by persistent GUID so they survive disconnect/reconnect; a separate reap pass handles AI-managed disconnected fleets.
- **Fleet RNG lifecycle**: the fleet manager's `RandomEngine` is `TimeSeed()`ed once at construction and persists across all `ResetState()` calls; save/load round-trips its state at the tail of the fleet block. Never re-seed on session reset, quickload, or replay-load — replay determinism depends on it. Fleet creation draws two 64-bit values from this engine to mint the persistent fleet identifier; any change to that draw count is a save/replay break.
- **Flagship timer (post-arrival countdown)**: `Fleet::fFrameChangeTimer` drains only while `flagship.coord == fleet.wantedCoord` — i.e., the timer is paused during transit and only counts down once the flagship has actually arrived at the previously-picked destination. Cycle time is `transit + fNavigationDelay`, matching the intuitive meaning of "delay = N seconds" (idle N seconds at each destination, not "N seconds total including travel"). Cardinal mode is intentionally NOT gated on the drain: a flagship that arrives mid-cardinal (heading toward an edge) still drains, so the (likely already-negative) timer fires immediately when nav mode flips out of cardinal — that's the un-freeze property covering the H1 cardinal-stuck case. Firing requires `timer <= 0` AND `flagship.coord == wantedCoord` AND not in cardinal mode AND frame loaded; the coord-match check is repeated at fire time because a cardinal-eject between ticks can move the flagship away. Direction changes emit as `kUpdateFleet` status changes via the countdown-activation pattern (see [Frame/CLAUDE.md](../../Frame/CLAUDE.md)).
- **Tick-rate state in `BuildFrameInputs`**: `BuildFrameInputs` runs every `ServerUpdate` (inside `PrepareActiveSet`), even when paused — but the per-tick loop that consumes `mFrameInputs[*].statusChanges` does not, and the next `BuildFrameInputs` wipes the map via `mFrameInputs.clear()`. Any sim-time progression here (timers, accumulators) MUST advance by `gpGame->mfLastDeltaTime`, **not** `kfDeltaTime`. `mfLastDeltaTime = iFullTicks * kfDeltaTime`, so it's zero during pause (no queued status change to lose) and correctly scaled under `mTimeStep` fast-forward / slow-mo (so a fleet ticked once per `BuildFrameInputs` advances at the same rate as players ticked `iFullTicks` times in the per-tick loop). `TickFleetTimers` is the canonical example.
- **Reconnect / load relink**: matching `pClientGuids[i]` across frames are sorted by global ID to preserve creation order before rebuilding parallel vectors. Load additionally resets all managers and ring buffers but preserves pending flagship updates since fleet restoration enqueues into it.
- **Allocation suppression**: public manager entry points that grow SOA buffers or member containers wrap their body in `ScopedSuppressAllocationTracking`. Entry points whose per-tick scratch was migrated to the workbuffer (`gpThreadLocal->mWorkbuffer`) are allocation-free with self-guarding callees and intentionally OMIT the guard so the tracker stays armed for them (`BroadcastStatusChanges` is the canonical example).

## Notes

- Out-of-band assign/state/fleet-sync/timespeed packets use the reliable channel; payload size checks are post-strip (see hub for type-byte stripping).
- The debug-control packets the parent hub lists are decoded in `ParseReceivedGamePackets`. Timespeed broadcast is edge-triggered from `BroadcastTimespeedIfChanged` (called from engine `GameBase::ServerUpdate`); newly-handshaken clients are caught up via `SendTimespeedToNewClient` (called from engine `Server::ClientHello`).
- Disconnect clears `kPaused` and restores `mTimeStep` to 1/1 once no clients remain.
- Pending-request queues are cleared at the start of each tick and drained in fixed order; requests for vanished clients are silently dropped.

## See Also

- [../CLAUDE.md](../CLAUDE.md)
- [Engine/Source/Network/CLAUDE.md](../../../../../Engine/Source/Network/CLAUDE.md)
- [Network Architecture](../../../../../Documents/Architecture/Network.md)
