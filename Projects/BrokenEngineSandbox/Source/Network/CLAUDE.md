# `/Network/` - Game Networking Sessions

## Overview

Game-layer multiplayer orchestration. `ClientSession` and `ServerSession` extend their engine base classes and are conditionally compiled (`BT_CLIENT` / `BT_SERVER`); see the subdirectories. The top-level files are side-agnostic: packet identifiers (`game::`), player-event / fleet-sync parsing (`game::`), and the `StatusChange` wire codec (declared `engine::` because the engine transfer path calls it, even though the per-type payloads are game data). Engine-level networking conventions (channel math, send path, ACK model, ENet tuning) are documented in [Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md) — not restated here.

## Hub Conventions (children do not re-document these)

- **Packet-type extension**: `GamePacketType` extends `engine::PacketType` starting at `kGamePacketStart`. Engine forwards these as opaque bytes. Enumerator order is the wire protocol — append only.
- **Debug-control packets**: a block of one-way client→server requests (quicksave/quickload/reset, replay record + playback, pause, timescale) plus a server timescale broadcast; debug-only, decoded server-side (see [Server/CLAUDE.md](Server/CLAUDE.md)).
- **Type-byte stripping**: drained game packets arrive as (type, payload) pairs with the type byte already removed; payload size checks are post-strip.
- **Raw-packet ownership**: `ParsePlayerEvents` leaves `rRawPackets` untouched and appends into a workbuffer arena (no heap); `ParseFleetSync` erases consumed entries and heap-resizes the caller's fleet vector (last sync wins).
- **Payload validation asymmetry**: player-event parsing skips short payloads (`continue`; unknown wire values `DEBUG_BREAK()` then skip); fleet-sync parsing trusts wire-supplied counts with no size validation.
- **Wire-order contract**: enum-to-wire mappings (e.g., `PlayerStateWireType`) are send-order-sensitive; server send order and client decode order must move together. `PlayerEventType::kAssigned` exists only locally (synthesized from the assign packet), never on the wire.

## StatusChange Batch Format

- Indices sorted by `StatusChangeType` (counting sort via workbuffer scratch) for deterministic byte output; each group prefixed with a type byte + uint16 count.
- LZ4 envelope: int32 uncompressed-size prefix + compressed data; serialize/decompress scratch borrowed via the workbuffer.
- Deserialization detects truncation after reading (post-group cursor check, logged at `kVerbose`) — the source buffer needs slack past its logical end; the int64-chunk-rounded workbuffer scratch provides it.
- `kiMaxBytesPerItem` is `static_assert`ed against the largest serialization (currently `kTransferPlayer`) — bump it when any transfer payload grows past it.
- Client-only fields (e.g., missile smoke-trail id) are still written/read on server to preserve identical wire size; server discards on read via `BT_CLIENT` gating.
- LZ4 decompression failure logs at `kNetwork`/`kWarning` and returns 0; compression result is unchecked.

## Adding a StatusChangeType

Update both switches in `SerializeGroup` and `DeserializeStatusChangeBatch`, extend `DefaultDataForType`, and re-check `kiMaxBytesPerItem` covers the new payload.

## Subdirectories

- [Client/CLAUDE.md](Client/CLAUDE.md) - `ClientSession` + reconciliation pipeline
- [Server/CLAUDE.md](Server/CLAUDE.md) - `ServerSession` + fleet/transfer/broadcast/client managers

## See Also

- [Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md) - Engine hub (shared patterns)
- [Network Architecture](../../../../Documents/Architecture/Network.md)
- [Game Reconciliation](../../../../Documents/Architecture/GameReconciliation.md)
