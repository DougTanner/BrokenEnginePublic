<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Wire-Format Write/Read Pairing — Game Side

## Context

The engine paired-message scheme has landed as `Engine/Source/Network/NetworkMessages.h` (namespace `engine::NetworkMessages`): a generic `MessageWriter`/`MessageReader` pair, one `struct` per packet carrying a static `Visit(TVisitor&, TMessage&)` that drives both directions, and `Write<TMessage>` / `Read<TMessage>` free-function templates. Engine packets (`ClientAckStreamMessage`, `ClientSubscribeMessage`, `ClientDesyncReportMessage`, ...) already route send and parse through that single `Visit`, so a one-sided field edit fails to compile. The infrastructure is reachable game-side through the PCH (Engine aggregation → `NetworkProtocol.h` → `NetworkMessages.h`).

This plan applies that same scheme to the three game-layer messages, which are still hand-rolled: each layout is mirrored in a send-side comment and independently re-summed in a separate parse function.

- **`kServerAssignPlayer`** — sent by `ServerSession::SendAssignPlayer` (`ServerSession.cpp` ~`:399-409`) via `engine::gpServer->SendSimplePacket` with the layout in a comment (`"[1B type][8B global player ID][GridCoord]"`); parsed at 16 post-strip bytes in `ParsePlayerEvents` (`PlayerEvents.cpp` ~`:21-32`, `"8B playerId + 4B gridX + 4B gridY"`).
- **`kServerPlayerState`** — sent by `ServerSession::SendPlayerState` (~`:411-430`, `"[1B type][1B state][8B global player ID][4B coord.x][4B coord.y]"`); parsed at 17 post-strip bytes in `ParsePlayerEvents` (`PlayerEvents.cpp` ~`:33-62`).
- **`kServerFleetSync` envelope** — sent by `game::SendFleetSync` (`ServerFleetSerialization.cpp` ~`:12-43`) with the whole layout in one comment (~`:25`); parsed by `ParseFleetSyncPayload` (`PlayerEvents.cpp` ~`:70-121`) with hand-summed 36-byte-per-fleet and 9-byte-per-member constants.

### (b) Send-order / decode-order coupling

`PlayerStateWireType` decode is a `switch` in send order (`PlayerEvents.cpp` ~`:46-60`); the send-side log name table `kpStateNames` is order-locked by `static_assert(std::size(kpStateNames) == static_cast<size_t>(PlayerStateWireType::kCount))` (`ServerSession.cpp` ~`:419-426`). Per the hub convention "server send order and client decode order must move together", the pairing should make this **structural** — one definition drives both directions — rather than convention-enforced.

### (c) Spawn-request layer violation — split out

The spawn-request engine-surface work originally bundled here now lives in `Documents/Plans/Network/SpawnRequestSurfaceRemoval.md`, which deletes the dead `engine::PacketType::kClientSpawnRequest` surface outright. That is an incompatible wire change with a `kuiProtocolVersion` bump; this plan is byte-identical with no bump, so bundling would gate a harmless refactor behind protocol-version batching. The two plans touch disjoint regions and may land in either order.

### (d) Riders in `ParseFleetSyncPayload` — already implemented

Both riders originally planned here are **already present in the current code**; this step is verify-only, not reimplement:

- `iFlagshipIndex` is already validated at the parse trust boundary (`PlayerEvents.cpp:106-111`), not only downstream in consumers.
- `ParseFleetSyncPayload` already uses `operator[]` (`PlayerEvents.cpp:94`, `:112`, `:117`), not `.at()`, on indices it just `resize`d.

## Design

- **(a)/(b) — byte-identical conversion.** Define each game message once as a **payload-only** visitor `struct` in a new game colocation header (`GameMessages.h`, sibling of `GamePacketType.h`), reusing `engine::NetworkMessages::MessageWriter`/`MessageReader` and the `Write`/`Read` templates. The game-packet forwarding path prepends the type byte on send and strips it before parse (`Server.cpp:312`, `Client.cpp:281`), so game `Visit`s cover only the payload fields — no `Type()` field (unlike engine messages, which are parsed whole). Restructure `SendAssignPlayer` and `SendPlayerState` off `SendSimplePacket` to build the workbuffer via a pushed type byte plus `NetworkMessages::Write(...)`, then `NetworkManager::SendPacket`, mirroring the existing `SendFleetSync` build style. The `PlayerStateWireType` decode mapping and the `kpStateNames` log table both derive from one colocated wire-type descriptor table (one row per wire value, in order: `{PlayerStateWireType, PlayerEventType, name}`), retiring the "send order and decode order move together" convention. Assert written size against the current constants (16 / 17) during transition. No wire bytes change.
  - **Fleet-sync envelope**: express its layout once in `GameMessages.h` as a colocated paired writer/reader sharing named `constexpr` per-fleet (36) and per-member (9) size constants, with a written-size assert. Keep it a paired writer/reader rather than a single shared `Visit`, because its nested dynamic member vectors and its trust-boundary count validation (the divide-not-multiply overflow guard and per-fleet re-check) do not fit the fixed-capacity `MessageReader` model. The send site and `ParseFleetSyncPayload` both consume those shared constants so a one-sided layout edit changes the shared constant and trips the asserts.
- **(d)**: verify the two riders remain satisfied (above); make no edit unless a future drift reintroduces `.at()` or removes the flagship-index boundary check.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/GameMessages.h` (new) — single definition site for all three game message layouts plus the `PlayerStateWireType` descriptor table.
- `Projects/BrokenEngineSandbox/Source/Network/PlayerEvents.{h,cpp}` — client-side parse of all three messages; `PlayerStateWireType` enum of record.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `SendAssignPlayer` / `SendPlayerState` send sites.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetSerialization.cpp` — `SendFleetSync` only.
- `Engine/Source/Network/NetworkMessages.h` — **read-only**: the `MessageWriter`/`MessageReader` and `Write`/`Read` infrastructure the new game structs reuse. No engine message is added, edited, or deleted here.

## Scope contract

The listed scope is both target and ceiling. Make the smallest complete change that satisfies the acceptance criteria and invariants. Add no abstractions, configuration, extension points, or refactors; do not "clean up" adjacent code encountered in these files. Naming a file below grants permission only to touch the named functions/members/regions plus the mechanical necessities (includes, forward declarations, enum/case entries, vcxproj membership) the named change requires.

### In scope

**New file**

- `Projects/BrokenEngineSandbox/Source/Network/GameMessages.h` (new) — payload-only visitor structs for assign and player-state, the colocated `PlayerStateWireType` descriptor table, and the fleet-sync paired writer/reader with shared per-fleet/per-member size constants. Add to the game vcxproj + filters.

**(a)/(b) conversions**

- `Projects/BrokenEngineSandbox/Source/Network/PlayerEvents.cpp` — `ParsePlayerEvents` assign branch (~`:21-32`) and player-state branch (~`:33-62`): replace hand-rolled cursor reads with `NetworkMessages::Read` of the new payload-only structs; drive the wire-type→event mapping from the descriptor table.
- `Projects/BrokenEngineSandbox/Source/Network/PlayerEvents.h` — `PlayerStateWireType` (`:9-16`) stays the enum of record; move/derive the send-order-coupled name+event association into the colocated descriptor table (in `GameMessages.h`) that both sides consume. `ParsePlayerEvents`/`ParseFleetSync` declarations unchanged.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `SendAssignPlayer` (~`:399-409`) and `SendPlayerState` (~`:411-430`): build via type byte + `NetworkMessages::Write`; source the log name from the descriptor table (retiring the local `kpStateNames`/`static_assert`).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetSerialization.cpp` — `SendFleetSync` only (~`:12-43`): emit through the shared fleet-sync writer + size constants. `WriteFleet`/`ReadFleet`/`WriteFleetData`/`ReadFleetData` (save/replay path) are **out of scope**.

### Out of scope

- Engine message conversions (engine packet types below `kGamePacketStart`) — already landed in `NetworkMessages.h`; do not touch.
- The `StatusChange` batch codec (`Projects/.../Network/NetworkSerialization.cpp`) — separate wire concern; version-gate handling is tracked by `Documents/Plans/Network/StatusChangeWireVersionGate.md`. Not a paired-message target here.
- Fleet-sync **content**/semantics, `Fleet` fields, and the GUID envelope meaning — only its wire layout definition moves.
- Fleet save/replay serialization (`WriteFleet`/`ReadFleet`/`WriteFleetData`/`ReadFleetData`) — a different, file-backed format; unrelated to the network fleet-sync layout.
- The spawn-request engine surface — `engine::PacketType::kClientSpawnRequest`, `ClientRequestFlags`(`_t`), `PendingSpawnRequest`, `ClientSpawnRequestMessage`, `Client::SendSpawnRequest`, `Server::ClientSpawnRequest`/`mPendingSpawnRequests`, and `ServerClientManager::ProcessSpawnRequests` — is owned entirely by `Documents/Plans/Network/SpawnRequestSurfaceRemoval.md`. Touch none of it here.
- `Engine/Source/Network/NetworkProtocol.h` in full, including `kuiProtocolVersion` — this plan changes no wire bytes and bumps no version.
- `GamePacketType` enumerators and `GetGamePacketContract` — the three converted messages reuse their existing enumerators unchanged; no enumerator is added, removed, or reordered.
- The landed subscription-lifecycle wire contract — `kClientUnsubscribe` is already a 4-byte slot-plus-epoch packet at protocol version 7 (`ClientUnsubscribeMessage`, `NetworkMessages.h:474-489`); preserve it as documented in `Documents/Architecture/Network.md`.
- Rider (d) — already implemented; verify only.

## Risk tier and invariants

**Tier 3.** Trigger: serialization / data-layout surface — the three game message layouts each move to a single definition site. There is no wire/protocol trigger; the change is byte-identical. Invariants to hold:

- **Byte-identity**: the converted assign, player-state, and fleet-sync layouts must be byte-for-byte identical to the current output. Guard with per-message written-size asserts against the current constants (16 / 17 / 36 / 9) and interop smoke between conversions.
- **No version movement**: `kuiProtocolVersion` stays 7 and `GamePacketType` values and order are untouched. A conversion that forces either is a design failure, not a licensed bump.
- No CRC / determinism / `.pack` / `Frame::kiVersion` exposure — this is transport, not simulation state.

## Acceptance criteria

- Assign, player-state, and fleet-sync layouts are each defined once (in `GameMessages.h`); a one-sided field insertion fails to compile or trips a written-size assert rather than desyncing.
- `PlayerStateWireType` send-side name/log and client-side decode both derive from one descriptor table; the standalone `kpStateNames` table and its `static_assert` are gone.
- Rider (d) remains satisfied: `iFlagshipIndex` validated in `ParseFleetSyncPayload`; fleet-sync parse uses `operator[]`.
- Byte-identical wire output; interop verified after conversion for assign, player-state changed-frame, spawned, died, and fleet sync. `kuiProtocolVersion` is still 7 and `Documents/Architecture/Network.md` needs no edit.

## Notes

- **No wire change, no coordination.** Byte-identical output means no `kuiProtocolVersion` bump, so this plan sits outside the protocol-version batching that governs `Documents/Plans/Network/FleetRequestsByGuid.md`, `Documents/Plans/Network/StatusChangeWireVersionGate.md`, and `Documents/Plans/Network/SpawnRequestSurfaceRemoval.md`. It can land at any point relative to those.
- Highest risk is a non-identical conversion desyncing; mitigate with the per-message written-size asserts against the current constants and interop smoke between conversions.
