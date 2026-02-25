# `/Engine/Source/Network/` - Networking

## Overview

Provides client/server networking infrastructure using the ENet reliable UDP library. NetworkManager is a singleton (`gpNetworkManager`) that owns the ENet library lifecycle. NetworkServer and NetworkClient handle the server-side and client-side communication respectively, using a custom binary protocol defined in NetworkProtocol. NetworkSerialization handles compact binary serialization and LZ4 compression of game state for network transmission. Used by both client and server builds.

## Key Classes/Systems

- **NetworkManager** - Singleton managing ENet library initialization and shutdown. Defines channel constants for reliable and unreliable communication. Constructed in `Main.cpp` for both client and server builds.

- **NetworkProtocol** - Defines the wire protocol: `PacketType` enum for all packet types (server assign/full-state/update-stream, client input/spawn/desync), `ClientRequestFlags` for spawn/respawn requests, and protocol constants (default port, max resend frames, max buffered frames, max packet size).

- **NetworkServer** (`gpNetworkServer`) - Server-side ENet host managing client connections. Tracks each client's active 3x3 grid region around their player. Handles incoming client input streams (with server-side press detection from held-state deltas), spawn requests, and desync reports. Sends player assignment, full state (LZ4-compressed Frame serialization), and per-tick delta updates (compressed StatusChange batches filtered to each client's active grid cells). Maintains a ring buffer of recent frames for re-send support when clients report missing frames.

- **NetworkClient** (`gpNetworkClient`) - Client-side ENet peer connecting to a server. Sends player input, spawn requests, and desync reports. Receives player assignment, full state snapshots (LZ4-decompressed into Frame objects), and delta update streams (decompressed StatusChange batches per grid cell with CRC for desync detection). Tracks received frame numbers and detects gaps to request re-sends, which are piggybacked onto the input stream packet.

- **NetworkSerialization** (free functions) - Type-specific binary serialization of `game::StatusChange` batches for network transport. Groups changes by `StatusChangeType`, then serializes each group with a type+count header followed by per-type field layouts (blaster, spaceship, missile, player transfers). Uses cursor-based read/write helpers for compact wire format. Compress/decompress variants add LZ4 compression with a 4-byte uncompressed-size prefix. Uses the workbuffer for intermediate storage during serialization and compression passes. Missile transfers include `BT_CLIENT`-gated smoke trail IDs.

## Architecture Notes

The server broadcasts delta updates (compressed StatusChange batches) each tick to connected clients, filtered to each client's 3x3 active grid region. Full state transfers use LZ4-compressed Frame serialization and are sent on initial connect or when a client's active region changes. Both server and client use the workbuffer for packet assembly and cursor-based binary read/write helpers for deserialization. Re-send support uses a server-side ring buffer of buffered frames; clients detect gaps via frame number tracking and piggyback re-send requests onto their input packets.
