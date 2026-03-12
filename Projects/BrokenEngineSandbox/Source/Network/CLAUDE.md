# `/Network/` - Client and Server Sessions

## Overview

Game-level networking sessions encapsulating client-mode and server-mode multiplayer orchestration. Each session class inherits from an engine-layer base class and owns all build-specific networking data and methods, extracted from Game to separate networking concerns from the core game coordinator.

## Key Classes/Systems

- **ClientSession** (`game::ClientSession`, accessed via `game::gpClientSession`, client-only `#ifdef BT_CLIENT`) - Inherits from `engine::ClientSessionBase` (which owns `mpNetworkClient` and `mpDiscoveryScanner`). Split across three `.cpp` files: `ClientSession.cpp` (connection, polling, subscription management, extrapolation, clock correction), `ClientSessionReconcile.cpp` (reconciliation orchestrator, CRC fast-path, reconcile result application), and `ClientSessionReconcileReplay.cpp` (replay pipeline helpers: rollback, replay range, tick replay, catch-up). Owns all client reconciliation state: `ReconcileContext`, `CoordReconcileWork` items, confirmed human state, desync debug state, subscription queue, and the `PersistentWorker` reconcile thread. Game owns `mpClientSession` (`unique_ptr<ClientSession>`, `#ifdef BT_CLIENT`).

- **ServerSession** (`game::ServerSession`, accessed via `game::gpServerSession`, server-only `#ifdef BT_SERVER`) - Inherits from `engine::ServerSessionBase` (which owns the `NetworkDiscoveryResponder` for LAN auto-detection). Owns all server-specific data: pending player destroys, client spawn queue, dead client tracking, pre-spawn player ID snapshots, broadcast spawn/transfer maps, and pending subscription updates. Game owns `mpServerSession` (`unique_ptr<ServerSession>`, `#ifdef BT_SERVER`).

- **Supporting structs** - Server: `ClientSpawnInfo` (client ID + spawn coord), `PendingPlayerDestroy` (coord + player ID for destroy StatusChange injection), `SubscriptionUpdate` (client ID + new coord + new player ID for cross-cell transfer handling).

## Architecture Notes

**Client Session**: Four main-loop integration methods called from `GameBase`: `PollAndReconcile()` (before physics: clock correction, reconciliation wait/apply), `PollNetwork()` (called from `PostTick()`: polls NetworkClient, processes full states/updates/player states/assignments, manages subscriptions), `PostTick()` (after physics: polls network, kicks reconciliation, sends ACKs, flushes), and `PostRender()` (after render: kicks reconciliation so snapshots remain valid for rendering). Manages client-driven coord subscriptions (`UpdateSubscriptions()`/`TrySubscribeNext()` with sequential subscription), extrapolation snapshot ring buffer, and rollback-and-replay reconciliation on a dedicated `PersistentWorker` thread.

**Server Session**: `PreTickNetwork()` is the single pre-physics entry point called from `GameBase::UpdateServer()`, encapsulating NetworkServer polling, discovery responder polling, disconnect handling, new client handling, and spawn request processing. The post-physics methods (`FinalizeNewClients`, `DetectPlayerDeaths`, `HandleSubscriptionUpdates`, `BroadcastStatusChanges`) are called per-physics-frame inside `GameBase::TickFrames()` to ensure clients receive updates for every simulated frame.

## See Also
- Engine client base class: [../../../../Engine/Source/Network/NetworkClient/ClientSessionBase.h](../../../../Engine/Source/Network/NetworkClient/ClientSessionBase.h)
- Engine server base class: [../../../../Engine/Source/Network/NetworkServer/ServerSessionBase.h](../../../../Engine/Source/Network/NetworkServer/ServerSessionBase.h)
- Network subsystem: [../../../../Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md)
