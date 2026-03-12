# `/Network/` - Client and Server Sessions

## Overview

Game-level networking sessions that inherit from engine base classes and encapsulate all multiplayer orchestration. Separates networking concerns from the core Game coordinator. Each session is conditionally compiled (`BT_CLIENT` / `BT_SERVER`) and owned by Game via `unique_ptr`.

## Key Classes

- **ClientSession** (`game::gpClientSession`, `#ifdef BT_CLIENT`) - Inherits `engine::ClientSessionBase`. Game-specific connection entry/exit, reconciliation orchestration, subscription coord selection, full-state application, and desync debug comparison. Split across companion `.cpp` files by responsibility (core, subscriptions). Delegates reconciliation to `ClientReconciler`. Engine-generic logic (connection lifecycle, subscription mechanics, extrapolation, clock correction) lives in the base class
- **ClientReconciler** - Rollback-and-replay reconciliation on a dedicated `PersistentWorker` thread. Encapsulates reconciliation state with controlled access from `ClientSession`. `ReconcileReplay` provides the replay pipeline helpers
- **ServerSession** (`game::gpServerSession`, `#ifdef BT_SERVER`) - Inherits `engine::ServerSessionBase`. Game-specific active set computation, tick broadcasting, client spawn/disconnect lifecycle, player death detection, cross-cell transfer harvesting, and subscription updates. Engine-generic logic (tick timing, network polling, full-state sending) lives in the base class
- **NetworkSerialization** (`NetworkSerialization.cpp`) - Game-layer implementation of `engine::NetworkSerialization.h`. Binary serialization of `StatusChange` batches with type-grouped encoding and LZ4 compress/decompress variants

## Architecture Notes

- **ClientSession companion files**: `ClientSession.cpp` (core/reconcile integration), `ClientSessionSubscriptions.cpp` (coord subscription management, full-state application)
- **Client main-loop integration**: `PollAndReconcile()` (before physics), `PostTick()` (after physics), `PostRender()` (after render). Reconciliation runs asynchronously and results are applied on the next frame
- **Server main-loop integration**: `PreTickNetwork()` (pre-physics polling and client handling), `PrepareTick()` (per-tick active set), `BroadcastTick()` (per-tick state broadcast), `WaitForTick()` (delegates to base class fixed-rate timer)

## See Also
- Engine client base: [ClientSessionBase.h](../../../../Engine/Source/Network/ClientNetwork/ClientSessionBase.h)
- Engine server base: [ServerSessionBase.h](../../../../Engine/Source/Network/ServerNetwork/ServerSessionBase.h)
- Network subsystem: [../../../../Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md)
