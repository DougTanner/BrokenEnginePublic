# `/Engine/Source/Network/Client/` - Client Networking

## Overview

Client-side ENet peer (`Client`, `gpClient`) and engine-generic `ClientSessionBase`. Game sessions inherit the session base. Client-only (`BT_CLIENT`).

## Invariants

- **Drain-per-poll**: `Poll()` clears all receive buffers on entry; callers must consume every `Drain*` output before the next poll.
- **Slot-active gate**: updates for non-`kActive` slots are dropped; `try_emplace` keeps first arrival; buffer overflow asserts.
- **Epoch check on every receive handler**: mismatches silently dropped — this is what makes slot reuse safe across rapid (un)subscribe cycles.
- **Out-of-order tolerance**: full-state can arrive before subscribe-accept (different ENet channels); handlers reconcile the placeholder slot either way.
- **Cancelled-subscription ghosts**: locally-dropped `kSubscribing` slots record the coord; late accept/full-state triggers an unsubscribe. One epoch-heal case covers legitimate re-subscribe to an already-active slot.
- **Gap beyond `kiNetworkBufferSize`** on a single slot forces disconnect.

## Subscription State Machine

`kUnsubscribed -> kSubscribing -> kWaitingFullState -> kActive -> kUnsubscribing`.

## Clock & Pipeline

Pipeline RTT seeded from handshake wall-clock delta, refined via client timestamp echoed in each server coord update (monotonic guard prevents duplicate processing during multi-frame ticks). Clock correction uses jitter-derived target with 2-tick hysteresis; sustained error forces disconnect. Full formulas in [Network.md](../../../../Documents/Architecture/Network.md).

## Other

- **ENet tuning**: 1 MB socket buffers; peer throttle disabled so reconciliation stalls don't drop unreliable traffic.
- **GUID**: versioned `ClientGuid.bin` under `FileFlags::kAppDataDirectory`; loaded on hello, written on connection accept.
- **Disconnect**: resets all session state and calls `CoordFrames::ResetClientState` on every coord.
- **LAN discovery**: scanner auto-restarts on timeout.
- **Network simulation**: fast-forward bypasses the delay queue and flushes pending; slot reuse and unsubscribe-ack paths purge delayed packets on that slot's channels.

## See Also

- Parent: [../CLAUDE.md](../CLAUDE.md)
- Game session: [Projects/BrokenEngineSandbox/Source/Network/Client/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Network/Client/CLAUDE.md)
- [Network.md](../../../../Documents/Architecture/Network.md)
