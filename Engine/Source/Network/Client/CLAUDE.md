# `/Engine/Source/Network/Client/` - Client Networking

## Overview

Client-side ENet peer (`Client`, `gpClient`) plus engine-generic `ClientSessionBase`. The peer owns the wire: receive-handler dispatch, per-slot subscription state, ACK/RTT/jitter/bandwidth tracking. The session base owns the per-frame policy: subscription queue, stale-coord unsubscribe, draining received updates into `CoordFrames`, clock-correction, and LAN-discovery polling. Game sessions inherit the base. Client-only (`BT_CLIENT`).

## Receive Paths

Four distinct server-update kinds land in separate buffers: per-tick coord delta updates (one buffer per slot), full state (placeholder-adopt + slot activate), static data (sent once per subscription; carries NavData), and debug frames (one-shot, captured for desync inspection). Channel/ACK/drain-per-poll conventions are the hub's — see parent.

## Subscription Receive Invariants

Each receive handler classifies into a flag set (commit / clear-placeholder / heal-epoch / reject-as-ghost) before mutating slot state; the classify step is the choke point for the epoch and ghost logic below.

- **Epoch check on every receive**: mismatches silently dropped — what makes slot reuse safe across rapid (un)subscribe cycles.
- **Out-of-order tolerance**: full state can arrive before subscribe-accept (different ENet channels); handlers reconcile the placeholder slot either way.
- **Pre-full-state buffering**: a `kWaitingFullState` slot accepts delta updates but does NOT advance its ACK tick floor (only `kActive` slots track received ticks).
- **Cancelled-subscription ghosts**: locally-dropped `kSubscribing` slots record the coord; a late accept/full-state triggers an unsubscribe. One epoch-heal case covers legitimate re-subscribe to an already-active slot.
- **Gap beyond `kiNetworkBufferSize`** on a single slot forces disconnect.

## Session Policy (`ClientSessionBase`)

- **Subscription queue** rebuilt each frame from the game's desired-coord set (active slots excluded); drained into free/freeing slots when any is available, else suppressed-logged.
- **Stale-coord unsubscribe**: active slots whose coord left the desired set are cancelled (if still `kSubscribing`) or sent an unsubscribe; the matching `CoordFrames` entry is erased.
- **Update apply** moves drained per-tick updates into each active slot's `CoordFrames::serverUpdates` via `try_emplace` (first arrival wins, stale ticks skipped); buffer overflow asserts.
- **Clock correction**: jitter-derived `targetBehind` (jitter + fixed safety margin, 2-tick hysteresis) sets how far behind `latestServerTick` the sim runs; gradual per-tick nudge, sustained error past threshold forces disconnect. No active slot resets `latestServerTick`. Full formulas in [Network.md](../../../../Documents/Architecture/Network.md).
- **Metrics**: bytes in/out per second (host counters), interarrival jitter (deviation from expected tick interval), and packet-loss percent (received vs. expected for active slots) — jitter feeds clock correction.

## Clock & Pipeline RTT

Pipeline RTT seeded from the handshake wall-clock delta, then refined from a client timestamp echoed in each coord update (monotonic guard prevents duplicate processing during multi-frame ticks).

## Other

- **GUID**: versioned `ClientGuid.bin` under `FileFlags::kAppDataDirectory`; loaded on hello, written atomically on connection accept so a mid-write crash can't orphan server-side state.
- **Disconnect**: resets all session state and calls `CoordFrames::ResetClientState` on every coord.
- **LAN discovery**: scanner auto-restarts on timeout.
- **Network simulation**: fast-forward (time multiply > 1) bypasses the delay queue and flushes pending; slot reuse and unsubscribe-ack paths purge delayed packets on that slot's channels.

## See Also

- Parent: [../CLAUDE.md](../CLAUDE.md)
- Game session + reconciliation: [Projects/BrokenEngineSandbox/Source/Network/Client/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Network/Client/CLAUDE.md)
- [Network.md](../../../../Documents/Architecture/Network.md)
