# `/Engine/Source/Network/` - Networking

See also: [Network Architecture](../../../Documents/Architecture/Network.md)

## Overview

Client/server networking infrastructure using the ENet reliable UDP library. Provides slot-based coord subscriptions, binary serialization with LZ4 compression, and LAN discovery. Used by both client and server builds.

## Key Classes

- **NetworkManager** (`gpNetworkManager`) - Singleton managing ENet lifecycle. Defines the channel layout: control channels (reliable + unreliable) plus paired reliable/unreliable channels per coord slot. Constructed in `Main.cpp` for both builds
- **NetworkProtocol** - Wire protocol definitions: `PacketType` enum, `ClientRequestFlags`, `AckState`, protocol constants, and LAN discovery constants
- **NetworkSimulation** - Optional latency/loss simulation for testing. Region presets, delay queue with sorted insertion, and packet drop logic. Enabled via compile-time `NetworkSimulationLevel` enum in `Pch.h`; zero overhead when disabled via `if constexpr`
- **Server** (`gpServer`, `Server/` subfolder) - Server-side ENet host. Split across three `.cpp` files (core, receive, send). Manages client connections, build config handshake, slot-based coord subscriptions, per-coord ring buffers for resend support, and per-client ACK state with epoch-based slot reuse protection. Send methods for player assignment and player state use raw `int64_t` player IDs (not game-typed `player_t`)
- **Client** (`gpClient`, `Client/` subfolder) - Client-side ENet peer. Split across three `.cpp` files (core, receive, send). Manages server connection, coord slot subscriptions with state machine (kUnsubscribed -> kSubscribing -> kWaitingFullState -> kActive -> kUnsubscribing), per-slot ACK tracking, and pipeline RTT measurement. Game-specific packets (player assignment, player state) are stored as raw bytes and forwarded to the game layer for parsing via `DrainReceivedGamePackets()`
- **NetworkDiscovery** - LAN server discovery via UDP broadcast. `NetworkDiscoveryResponder` (server) replies to probes; `NetworkDiscoveryScanner` (client) broadcasts and polls for responses
- **ClientSessionBase** (`Client/ClientSessionBase.h/.cpp`, client-only) - Base class for game-level client sessions. Owns `Client` and `NetworkDiscoveryScanner`. Provides engine-generic connection lifecycle, coord subscription mechanics, extrapolation snapshot ring buffer management, clock correction, and query methods
- **ServerSessionBase** (`Server/ServerSessionBase.h/.cpp`, server-only) - Base class for game-level server sessions. Owns `NetworkDiscoveryResponder` and the Windows waitable timer for fixed-rate server ticks. Provides engine-generic tick timing (`WaitForTick`), network polling (`PollNetworkBase`), and new subscription full-state sending
- **ServerTypes** (`Server/ServerTypes.h`) - Shared struct definitions used by both `Server` and game-layer code: `ClientCoordSubscription`, `PendingSpawnRequest`, `PendingDisconnect`, `PendingNewSubscription`, `GridUpdateData`
- **NetworkCursor** - Inline cursor-based binary read/write helpers for packet assembly/parsing
- **NetworkSerialization** (`NetworkSerialization.h`) - Interface for binary serialization of `game::StatusChange` batches with LZ4 compress/decompress variants. Implementation lives in the game layer (`Projects/.../Network/NetworkSerialization.cpp`)

## Architecture Notes

- **Slot-based subscriptions**: Each client subscribes to grid coords via numbered slots. Each slot maps to one coord with independent ACK tracking (floor + 64-bit bitfield + epoch). Full state uses the slot's reliable channel; updates and resends use the slot's unreliable channel
- **Data flow**: Server buffers compressed StatusChange deltas per-coord each tick, then sends one unreliable packet per active client subscription slot. AI inputs are deterministic and not sent over the wire
- **Resend system**: Client ACK stream packets carry per-slot (slot, epoch, floor, bitfield) tuples. Server scans unset bits to find missing frames and resends them as separate unreliable packets
- **Connection handshake**: Client sends `kClientHello` with protocol version and build config name; server validates both and accepts/rejects
- **Network simulation**: When enabled via compile-time enum, both sides inject packet loss and variable latency on received unreliable packets. Implementation in `NetworkSimulation.h`. Zero overhead when disabled via `if constexpr`

## See Also

- [Client/CLAUDE.md](Client/CLAUDE.md) - Client-side ENet peer and ClientSessionBase
