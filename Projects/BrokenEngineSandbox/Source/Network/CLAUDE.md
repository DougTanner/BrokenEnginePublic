# `/Network/` - Client and Server Sessions

## Overview

Game-level networking sessions that inherit from engine base classes and encapsulate all multiplayer orchestration. Separates networking concerns from the core Game coordinator. Each session is conditionally compiled (`BT_CLIENT` / `BT_SERVER`) and owned by Game via `unique_ptr`.

## Key Classes

- **ClientSession** (`game::gpClientSession`, `#ifdef BT_CLIENT`) - Inherits `engine::ClientSessionBase`. Game-specific connection entry/exit, reconciliation orchestration, subscription coord selection, full-state application, desync debug comparison, and soft desync recovery (requests full state re-download instead of disconnecting, with frequency-based escalation to disconnect after repeated desyncs). Split across companion `.cpp` files by responsibility (core, subscriptions). Delegates reconciliation to `ClientReconciler`. Engine-generic logic (connection lifecycle, subscription mechanics, extrapolation, clock correction) lives in the base class
- **ClientReconciler** - Rollback-and-replay reconciliation on a dedicated `PersistentWorker` thread. Encapsulates reconciliation state with controlled access from `ClientSession`
- **ReconcileReplay** (`ReconcileReplay.h/.cpp`) - Stateless pipeline helpers for rollback reconciliation. CRC fast-path skips replay when client snapshots match server CRCs. Full reconciliation path: rollback to confirmed frame, replay server-validated ticks with StatusChange injection, catch-up to target tick. Handles per-coord workspace frame management, human player migration tracking across grid transfers, and post-replay desync detection
- **ServerSession** (`game::gpServerSession`, `#ifdef BT_SERVER`) - Inherits `engine::ServerSessionBase`. Game-specific active set computation, tick broadcasting, client spawn/disconnect lifecycle, player death detection, cross-cell transfer harvesting, subscription updates, and resync request handling (tears down and re-sends full state for all of a client's subscribed coords). Engine-generic logic (tick timing, network polling, full-state sending) lives in the base class
- **PlayerEvents** (`PlayerEvents.h/.cpp`) - Game-layer parsing of raw game packets received from `engine::Client::DrainReceivedGamePackets()`. Defines `PlayerEventType` (assigned/spawned/changed-frame/died), `PlayerStateWireType` for wire encoding, and `ReceivedPlayerEvent` struct. `ParsePlayerEvents()` converts raw packet bytes into typed events
- **NetworkSerialization** (`NetworkSerialization.cpp`) - Game-layer implementation of `engine::NetworkSerialization.h`. Binary serialization of `StatusChange` batches with type-grouped encoding and LZ4 compress/decompress variants

## Architecture Notes

- **ClientSession companion files**: `ClientSession.cpp` (core/reconcile integration), `ClientSessionSubscriptions.cpp` (coord subscription management, full-state application)
- **Client main-loop integration**: `Poll()` (polls network, sends ACK), `Reconcile()` (self-gates on `IsStalled()`, waits for reconciliation, applies tick deficit and clock correction), `PostRender()` (after render, gates on `IsStalled()`). `IsStalled()` is a generic stall check (currently driven by desync debug mode) that gates both reconciliation and physics in `GameBase::ClientUpdate()`. Reconciliation runs asynchronously and results are applied on the next frame
- **Server main-loop integration**: `PreTickNetwork()` (pre-physics polling and client handling), `PrepareTick()` (per-tick active set), `BroadcastTick()` (per-tick state broadcast), `SendResends()` (called once after tick loop, not per-tick), `WaitForTick()` (delegates to base class fixed-rate timer)
- **Soft desync recovery**: On CRC mismatch, client sends `kClientResyncRequest` instead of disconnecting. Server tears down all client subscriptions and re-sends full state. Client tracks desync frequency and escalates to disconnect after 3 desyncs within 10 seconds

## See Also
- Engine client base: [ClientSessionBase.h](../../../../Engine/Source/Network/Client/ClientSessionBase.h)
- Engine server base: [ServerSessionBase.h](../../../../Engine/Source/Network/Server/ServerSessionBase.h)
- Network subsystem: [../../../../Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md)
