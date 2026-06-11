# Architecture: Wire-Format Write/Read Pairing

## Context

Source: /external-architecture-review on `Engine/Source/Network` (recursive). Every message layout exists at least twice as hand-rolled cursor code with mirrored magic numbers in comments — the directory's principal lockstep-edit hazard and biggest latent desync source. Send/receive layouts were verified byte-for-byte symmetric for all seventeen engine message types *today*; nothing structural keeps them that way. A third copy of the coord-update layout hides as a magic byte-offset in `NetworkSimulation.h`.

## Design

### Colocate each message's writer and reader
- Header sizes 20/16/32/12/17/2 are hand-mirrored between `ClientReceive.cpp:141,220,273,444,354,515` and the writers (`ServerSend.cpp:34-42,66-72,104-119,99-101`; `ServerReceive.cpp:212-218`); the ACK layout `2+27n+8` between `ClientSend.cpp:22-49` and `ServerReceive.cpp:24-99`; the hello between `ClientSend.cpp:190-196` and `ServerReceive.cpp:225-296`. Introduce per-message paired write/read functions (or layout structs with `Write(cursor)`/`Read(cursor)`) colocated in one place per message — e.g. a `NetworkMessages.h` beside `NetworkProtocol.h` — so a one-sided layout edit becomes structurally impossible. Wire bytes must remain identical; convert message-by-message, diffing capture or asserting written sizes against the existing magic numbers during transition. [~1h per message family; ~6h total]
- Dedupe the two near-verbatim `SendSimplePacket` templates (`Client.h:80-97`, `Server.h:129-141`) into one shared helper (e.g. on `NetworkManager`), and give "simple" packets a read-side counterpart so even they are symmetric in mechanism. [~30m]

### Engine/Source/Network/NetworkSimulation.h
- `NetworkSimulation.h:142-146` parses the tick from coord-update packets at a magic byte offset 4 (log-only; acknowledged in `Network/CLAUDE.md:23`). Replace with the shared layout accessor once the coord-update message is converted. [~10m]

## Critical files

- `Engine/Source/Network/Client/ClientSend.cpp`, `ClientReceive.cpp`, `Client/Client.h`
- `Engine/Source/Network/Server/ServerSend.cpp`, `ServerReceive.cpp`, `Server/Server.h`
- `Engine/Source/Network/NetworkProtocol.h` (or new `NetworkMessages.h` — new file: `Engine.h` + both vcxprojs)
- `Engine/Source/Network/NetworkSimulation.h`

## Out of scope

- Any wire-format *change* — the deliverable is byte-identical bytes produced/consumed from a single definition per message.
- The game-layer packet payloads past `kGamePacketStart` (engine treats them as opaque — verified true opacity; stays that way).
- `NetworkCursor.h` itself (the primitive layer is sound and shared).
- The serialization asymmetry policy for `NetworkSerialization.h`'s game-implemented functions (declare-engine/define-game split is clean as-is).

## Acceptance criteria

- Each engine message type's layout is defined exactly once; `NetworkSimulation.h` no longer hard-codes a byte offset; a deliberate one-sided field insertion no longer compiles (or fails a written-size assert) rather than desyncing at runtime.
- Client/server interop verified after conversion (connect, subscribe, play, resync) with no protocol version bump needed.

## Notes

- **Invariant exposure**: network protocol — highest blast radius in the directory if a conversion is not byte-identical (Risks 3). Mitigate by converting one message family per session and smoke-testing interop between conversions.
- Pre-staged grill decision: paired free functions vs layout structs; and whether converted messages live in `NetworkProtocol.h` or a new `NetworkMessages.h`.

## Verification Notes

Verified against source (2026-06-10); one count corrected:
- Hand-mirrored sizes confirmed: reader checks 20/16/32/12/17/2 at `ClientReceive.cpp:142/:221/:274/:445/:355/:516` (comments one line above, as cited); writers at `ServerSend.cpp:34-42` (full state), `:66-72` (static data), `:104-119` (`WriteBufferedFramePacket`, update/resend), `:99-101` (subscribe accept); debug-frame writer `ServerReceive.cpp:212-218`. ACK layout `2 + 27n + 8` mirrored between `ClientSend.cpp:22-49` and `ServerReceive.cpp:24-99` (expected-size formula at `:27`). Hello mirrored between `ClientSend.cpp:190-196` and `ServerReceive.cpp:225-296`.
- `SendSimplePacket` duplication confirmed (`Client.h:80-97`, `Server.h:129-141`) — near-verbatim; only deltas are Client's `CanSend()` gate and member `mpServerPeer` vs Server's `pPeer` parameter, so a shared helper takes the peer and the client keeps its gate at the call site.
- `NetworkSimulation.h` magic offset confirmed: `memcpy(&iTick, rEvent.packet->data + 4, sizeof(iTick))` at `:145` (cited block `:142-146`, log-only; acknowledged in `Network/CLAUDE.md`).
- Corrected: "fifteen" → seventeen — `PacketType` has 17 real engine message enumerators (`NetworkProtocol.h:7-27`, excluding the `kGamePacketStart` sentinel).
- Colocated write/read pairing is feasible with zero wire change — all writers/readers are field-by-field cursor calls with no struct memcpy, so a per-message paired definition reproduces the bytes mechanically.
