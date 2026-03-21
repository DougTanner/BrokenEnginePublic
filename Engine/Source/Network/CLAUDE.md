# `/Engine/Source/Network/` - Networking

See also: [Game Reconciliation](../../../Documents/Architecture/Network/GameReconciliation.md) — update this diagram if reconciliation state machine changes

## Overview

Client/server networking infrastructure using the ENet reliable UDP library. Provides slot-based coord subscriptions, binary serialization with LZ4 compression, and LAN discovery. Used by both client and server builds.

## Key Classes

- **NetworkManager** (`gpNetworkManager`) - Singleton managing ENet lifecycle. Defines channel layout: control channels (reliable + unreliable) plus paired reliable/unreliable channels per coord slot
- **NetworkProtocol** - Wire protocol definitions: `PacketType` enum (includes `kClientPauseRequest` for debug pause, `kClientTimespeedRequest` for debug timespeed control, `kServerTimespeedUpdate` for server broadcast of current timescale, `kClientSaveRequest`/`kClientLoadRequest` for server-authoritative save/load, `kServerLoadNotification` for broadcasting load completion, `kClientWeaponModeRequest` for per-player weapon mode toggle, `kClientReplayRecordRequest` for server-authoritative replay record start/stop, and `kClientReplayPlaybackRequest` for server-authoritative replay playback), `ClientGuid` (128-bit persistent client identity wrapper), `ClientRequestFlags`, `AckState`, protocol constants, and LAN discovery constants
- **NetworkSimulation** - Optional latency/loss simulation for testing. Region presets with delay queue and packet drop logic. Per-level bounds define expected reconciliation stats for profile overlay anomaly detection. When timespeed is accelerated (`miTimeMultiply > 1`), delayed packets are flushed immediately to prevent simulation artifacts. Enabled via compile-time enum in `Pch.h`; zero overhead when disabled via `if constexpr`
- **Server** (`gpServer`, `Server/`) - Server-side ENet host managing client connections, slot-based subscriptions, per-coord frame ring buffers for resend and debug, and ACK state with epoch-based slot reuse protection. Send methods use raw `int64_t` player IDs (not `player_t`)
- **Client** (`gpClient`, `Client/`) - Client-side ENet peer with coord slot subscription state machine, per-slot ACK tracking, pipeline RTT/jitter/bandwidth measurement, and soft desync recovery. Game-specific packets forwarded as raw bytes for game-layer parsing
- **ClientSessionBase** (`Client/`, client-only) - Engine-generic base for game client sessions. Owns `Client` and `NetworkDiscoveryScanner`. Provides connection lifecycle, LAN discovery with auto-restart, coord subscription queue management, update buffering, extrapolation snapshot ring buffers, and clock correction
- **ServerSessionBase** (`Server/`, server-only) - Engine-generic base for game server sessions. Owns `NetworkDiscoveryResponder` and Windows waitable timer for fixed-rate ticks
- **NetworkDiscovery** - LAN server discovery via UDP broadcast (responder on server, scanner on client)
- **ServerTypes** - Shared pending-event structs used by `Server` and game layer
- **NetworkCursor** - Inline cursor-based binary read/write helpers for packet assembly/parsing
- **NetworkSerialization** - Interface for `StatusChange` batch serialization with LZ4 compression. Implementation in game layer

## Architecture Notes

- **Slot-based subscriptions**: Each client subscribes to grid coords via numbered slots. Each slot maps to one coord with independent ACK tracking (floor + 128-bit bitfield + epoch). Full state uses the slot's reliable channel; updates and resends use the slot's unreliable channel
- **Data flow**: Server buffers compressed StatusChange deltas per-coord each tick, then sends one unreliable packet per active client subscription slot. AI inputs are deterministic and not sent over the wire
- **Resend system**: Client ACK stream packets carry per-slot (slot, epoch, floor, bitfield low, bitfield high) tuples. Server scans unset bits to find missing frames and resends them as separate unreliable packets
- **Connection handshake**: Client sends `kClientHello` with protocol version, build config name, and persisted `ClientGuid` bytes; server validates both, assigns or generates a GUID, and returns it in `kServerConnectionResponse`. On accept, server sends `kServerTimespeedUpdate` to the new client if the current timespeed is not 1x. GUIDs are persisted to disk on both ends so clients retain their identity across reconnections
- **Server-authoritative save/load**: Client sends `kClientSaveRequest`/`kClientLoadRequest`; server performs the operation and broadcasts `kServerLoadNotification` after a load. Each session calls `ResetClientsForLoad` (server) or `ResetForServerLoad` (client) to re-link player identities by GUID and clear buffered state
- **Server-authoritative replay**: Client sends `kClientReplayRecordRequest` (F7) or `kClientReplayPlaybackRequest` (F8) as 1-byte reliable packets; server sets `GameFlags::kSaveReplay` or `GameFlags::kLoadReplay` on the game. `GameSaveLoad::SaveLoadReplay()` processes these flags each tick: a second F7 press stops recording and writes per-coord files + manifest + grid state; F8 loads the manifest and grid state, creates per-coord readers, and calls `ResetClientsForLoad`. During replay, `PrepareActiveSet` uses all recorded coords and `ServerSession::PrepareTick` skips `ComputeActiveSet` to preserve the replay active set
- **Network simulation**: When enabled via compile-time enum, both sides inject packet loss and variable latency on received unreliable packets. When timespeed is accelerated, delayed packets are flushed immediately. On disconnect, the server purges delayed packets for that peer. Implementation in `NetworkSimulation.h`. Zero overhead when disabled via `if constexpr`

## See Also

- [Client/CLAUDE.md](Client/CLAUDE.md) - Client-side ENet peer and ClientSessionBase
- [Server/CLAUDE.md](Server/CLAUDE.md) - Server-side ENet host, ServerSessionBase, and ServerTypes
