# `/Network/` - Game Networking Sessions

## Overview

Game-layer multiplayer orchestration. `ClientSession` and `ServerSession` extend their engine base classes and are conditionally compiled (`BT_CLIENT` / `BT_SERVER`), owned by `Game` via `unique_ptr`. The top-level files here are side-agnostic: packet identifiers (`game::`), player-event / fleet-sync parsing (`game::`), and the `StatusChange` wire codec (declared `engine::` because the engine transfer path calls it, even though the per-type payloads are game data). Engine-level networking conventions (channel math, send path, ACK model, ENet tuning) are documented in [Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md) — not restated here.

## Hub Conventions (children do not re-document these)

- **Packet-type extension**: `GamePacketType` extends `engine::PacketType` starting at `kGamePacketStart`. Engine forwards these as opaque bytes.
- **Debug-control packets**: A block of one-way client→server requests (quicksave/quickload/reset, replay record + playback, pause, timescale) plus a server timescale broadcast; these are debug-only and decoded in `ServerSession::ParseReceivedGamePackets`.
- **Parse dispatch**: Parsers strip the packet-type byte before decoding payload. Short/truncated payloads are silently skipped (`continue`) — never thrown.
- **Raw-packet ownership**: `ParsePlayerEvents` leaves `rRawPackets` untouched; `ParseFleetSync` **erases** consumed entries. Callers must tolerate mutation by fleet sync.
- **Wire-order contract**: Enum-to-wire mappings (e.g., `PlayerStateWireType`) are send-order-sensitive; server send order and client decode order must move together.

## StatusChange Batch Format

- Indices sorted by `StatusChangeType` (counting sort via workbuffer scratch); each group prefixed with a type byte + uint16 count.
- LZ4 envelope wraps an int32 uncompressed-size prefix; serialize/decompress scratch borrowed via `Workbuffer::Push`/`Pop`.
- `kiMaxBytesPerItem` is `static_assert`ed against the largest serialization (currently `kTransferPlayer`) — bump it when any transfer payload grows past it.
- Client-only fields (e.g., missile smoke-trail id) are still written/read on server to preserve identical wire size; server discards on read via `BT_CLIENT` gating.
- LZ4 failures log at `kNetwork`/`kWarning`; mid-group truncation logs at `kVerbose`.

## Adding a StatusChangeType

Update both switches in `SerializeGroup` and `DeserializeStatusChangeBatch`, extend `DefaultDataForType`, and re-check `kiMaxBytesPerItem` covers the new payload.

## Subdirectories

- [Client/CLAUDE.md](Client/CLAUDE.md) - `ClientSession` + reconciliation pipeline
- [Server/CLAUDE.md](Server/CLAUDE.md) - `ServerSession` + fleet/transfer/broadcast/client managers

## See Also

- [Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md) - Engine hub (shared patterns)
- [Network Architecture](../../../../Documents/Architecture/Network.md)
- [Game Reconciliation](../../../../Documents/Architecture/GameReconciliation.md)
