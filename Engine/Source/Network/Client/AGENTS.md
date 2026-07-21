# `/Engine/Source/Network/Client/` - Client Networking

## Overview

Client-side ENet peer (`Client`, `gpClient`) plus composed `ClientSessionRuntime`. The peer owns wire dispatch, slot state, ACK/RTT/jitter/bandwidth tracking, and receive buffers. The runtime owns GUID persistence, discovery, subscription orchestration, clock correction, ordered polling/draining, ACK/flush, and generic reset state. The concrete [game client session](../../../../Projects/BrokenEngineSandbox/Source/Network/Client/AGENTS.md) supplies adoption, reconciliation, UI, and gameplay policy hooks. Client-only (`BT_CLIENT`).

## Receive Paths

Server-update kinds land in separate buffers: per-tick coord delta updates (one buffer per slot), full state (placeholder-adopt + slot activate), static data (sent once per subscription; carries NavData), debug frames (one-shot, captured for desync inspection), and a one-shot load-notification flag the game layer drains. Channel/ACK/drain-per-poll conventions are the hub's — see parent.

## Subscription Receive Invariants

Most receive handlers (full-state, coord-update, subscribe-accept) classify into a flag set (commit / clear-placeholder / heal-epoch / reject-as-ghost) before mutating slot state, the choke point for the epoch and ghost logic below; static data applies the same logic inline.

- **Epoch check** (drop rationale: hub's slot ACK model) applies only where the slot has a server-assigned epoch — a `kSubscribing` placeholder has none yet.
- **Out-of-order full state** (before subscribe-accept) adopts the coord only when a matching `kSubscribing` placeholder still exists, clearing that placeholder at whichever slot holds it. Without that placeholder, the full state is a ghost and triggers an epoch-qualified unsubscribe.
- **Static data** applies the same coord-identity check as full state on `kWaitingFullState`/`kSubscribing` slots (silent drop on coord mismatch — the full-state path owns the ghost unsubscribe), with the epoch guard only on `kWaitingFullState`; `kUnsubscribed` accepts (out-of-order; buffers only, no slot mutation).
- **Pre-full-state buffering**: a `kWaitingFullState` slot accepts delta updates but does not advance its ACK tick floor (only `kActive` slots track received ticks).
- **Cancelled-subscription ghosts**: locally-dropped `kSubscribing` slots record the coord; a late accept/full-state triggers an unsubscribe carrying the epoch from that packet. One epoch-heal case covers legitimate re-subscribe to an already-active slot.
- **Resync re-commit**: a full state on a still-`kActive` slot commits only when its coord and epoch both match the slot — the server resends full state on live slots to re-sync a desynced client, re-baselining the slot's ACK floor and adopting a fresh frame; any other `kActive` full state (coord/epoch mismatch) or an `kUnsubscribing` slot is dropped.
- **Out-of-range accept guard**: a subscribe-accept with a non-`kuiSubscribeRejectSlot` index beyond the client slot pool is a trust-boundary violation — the client immediately unsubscribes to prevent a server-side slot from leaking.
- **Gap beyond `kiNetworkBufferSize`** on a single slot forces disconnect.
- **Activation and adoption are same-frame**: a full state mutates slot ACK/epoch/state immediately at receive time (`ClientReceive.cpp` `ServerCoordFullState`), but its frame payload is adopted later by the game-layer drain. Because `Poll()` clears all receive buffers at entry (drain-per-poll), a game layer that skips a drain loses the frame yet keeps the activated slot — so activation and adoption must both happen in the same frame.

## Transport Timing Inputs

The transport seeds pipeline RTT from the handshake wall-clock delta, then refines it from a client timestamp echoed in each coord update; a monotonic guard prevents duplicate processing during multi-frame ticks. Resends skip RTT/jitter processing because off-cadence arrivals would corrupt the interarrival jitter estimate. The game session consumes these measurements for clock correction; formulas live in [Network.md](../../../../Documents/Architecture/Network.md).

## Other

- **Desync debug mode**: real desync handling enters this mode only in game builds with `kbDesyncDebugFrames` enabled; it freezes the ACK floor (received-tick tracking becomes a no-op) so the server keeps resending while the captured debug frame is inspected. Disabled builds never request a debug frame or freeze ACK tracking for a real desync. The synthetic agent full-state fixture explicitly enters the same engine mode regardless of that compile flag and freezes ACK tracking until the fixture is cleared or reset.
- **Network simulation**: fast-forward (time multiply > 1) bypasses the delay queue and flushes pending; slot reuse and unsubscribe-ack paths purge delayed packets on that slot's channels. If ENet reports disconnect before a delayed reliable rejection is released, the client delivers that queued rejection first so teardown cannot suppress its reason.

## See Also

- Parent: `../AGENTS.md`
- Game session + reconciliation: [Projects/BrokenEngineSandbox/Source/Network/Client/AGENTS.md](../../../../Projects/BrokenEngineSandbox/Source/Network/Client/AGENTS.md)
- [Network.md](../../../../Documents/Architecture/Network.md)
