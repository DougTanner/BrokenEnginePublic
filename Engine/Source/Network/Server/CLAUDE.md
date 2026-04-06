# `/Engine/Source/Network/Server/` - Server Networking

## Overview

Server-side ENet networking split into the low-level `Server` class (host, client connections, packet I/O, frame buffering) and the engine-generic `ServerSessionBase` (tick timing, network polling). Server-only (`BT_SERVER`).

## Key Classes

- **Server** - ENet host managing multiple client connections with slot-based coord subscriptions, per-client ACK state with epoch-based slot reuse protection, per-coord ring buffers of LZ4-compressed deltas for resend support, and full frame ring buffer for debug requests. Handles connection handshake, spawn requests, subscribe/unsubscribe, desync reports, debug frame requests, client resync, pause requests, timespeed requests (`ClientTimespeedRequest` adjusts `GameBase::mTimeStep` and calls `BroadcastTimespeedUpdate` to notify all clients; `SendTimespeedUpdate(ENetPeer*, int64_t, int64_t)` sends to a single peer for newly connected clients if timespeed is not 1x), save/load requests (`ClientSaveRequest`/`ClientLoadRequest` delegate to the game layer; `BroadcastLoadNotification` sends `kServerLoadNotification` to all connected clients after a load; `ClearBufferedFrames` purges per-coord ring buffers), update player requests (`ClientUpdatePlayerRequest` queues a `PendingUpdatePlayerRequest`; `DrainPendingUpdatePlayerRequests` returns and clears the vector for game-layer processing), replay requests (`ClientReplayRecordRequest` sets `GameFlags::kSaveReplay`; `ClientReplayPlaybackRequest` sets `GameFlags::kLoadReplay`; both are `BT_SERVER`-only handlers that validate the handshake before delegating to `game::gpGame->mGameFlags`), and reset requests (`ClientResetRequest` delegates to `GameSaveLoad::ServerReset`, which creates a fresh frame, resets the global ID counter to 1, and broadcasts `kServerLoadNotification` via `ResetClientsForLoad`). When network simulation is enabled and timespeed is accelerated (`miTimeMultiply > 1`), received packets bypass the delay queue and queued packets are flushed immediately; on disconnect, delayed packets for that peer are purged from the queue. GUID generation uses `UuidCreate` (Rpcrt4.lib) in `ClientHello` and is included in `SendConnectionResponse`. Reuses a persistent compression scratch buffer for LZ4 operations. Split across three `.cpp` files: core, receive, send
- **ServerSessionBase** - Base class for game-level server sessions. Owns `NetworkDiscoveryResponder` and a Windows high-resolution waitable timer for fixed-rate server ticks. Provides tick timing (`WaitForTick`), network polling (`PollNetworkBase`), and new subscription full-state sending
- **ServerTypes** - Shared struct definitions: `ClientCoordSubscription`, `PendingSpawnRequest`, `PendingDisconnect` (carries the full list of owned `global_player_t` IDs and coords for that client), `PendingNewSubscription`, `GridUpdateData`, `PendingUpdatePlayerRequest` (carries a `global_player_t`, weapon mode flag, and navigation delay for per-player settings updates)

## Architecture Notes

- `ClientConnection` tracks per-client state: ENet peer, `clientGuid` (128-bit persistent identity), handshake completion flag, lists of owned `global_player_t` IDs and their corresponding grid coords (one client may own multiple players), slot-based coord subscriptions with independent ACK tracking and slot management helpers, and pipeline RTT timestamp echo
- Receive handlers (ACK stream, spawn request, subscribe, resync) reject packets from clients that haven't completed the ClientHello handshake
- Subscribe requests are validated against any of the client's owned player coords: only coords within the 3x3 adjacency grid of at least one owned player are accepted; out-of-range requests receive a rejection response (slot `0xFF`) so the client can clean up
- Server buffers compressed StatusChange deltas per-coord each tick, then sends one unreliable packet per active client subscription slot. Resend uses the same buffered data
- Pending events (spawns, disconnects, new subscriptions, resync requests) are drained by the game layer each tick

## See Also

- Parent: [../CLAUDE.md](../CLAUDE.md)
- Game-layer session: [../../../../Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md)
