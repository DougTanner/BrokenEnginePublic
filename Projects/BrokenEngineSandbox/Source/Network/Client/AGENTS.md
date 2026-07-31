# Network Client - Session and Reconciliation

Client-only game networking. `ClientSession` is the game-policy wrapper over `engine::ClientSessionRuntime`; it owns receive-side hydration (filling collection slots from received data — see `../../Frame/Collections/AGENTS.md`), reconciliation, and desync/gameplay policy.

## Ownership

- `engine::ClientSessionRuntime` owns connection/GUID/discovery, subscription state, clock state, ordered drains, and ACK/flush. `ClientSession` supplies desired coordinates and applies static data, full states, game packets, and tick updates to `CoordFrames`; static-data arrival also requests matching terrain textures.
- `ClientReconciler` dispatches each coordinate once per client update. Workers write only their assigned `CoordFrames`; result merging, first-wins desync selection, visual-error aggregation, and cross-coordinate player-transfer migration run after dispatch.
- `ClientDesyncManager` coordinates debug capture, resync, and repeated-desync disconnect policy.

## Session Policy

- Persist the client GUID in the versioned app-data `ClientGuid.bin`: load it before connecting, then let the transport's accept callback write it atomically so an interrupted write cannot orphan persistent server state.
- Disconnect clears transport and discovery objects, every coordinate's client state, subscription intent, the latest-server pacing baseline, active correction error/target, correction-log cadence, reconciliation, and desync recovery.
- Preserve subscription orchestration order: remove stale coordinates, recover timed-out transitional slots, rebuild the desired queue, then fill available slots. Apply each poll's static data before full states and full states before delta updates; stale deltas are skipped and duplicate ticks keep the first arrival.
- LAN discovery stops after recording a found address. A scan timeout records the timeout, replaces the scanner, and immediately starts a fresh scan.

## Reconciliation Invariants

- A server-validated tick is frozen and must not be simulated again. Matching speculative CRCs advance confirmation without replay; unresolved mismatches or due pending authoritative state enter rollback and replay. Future full states stay queued until due; a due state beyond an update gap becomes the authoritative ring base.
- Preserve render-behind history when advancing confirmed state. Replay and catch-up must remain within the coordinate ring budget.
- Apply transfer `StatusChange`s after each tick, matching server Destroy/Spawn order, and recompute the Frame CRC when transfers modify the result. The server recomputes the same way after its own transfers land, so this side's frame only matches the broadcast CRC if both sides recompute; dropping either recompute turns every cross-cell transfer into a reported desync.
- Server-load notification clears coord, clock, identity, fleet, subscription, and reconciler state before the client accepts post-load data.
- Player-event, timespeed, and fleet-sync handlers have independent log-and-continue exception boundaries. Static-data application, full-state hydration, and tick-update application remain outside those catches.
- Sticky desired subscriptions reduce visible churn; the engine slot queue owns transport throttling. During a real debug-frame wait or the synthetic full-state fixture stall, transport polling and receive-buffer drains continue while subscription updates, simulation, and reconciliation remain stalled.

Speculative and provisional CRC mismatches log at `kDebug`. Only a mismatch that survives full rollback/replay is a confirmed desync, logs at `kError`, and triggers recovery policy. A confirmed mismatch always reports the differing CRCs. With `kbDesyncDebugFrames` enabled, the client requests the server snapshot, stalls until the response or `kDesyncDebugTimeout`, compares any response, then recovers or disconnects. With it disabled, the client never requests or stalls for a real desync and immediately follows `kbDesyncRecovery`; repeated recoveries within the configured window escalate to disconnect.

Detailed fast-path, rollback-base, clock, and full-state behavior belongs in the architecture documents rather than this leaf.

## See Also

- `../../../../../Documents/Architecture/GameReconciliation.md`
- `../../../../../Documents/Architecture/Network.md`
