# `/Engine/Source/Network/Client/` - Client Networking

## Overview

Client-side ENet peer (`Client`, `gpClient`) plus engine-generic `ClientSessionBase`. The peer owns the wire: receive-handler dispatch, per-slot subscription state, ACK/RTT/jitter/bandwidth tracking. The session base owns the per-frame policy: subscription queue, stale-coord unsubscribe, draining received updates into `CoordFrames`, clock-correction, and LAN-discovery polling. Game sessions inherit the base. Client-only (`BT_CLIENT`).

## Receive Paths

Server-update kinds land in separate buffers: per-tick coord delta updates (one buffer per slot), full state (placeholder-adopt + slot activate), static data (sent once per subscription; carries NavData), debug frames (one-shot, captured for desync inspection), and a one-shot load-notification flag the game layer drains. Channel/ACK/drain-per-poll conventions are the hub's — see parent.

## Subscription Receive Invariants

Most receive handlers (full-state, coord-update, subscribe-accept) classify into a flag set (commit / clear-placeholder / heal-epoch / reject-as-ghost) before mutating slot state, the choke point for the epoch and ghost logic below; static data applies the same logic inline.

- **Epoch check** (drop rationale: hub's slot ACK model) applies only where the slot has a server-assigned epoch — a `kSubscribing` placeholder has none yet.
- **Out-of-order full state** (before subscribe-accept) adopts the coord, clearing the `kSubscribing` placeholder at whichever slot holds it.
- **Static data** applies the same coord-identity check as full state on `kWaitingFullState`/`kSubscribing` slots (silent drop on coord mismatch — the full-state path owns the ghost unsubscribe), with the epoch guard only on `kWaitingFullState`; `kUnsubscribed` accepts (out-of-order; buffers only, no slot mutation).
- **Pre-full-state buffering**: a `kWaitingFullState` slot accepts delta updates but does not advance its ACK tick floor (only `kActive` slots track received ticks).
- **Cancelled-subscription ghosts**: locally-dropped `kSubscribing` slots record the coord; a late accept/full-state triggers an unsubscribe. One epoch-heal case covers legitimate re-subscribe to an already-active slot.
- **Resync re-commit**: a full state on a still-`kActive` slot commits only when its coord and epoch both match the slot — the server resends full state on live slots to re-sync a desynced client, re-baselining the slot's ACK floor and adopting a fresh frame; any other `kActive` full state (coord/epoch mismatch) or an `kUnsubscribing` slot is dropped.
- **Out-of-range accept guard**: a subscribe-accept with a non-`kuiSubscribeRejectSlot` index beyond the client slot pool is a trust-boundary violation — the client immediately unsubscribes to prevent a server-side slot from leaking.
- **Gap beyond `kiNetworkBufferSize`** on a single slot forces disconnect.
- **Activation and adoption are same-frame**: a full state mutates slot ACK/epoch/state immediately at receive time (`ClientReceive.cpp` `ServerCoordFullState`), but its frame payload is adopted later by the game-layer drain. Because `Poll()` clears all receive buffers at entry (drain-per-poll), a game layer that skips a drain loses the frame yet keeps the activated slot — so activation and adoption must both happen in the same frame.

## Session Policy (`ClientSessionBase`)

- **Subscription queue** rebuilt each frame from the game's desired-coord set (active slots excluded); drained into free/freeing slots when any is available, else suppressed-logged.
- **Stale-coord unsubscribe**: active slots whose coord left the desired set are cancelled (if still `kSubscribing`) or sent an unsubscribe; the matching `CoordFrames` entry is erased.
- **Update apply** moves drained per-tick updates into each active slot's `CoordFrames::serverUpdates` via `try_emplace` (first arrival wins, stale ticks skipped); buffer overflow drops the update with a `kWarning` log.
- **Clock correction**: jitter-derived `targetBehind` (jitter + fixed safety margin, 2-tick hysteresis) sets how far behind `latestServerTick` the sim runs; gradual per-tick nudge bounded to ≤ 0.5 tick/frame, no clock-error disconnect (extreme error is hard-snapped by the game session). The hard sim ceiling (`GetSimTickCeiling()`) sits `kiSimCeilingSlackTicks` above the servo target so steady-state jitter is absorbed by the nudge, not the clamp. No active slot resets `latestServerTick`. Full formulas in [Network.md](../../../../Documents/Architecture/Network.md).
- **Metrics**: bytes in/out per second (host counters), interarrival jitter (deviation from expected tick interval), and packet-loss percent (received vs. expected for active slots) — jitter feeds clock correction. Both jitter expectation and packet-loss expected-frame count scale with the debug timescale (`mTimeStep.SimToWall` / `miTimeMultiply`/`miTimeDivide`), so both metrics stay meaningful under non-1× server speed; the fixed `kiJitterSafetyUs` safety margin is unaffected.
- **Queue-build scratch sizing**: the queue-build stack array is sized to the engine ceiling `NetworkManager::kiMaxEnetCoordSlots`, so raising the client's desired-slot count cannot overflow it; the discovery address buffer is IPv4 dotted-quad only.

## Clock & Pipeline RTT

Pipeline RTT seeded from the handshake wall-clock delta, then refined from a client timestamp echoed in each coord update (monotonic guard prevents duplicate processing during multi-frame ticks). Resends skip RTT/jitter processing — off-cadence arrivals would corrupt the interarrival jitter estimate.

## Other

- **GUID**: versioned `ClientGuid.bin` under `FileFlags::kAppDataDirectory`. Load and store live in `ClientSessionBase` (not the `Client` transport peer): the session loads the GUID before connect and hands it to the `Client` ctor with a persist callback; the peer holds only the in-memory GUID and invokes that callback on connection accept, which writes atomically so a mid-write crash can't orphan server-side state.
- **Disconnect**: resets all session state and calls `CoordFrames::ResetClientState` on every coord.
- **LAN discovery**: scanner auto-restarts on timeout.
- **Desync debug mode**: freezes the ACK floor (received-tick tracking becomes a no-op) so the server keeps resending while the captured debug frame is inspected.
- **Network simulation**: fast-forward (time multiply > 1) bypasses the delay queue and flushes pending; slot reuse and unsubscribe-ack paths purge delayed packets on that slot's channels.

## See Also

- Parent: [../AGENTS.md](../AGENTS.md)
- Game session + reconciliation: [Projects/BrokenEngineSandbox/Source/Network/Client/AGENTS.md](../../../../Projects/BrokenEngineSandbox/Source/Network/Client/AGENTS.md)
- [Network.md](../../../../Documents/Architecture/Network.md)
