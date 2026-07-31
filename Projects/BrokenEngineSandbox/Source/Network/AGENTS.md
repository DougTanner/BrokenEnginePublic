# Game Network - Sessions and Game Wire Payloads

Game-layer packet extensions, status-change serialization, and multiplayer orchestration. Engine transport, channels, ACK state, cursors, and hostile-input enforcement are owned by `../../../../Engine/Source/Network/AGENTS.md`.

## Game Packet Contracts

- `GamePacketType` starts at `engine::PacketType::kGamePacketStart`; enumerator order is wire order and new values append. Every client-to-server type needs a game contract row and follows the engine validation and rate-limit checklist.
- Debug-control requests are contract-gated by `kbDebugInput`; a non-debug server treats them as contract violations.
- Drained game packets arrive with their type byte removed. Fixed payloads require exact post-strip sizes; variable payloads use `BoundedCursor` and reject malformed input without partial application.
- Player events append into workbuffer-backed output without consuming raw packets. Fleet synchronization consumes matching raw packets and replaces the destination only after a complete valid decode; a valid empty fleet is still an applied result.

## Status-Change Wire Format

- `StatusChangeType` is declared with Frame status data, but its append-only wire compatibility contract is owned here. Adding a type requires matching serialization, deserialization, wire-size, and default-data handling; keep the per-item maximum large enough for every payload.
- Any incompatible `StatusChange` or `TransferData` layout change — type tag, field order, field width, variant arm membership, or per-item wire size — requires the game codec author to increment `engine::kuiProtocolVersion`; Engine Network owns the shared Hello rejection gate.
- The protocol gate is distinct from `FrameInput::kiVersion` (replay-input compatibility) and `Frame::kiVersion` (deterministic-Frame/save/replay compatibility); neither Frame version substitutes for the protocol bump.
- Serialization groups changes deterministically by type, then wraps the batch in an LZ4 envelope. The uncompressed-size prefix is a trust boundary and must be clamped before reserving scratch memory.
- Deserialization bounds every item through the co-located wire-size contract and rejects the entire batch on an invalid type, short read, or decompression failure. Client-only values still occupy identical server-side wire space.
- `GameMessages` owns the field order and byte size of game payloads. Each message struct carries one `Visit` descriptor that both the writing and the reading side route through, plus a `static_assert`ed `kiSize`, and the wire-sensitive event enums sit in the same descriptor table. Add or change a game payload there rather than hand-writing a write on one side and a matching read on the other, which is how the two sides drift apart. Local-only synthesized states never enter the stream.

## Layering

- Top-level code owns packet identifiers, shared game payload codecs, and side-agnostic parsing.
- `Client/AGENTS.md` owns reconciliation and client game-session behavior.
- `Server/AGENTS.md` owns fleets, transfers, client management, and broadcast orchestration.
- Detailed rollback and packet flow belong in `../../../../Documents/Architecture/GameReconciliation.md` and `../../../../Documents/Architecture/Network.md`.
