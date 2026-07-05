# `/Network/` - Game Networking Sessions

## Overview

Game-layer multiplayer orchestration. `ClientSession` and `ServerSession` extend their engine base classes and are conditionally compiled (`BT_CLIENT` / `BT_SERVER`); see the subdirectories. The top-level files are side-agnostic: packet identifiers (`game::`), player-event / fleet-sync parsing (`game::`), and the `StatusChange` wire codec (declared `engine::` because the engine transfer path calls it, even though the per-type payloads are game data). Engine-level networking conventions (channel math, send path, ACK model, ENet tuning) are documented in [Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md) — not restated here.

## Hub Conventions (children do not re-document these)

- **Packet-type extension**: `GamePacketType` extends `engine::PacketType` starting at `kGamePacketStart`. Engine forwards these as opaque bytes. Enumerator order is the wire protocol — append only.
- **Debug-control packets**: a block of one-way client→server requests (quicksave/quickload/reset, replay record + playback, pause, timescale) plus a server timescale broadcast; debug-only. The client→server requests are decoded server-side (see [Server/CLAUDE.md](Server/CLAUDE.md)); the timescale broadcast is decoded client-side (see [Client/CLAUDE.md](Client/CLAUDE.md)).
- **Type-byte stripping**: drained game packets arrive as (type, payload) pairs with the type byte already removed; payload size checks are post-strip.
- **Raw-packet ownership**: `ParsePlayerEvents` leaves `rRawPackets` untouched and appends into a workbuffer arena (no heap); `ParseFleetSync` erases consumed entries (even when malformed) and heap-allocates — a successfully parsed sync replaces the caller's fleet vector wholesale (last valid sync wins; a malformed sync never clobbers an earlier valid one). A valid zero-fleet sync is a real applied result that leaves the vector empty, so the caller gates on the parse outcome, not on vector emptiness.
- **Payload validation**: player-event parsing does per-branch size checks and skips short payloads (`continue`; unknown wire values `DEBUG_BREAK()` then skip); fleet-sync parsing validates via `engine::BoundedCursor` — a malformed payload is rejected whole (no partial application) and logged at `kNetwork`/`kWarning`.
- **Wire-order contract**: enum-to-wire mappings (e.g., `PlayerStateWireType`) are send-order-sensitive; server send order and client decode order must move together. `PlayerStateWireType` carries a trailing non-wire `kCount` sentinel that sizes its server-side name table's length-guard `static_assert` — append new wire values above it. `PlayerEventType::kAssigned` exists only locally (synthesized from the assign packet), never on the wire.

## StatusChange Batch Format

- Indices sorted by `StatusChangeType` (counting sort via workbuffer scratch) for deterministic byte output; each group prefixed with a type byte + uint16 count.
- LZ4 envelope: int32 uncompressed-size prefix + compressed data; serialize/decompress scratch borrowed via the workbuffer (decompress scratch exact-sized to the clamped uncompressed size — no slack). The prefix is a trust boundary: clamped against `kiMaxSerializedStatusChangeBatchBytes` before it sizes the decompress reservation (rejects a hostile prefix that would otherwise over-allocate).
- Deserialization is bounds-checked up front: a `BoundedCursor` gates every item read against the per-type `StatusChangeItemWireSize` table; any shortfall or out-of-range type byte rejects the whole batch (returns 0, logged `kNetwork`/`kWarning`) — no partial application, the client resyncs via CRC.
- `kiMaxStatusChangeBytesPerItem` (engine-side `NetworkSerialization.h`) bounds every serialized item: a runtime `ASSERT` in `SerializeGroup` checks each item after writing, covering all `StatusChangeType`s — bump the constant when any payload grows past it (the ASSERT fires on violation).
- Client-only fields (e.g., missile smoke-trail id) are still written/read on server to preserve identical wire size; server discards on read via `BT_CLIENT` gating.
- LZ4 decompression failure logs at `kNetwork`/`kWarning` and returns 0; compression failure logs at `kNetwork`/`kError` and returns 0 (dropped, not shipped as a bare prefix the client would decode as zero changes).

## Adding a StatusChangeType

Update both switches (`SerializeGroup` / `DeserializeStatusChangeBatch`), the co-located `StatusChangeItemWireSize` table (bounds reads — must exactly match the case's write widths), and `DefaultDataForType`; re-check `kiMaxStatusChangeBytesPerItem` covers the new payload (the `SerializeGroup` ASSERT catches an overrun at serialize time).

## Subdirectories

- [Client/CLAUDE.md](Client/CLAUDE.md) - `ClientSession` + reconciliation pipeline
- [Server/CLAUDE.md](Server/CLAUDE.md) - `ServerSession` + fleet/transfer/broadcast/client managers

## See Also

- [Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md) - Engine hub (shared patterns)
- [Network Architecture](../../../../Documents/Architecture/Network.md)
- [Game Reconciliation](../../../../Documents/Architecture/GameReconciliation.md)
