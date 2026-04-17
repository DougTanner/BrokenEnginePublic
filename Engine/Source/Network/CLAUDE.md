# `/Engine/Source/Network/` - Networking

## Overview

Client/server networking over ENet reliable UDP with slot-based coord subscriptions, LZ4-compressed binary serialization, and LAN discovery. Full protocol flow (reconciliation, ACK/resend, clock correction, epoch guard) lives in [Documents/Architecture/Network.md](../../../Documents/Architecture/Network.md) — do not duplicate here or in leaves.

## Hub Conventions (children do not re-document these)

- **Channel math**: channel 0 reliable control, channel 1 reserved unreliable, channels `2 + slot*2` (reliable) / `2 + slot*2 + 1` (unreliable) per coord slot. Always use `NetworkManager::CoordSlot*` / `ChannelToSlot` / `IsCoordChannel` / `IsUnreliableChannel` — never hardcode.
- **Send path**: all sends (engine and game) go through `NetworkManager::SendPacket`; it wraps ENet's internal alloc with `ScopedSuppressAllocationTracking`.
- **Slot ACK model**: independent `AckState` per slot (int64 floor + 128-bit bitfield + uint16 epoch). Epoch mismatch silently drops stale packets; makes slot reuse across rapid (un)subscribe cycles safe.
- **Game-layer opacity**: packet types `>= kGamePacketStart` are forwarded as raw bytes; engine never interprets them.
- **ENet tuning** (both sides): peer throttle disabled so reconciliation stalls don't drop unreliable traffic; 1 MB socket send/recv buffers.
- **Drain-per-poll**: `Poll()` on both sides clears all pending buffers at entry; game layer must consume within the tick or data is lost.
- **Endianness**: `NetworkCursor` helpers `memcpy` directly — x64 little-endian only, no byte swap.
- **Tick-rate independence**: `kiNetworkBufferSize=128` and `kiJitterSafetyUs` are wall-clock; they do not scale with physics rate.

## Key Classes

- **NetworkManager** (`gpNetworkManager`) - Thin singleton: ENet init/deinit plus channel math and `SendPacket` helper. No runtime state.
- **NetworkProtocol** - Wire protocol header (inline constexpr): packet types, protocol/discovery constants, `ClientGuid`, `AckState`.
- **NetworkSimulation** - Compile-time latency/loss injection with regional presets. Zero overhead when disabled via `if constexpr`. One-way delay per direction; delayed-packet queue is the only heap user (suppressed).
- **ClientSessionBase** / **ServerSessionBase** - Engine-generic session bases inherited by game-layer sessions.
- **NetworkDiscovery** - LAN responder/scanner. Raw Winsock UDP (not ENet), non-blocking, port `kuiDefaultPort+1`, 4-byte magic `"BRKN"`. Scanner pings loopback before broadcasting so a local server wins the race.
- **NetworkSerialization** - `StatusChange` batch (de)serializer; compressed form is `uint32` uncompressed-size prefix + LZ4 payload. Implementation lives in game layer.

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