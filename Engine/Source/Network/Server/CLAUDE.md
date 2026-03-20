# `/Engine/Source/Network/Server/` - Server Networking

## Overview

Server-side ENet networking split into the low-level `Server` class (host, client connections, packet I/O, frame buffering) and the engine-generic `ServerSessionBase` (tick timing, network polling). Server-only (`BT_SERVER`).

## Key Classes

- **Server** - ENet host managing multiple client connections with slot-based coord subscriptions, per-client ACK state with epoch-based slot reuse protection, per-coord ring buffers of LZ4-compressed deltas for resend support, and full frame ring buffer for debug requests. Handles connection handshake, spawn requests, subscribe/unsubscribe, desync reports, debug frame requests, client resync, and pause requests. Reuses a persistent compression scratch buffer for LZ4 operations. Split across three `.cpp` files: core, receive, send
- **ServerSessionBase** - Base class for game-level server sessions. Owns `NetworkDiscoveryResponder` and a Windows high-resolution waitable timer for fixed-rate server ticks. Provides tick timing (`WaitForTick`), network polling (`PollNetworkBase`), and new subscription full-state sending
- **ServerTypes** - Shared struct definitions: `ClientCoordSubscription`, `PendingSpawnRequest`, `PendingDisconnect`, `PendingNewSubscription`, `GridUpdateData`

## Architecture Notes

- `ClientConnection` tracks per-client state: ENet peer, player identity, handshake completion flag, slot-based coord subscriptions with independent ACK tracking and slot management helpers, and pipeline RTT timestamp echo
- Receive handlers (ACK stream, spawn request, subscribe, resync) reject packets from clients that haven't completed the ClientHello handshake
- Server buffers compressed StatusChange deltas per-coord each tick, then sends one unreliable packet per active client subscription slot. Resend uses the same buffered data
- Pending events (spawns, disconnects, new subscriptions, resync requests) are drained by the game layer each tick

## See Also

- Parent: [../CLAUDE.md](../CLAUDE.md)
- Game-layer session: [../../../../Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md)
