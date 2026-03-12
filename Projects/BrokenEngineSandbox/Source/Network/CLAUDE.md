# `/Network/` - Client and Server Sessions

## Overview

Game-level networking sessions that inherit from engine base classes and encapsulate all multiplayer orchestration. Separates networking concerns from the core Game coordinator. Each session is conditionally compiled (`BT_CLIENT` / `BT_SERVER`) and owned by Game via `unique_ptr`.

## Key Classes

- **ClientSession** (`game::gpClientSession`, `#ifdef BT_CLIENT`) - Inherits `engine::ClientSessionBase`. Manages server connection, client-driven coord subscriptions (human cell + quadrant neighbors), extrapolation snapshot ring buffer, clock correction, and rollback-and-replay reconciliation on a dedicated `PersistentWorker` thread. Split across three `.cpp` files by responsibility: core session, reconciliation orchestration, and replay pipeline helpers.

- **ServerSession** (`game::gpServerSession`, `#ifdef BT_SERVER`) - Inherits `engine::ServerSessionBase`. Manages active set computation, fixed-rate tick broadcasting, client spawn/disconnect lifecycle, player death detection, cross-cell transfer harvesting, and subscription updates.

## Architecture Notes

- **Client main-loop integration**: `PollAndReconcile()` (before physics), `PostTick()` (after physics), `PostRender()` (after render). Reconciliation runs asynchronously and results are applied on the next frame.
- **Server main-loop integration**: `PreTickNetwork()` (pre-physics polling and client handling), `PrepareTick()` (per-tick active set), `BroadcastTick()` (per-tick state broadcast), `WaitForTick()` (fixed-rate timer).

## See Also
- Engine client base: [ClientSessionBase.h](../../../../Engine/Source/Network/ClientNetwork/ClientSessionBase.h)
- Engine server base: [ServerSessionBase.h](../../../../Engine/Source/Network/ServerNetwork/ServerSessionBase.h)
- Network subsystem: [../../../../Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md)
