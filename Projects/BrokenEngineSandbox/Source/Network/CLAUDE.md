# `/Network/` - Client and Server Sessions

## Overview

Game-level networking sessions that inherit from engine base classes and encapsulate all multiplayer orchestration. Separates networking concerns from the core Game coordinator. Each session is conditionally compiled (`BT_CLIENT` / `BT_SERVER`) and owned by Game via `unique_ptr`.

## Key Classes

- **ClientSession** (`game::gpClientSession`, `#ifdef BT_CLIENT`) - Inherits `engine::ClientSessionBase`. Handles connection lifecycle, player event processing (assigned/spawned/changed-frame/died), full-state application, subscription management, clock correction, and desync detection/recovery. Split across companion `.cpp` files by responsibility (core, subscriptions). Delegates reconciliation to `ClientReconciler`
- **ClientReconciler** - Rollback-and-replay reconciliation on a dedicated `PersistentWorker` thread. Owns `ReconcileContext` and snapshot state swapped from `CoordFrames` at kick time. Re-syncs human identity from `gpGame` at `Kick()` time. Parallelizes per-coord work via an owned `Multithreading` dispatch pool, controlled by `kbEnableReconcileDispatch` in `Pch.h`. Merges per-coord `ReconcileProfiling` counters into the aggregate context after the dispatch join
- **ReconcileReplay** (`ReconcileReplay.h/.cpp`) - Stateless pipeline helpers for rollback reconciliation. CRC fast-path skips replay when client snapshots match server CRCs. Full path: rollback to confirmed frame, replay server-validated ticks with StatusChange injection, catch-up to target tick. Neighbor coord CRC mismatches are non-fatal; only the human coord CRC mismatch triggers desync
- **ServerSession** (`game::gpServerSession`, `#ifdef BT_SERVER`) - Inherits `engine::ServerSessionBase`. Manages active set computation, tick broadcasting, client spawn/disconnect lifecycle, player death detection, cross-cell transfer harvesting, subscription updates, and resync request handling
- **PlayerEvents** (`PlayerEvents.h/.cpp`) - Game-layer parsing of raw game packets. Defines `PlayerEventType`, `PlayerStateWireType` for wire encoding, and `ReceivedPlayerEvent`. `ParsePlayerEvents()` converts raw packet bytes into typed events
- **NetworkSerialization** (`NetworkSerialization.cpp`) - Game-layer implementation of `engine::NetworkSerialization.h`. Binary serialization of `StatusChange` batches with type-grouped encoding and LZ4 compress/decompress variants

## Architecture Notes

- **ClientSession companion files**: `ClientSession.cpp` (core/reconcile integration), `ClientSessionSubscriptions.cpp` (coord subscription management via `UpdateDesiredCoords()` called at player event points, death screen handling, full-state application)
- **Sticky subscriptions**: When coords drop out of the desired set, they remain active for `kStickySubscriptionDuration` (2 s) via `mUnwantedTimestamps`. `UpdateSubscriptions()` merges unexpired timestamps into the effective list before unsubscribing, preventing flicker during brief coord transitions
- **Soft desync recovery**: On CRC mismatch, client sends `kClientResyncRequest` instead of disconnecting. Server tears down and re-sends full state. Client tracks desync frequency and escalates to disconnect after 3 desyncs within 10 seconds
- **ReconcileProfiling**: Standalone struct (defined in `ClientReconciler.h`) holding tick and event counters for one coord's reconcile pass. Embedded in both `CoordReconcileWork` (per-coord) and `ReconcileContext` (aggregate, via `Profiling` type alias)

## See Also
- Engine client base: [ClientSessionBase.h](../../../../Engine/Source/Network/Client/ClientSessionBase.h)
- Engine server base: [ServerSessionBase.h](../../../../Engine/Source/Network/Server/ServerSessionBase.h)
- Network subsystem: [../../../../Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md)
