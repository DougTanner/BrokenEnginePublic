# `/Engine/Source/Network/` - Networking

See also: [Network Architecture](../../../Documents/Architecture/Network.md)

## Overview

Client/server networking infrastructure using the ENet reliable UDP library. Provides slot-based coord subscriptions, binary serialization with LZ4 compression, and LAN discovery. Used by both client and server builds.

## Key Classes

- **NetworkManager** (`gpNetworkManager`) - Singleton managing ENet lifecycle. Defines the channel layout: control channels (reliable + unreliable) plus paired reliable/unreliable channels per coord slot. Constructed in `Main.cpp` for both builds
- **NetworkProtocol** - Wire protocol definitions: `PacketType` enum, `PlayerStateType` enum, `ClientRequestFlags`, protocol constants, LAN discovery constants, and optional network simulation infrastructure (`NetworkSimulationLevel` enum in `Pch.h` with region presets)
- **ServerNetwork** (`gpServerNetwork`, `ServerNetwork/` subfolder) - Server-side ENet host. Split across three `.cpp` files (core, receive, send). Manages client connections, build config handshake, slot-based coord subscriptions, per-coord ring buffers for resend support, and per-client ACK state with epoch-based slot reuse protection
- **ClientNetwork** (`gpClientNetwork`, `ClientNetwork/` subfolder) - Client-side ENet peer. Split across three `.cpp` files (core, receive, send). Manages server connection, coord slot subscriptions with state machine (kUnsubscribed -> kSubscribing -> kWaitingFullState -> kActive -> kUnsubscribing), per-slot ACK tracking, and pipeline RTT measurement
- **NetworkDiscovery** - LAN server discovery via UDP broadcast. `NetworkDiscoveryResponder` (server) replies to probes; `NetworkDiscoveryScanner` (client) broadcasts and polls for responses
- **ClientSessionBase** (`ClientNetwork/ClientSessionBase.h`, client-only) - Base class for game-level client sessions. Owns `ClientNetwork` and `NetworkDiscoveryScanner`
- **ServerSessionBase** (`ServerNetwork/ServerSessionBase.h`, server-only) - Base class for game-level server sessions. Owns `NetworkDiscoveryResponder` and the Windows waitable timer for fixed-rate server ticks
- **NetworkCursor** - Inline cursor-based binary read/write helpers for packet assembly/parsing
- **NetworkSerialization** - Binary serialization of `game::StatusChange` batches for network transport, with LZ4 compress/decompress variants

## Architecture Notes

- **Slot-based subscriptions**: Each client subscribes to grid coords via numbered slots. Each slot maps to one coord with independent ACK tracking (floor + 64-bit bitfield + epoch). Full state uses the slot's reliable channel; updates and resends use the slot's unreliable channel
- **Data flow**: Server buffers compressed StatusChange deltas per-coord each tick, then sends one unreliable packet per active client subscription slot. AI inputs are deterministic and not sent over the wire
- **Resend system**: Client ACK stream packets carry per-slot (slot, epoch, floor, bitfield) tuples. Server scans unset bits to find missing frames and resends them as separate unreliable packets
- **Build config handshake**: Client sends `kClientHello` with build config name on connect; server validates and accepts/rejects
- **Network simulation**: When enabled via compile-time enum, both sides inject packet loss and variable latency on received unreliable packets. Zero overhead when disabled via `if constexpr`
