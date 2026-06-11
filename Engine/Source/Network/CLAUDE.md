# `/Engine/Source/Network/` - Networking

## Overview

Client/server networking over ENet reliable UDP with slot-based coord subscriptions, LZ4-compressed binary serialization, and LAN discovery. Full protocol flow (reconciliation, ACK/resend, clock correction, epoch guard) lives in [Documents/Architecture/Network.md](../../../Documents/Architecture/Network.md) — do not duplicate here or in leaves.

## Hub Conventions (children do not re-document these)

- **Channel math**: channel 0 reliable control, channel 1 reserved unreliable, channels `2 + slot*2` (reliable) / `2 + slot*2 + 1` (unreliable) per coord slot. Always use `NetworkManager::CoordSlot*` / `ChannelToSlot` / `IsCoordChannel` / `IsUnreliableChannel` — never hardcode.
- **Send path**: all sends (engine and game) go through `NetworkManager::SendPacket`; it wraps ENet's internal alloc with `ScopedSuppressAllocationTracking`. For simple fixed-payload packets (type byte + arithmetic / `GridCoord` args), use the `SendSimplePacket` member template on `Client` / `Server` (see children) rather than recreating the workbuffer-push boilerplate; its per-arg serializer is the `PushSimplePacketArg` dispatcher in `NetworkCursor.h` (arithmetic and `GridCoord` only — unwrap enums/ids/flags at the call site).
- **Serialization helpers**: `NetworkCursor.h` holds the shared cursor read/write primitives used by every `Network*.cpp`; they do no bounds checking, so callers own buffer sizing. It is intentionally not aggregated into `Engine.h` — include it directly where needed.
- **Slot ACK model**: independent `AckState` per slot (int64 floor + 128-bit bitfield + uint16 epoch). Epoch mismatch silently drops stale packets; makes slot reuse across rapid (un)subscribe cycles safe.
- **Game-layer opacity**: packet types `>= kGamePacketStart` are forwarded as raw bytes; engine never interprets them.
- **ENet tuning** (both sides): peer throttle disabled so reconciliation stalls don't drop unreliable traffic; 1 MB socket send/recv buffers.
- **Drain-per-poll**: `Poll()` on both sides clears all pending buffers at entry; game layer must consume within the tick or data is lost.
- **Endianness**: cursor helpers `memcpy` directly — x64 little-endian only, no byte swap.
- **Tick-rate independence**: `kiNetworkBufferSize=128` and `kiJitterSafetyUs` do not scale with physics rate; the jitter safety margin is fixed wall-clock time.

## Key Classes

- **NetworkManager** (`gpNetworkManager`) - Thin singleton: ENet init/deinit plus channel math and `SendPacket` helper. No runtime state.
- **NetworkProtocol** - Wire protocol header (inline constexpr): packet types, spawn/respawn request flags, protocol/discovery/timing constants, `ClientGuid`, `AckState`.
- **NetworkSimulation** - Compile-time latency/loss injection with regional presets; toggle is `keNetworkSimulation` in the game's `Pch.h` (recompile to change level). Zero overhead when disabled via `if constexpr`. One-way delay per direction; the delayed-packet queue is the only heap user (suppressed). Bursty loss model: a drop makes further consecutive drops on that channel more likely up to a cap. Per-region `NetworkSimulationBounds` supply the CRC/replay-depth tolerances the game profiler's Network screen validates reconciliation metrics against. Gotcha: its drop log parses the tick from a fixed byte offset mirroring the coord-packet header layout (defined in Client/Server send code) — log-only, but it misreports if that layout changes.
- **NetworkDiscoveryResponder** (`BT_SERVER`) / **NetworkDiscoveryScanner** (`BT_CLIENT`) - LAN discovery split into platform-gated halves sharing wire format. Raw Winsock UDP (not ENet), non-blocking, port `kuiDefaultPort+1`, 4-byte magic `"BRKN"`. Scanner pings loopback before broadcasting so a local server wins the race; the reply is the bare magic with no payload, so the client connects to the responder's address on the default game port.
- **NetworkSerialization** - `StatusChange` batch (de)serializer declared here, implemented in the game layer (wire format is game-specific). Compressed form is `uint32` uncompressed-size prefix + LZ4 payload.

## Architecture Notes

- **GUID identity**: clients persist server-assigned `ClientGuid` to disk so player state survives reconnect.
- **Subscription lifecycle**: `kUnsubscribed -> kSubscribing -> kWaitingFullState -> kActive -> kUnsubscribing`. Full state and subscribe-accept can arrive in either order across channels; both sides reconcile.

## See Also

- [Client/CLAUDE.md](Client/CLAUDE.md) - Client peer and `ClientSessionBase`
- [Server/CLAUDE.md](Server/CLAUDE.md) - Server host and `ServerSessionBase`
- [Network.md](../../../Documents/Architecture/Network.md) - Protocol flow, ACK/resend, clock correction
- [Game Reconciliation](../../../Documents/Architecture/GameReconciliation.md)
</content>
</invoke>