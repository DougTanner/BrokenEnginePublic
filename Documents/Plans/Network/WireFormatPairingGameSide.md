# Wire-Format Write/Read Pairing — Game Side

## Context

Game-layer extension of `Architecture_WireFormatPairing.md` (engine plan). **Depends on that plan landing first** — this plan reuses whatever paired write/read scheme (paired free functions vs layout structs with `Write(cursor)`/`Read(cursor)`) and colocation home that plan establishes, and applies it to the game messages. Read the engine plan before executing.

The same lockstep-edit hazard the engine plan fixes for engine messages exists for game messages: every game message layout is hand-rolled cursor code with the byte count mirrored in a comment on the send side and re-summed on the parse side:

- **`kServerAssignPlayer`** — parsed at 16 bytes (`PlayerEvents.cpp:19-30`, "8B playerId + 4B gridX + 4B gridY"); sent via `SendSimplePacket` with the layout in a comment (`ServerSession.cpp:365`, "[1B type][8B global player ID][GridCoord]").
- **`kServerPlayerState`** — parsed at 17 bytes (`PlayerEvents.cpp:31-60`, "1B wireType + 8B id + 4B gridX + 4B gridY"); sent at `ServerSession.cpp:386` ("[1B type][1B state][8B global player ID][4B coord.x][4B coord.y]").
- **`kServerFleetSync` envelope** — parsed by `ParseFleetSyncPayload` (`PlayerEvents.cpp:68-112`, per-fleet 36-byte + per-member 9-byte hand-summed constants); sent by `game::SendFleetSync` (`ServerFleetSerialization.cpp:25-42`) with the whole layout mirrored in one comment (`:25`).

### (b) Send-order/decode-order coupling

`PlayerStateWireType` decode is a `switch` in send-order (`PlayerEvents.cpp:44-58`); the server name table is order-locked by a `static_assert(std::size(kpStateNames) == PlayerStateWireType::kCount)` (`ServerSession.cpp:383`). Per the hub convention "server send order and client decode order must move together" — the pairing scheme should make this **structural** (one definition drives both directions) rather than convention-enforced.

### (c) Spawn-request lives in the engine protocol but is game semantics (real layer violation)

`kClientSpawnRequest` (`NetworkProtocol.h:13`), `ClientRequestFlags::kSpawnRequested`/`kRespawnRequested` (`:30-34`), and `PendingSpawnRequest` (`ServerTypes.h:29-33`) encode spawn/respawn semantics that live entirely in the game layer — the engine only queues the request and forwards it. Per the repo's own rule (engine *types* naming game concepts = the real-violation direction), this belongs in the game packet space (`>= kGamePacketStart`). Surface:
- Engine send: `Client::SendSpawnRequest` (`ClientSend.cpp:52-64`), `Client.h:100`.
- Engine receive/dispatch: `Server::ClientSpawnRequest` (`ServerReceive.cpp:109-133`), `Server.cpp:184-185` dispatch, `Server.h:182,205,225` (`DrainPendingSpawnRequests`, `mPendingSpawnRequests`), `Server.cpp:70` clear.
- Engine types: `PendingSpawnRequest` + `ClientRequestFlags(_t)` (`ServerTypes.h`, `NetworkProtocol.h`), `PacketTypeName` entry (`NetworkProtocol.h:45`).
- Game consumer: `ServerClientManager.cpp:29-38` drains `PendingSpawnRequests` and reads the flags.

Move to a new `GamePacketType` enumerator (append-only, per hub) + a game-side pending-request queue drained from `ReceivedGamePacket`s, so the engine no longer names spawn/respawn. **This is a wire change** (retires an engine packet type, adds a game one).

### (d) Small riders in `PlayerEvents.cpp`

- Validate `iFlagshipIndex` at the parse boundary (`:96`) — consumers currently guard downstream (`FleetSelection.cpp:152-154`, `iFlagshipIndex >= 0 && < ssize(members)`); the invariant belongs at the trust boundary where the wire value enters.
- `ParseFleetSyncPayload` uses `.at()` on indices it just `resize`d (`:92` after `:84`; `:108` after `:103`) — defensive validation between our own lines; use `operator[]`.

## Design

- **(a)/(b) — byte-identical conversion**: express assign, player-state, and the fleet-sync envelope in the engine plan's paired scheme, colocated so a one-sided layout edit cannot compile (or trips a written-size assert). `PlayerStateWireType`'s decode switch and the server name table derive from the one paired definition, retiring the "send order and decode order move together" convention. Convert message-by-message; assert written sizes against the existing magic numbers (16 / 17 / 36 / 9) during transition. No wire bytes change.
- **(c) — wire change**: relocate the spawn/respawn request to `GamePacketType` + a game pending-request struct. Delete `kClientSpawnRequest`, `ClientRequestFlags(_t)`, `PendingSpawnRequest`, and the engine send/receive/drain path; wire the game client send + game server drain through the existing opaque-game-packet forwarding (`ReceivedGamePacket`). If this change lands independently, bump `kuiProtocolVersion` beyond the current version 6; only incompatible wire changes atomically co-landed in the same client/server release may share that one new version (see Notes).
- **(d)**: fold the two riders into the same file while it is open.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/PlayerEvents.cpp` — `ParsePlayerEvents` assign/player-state parse (`:19-60`), `ParseFleetSyncPayload` (`:68-112`), riders (`:92/103/108`, `:96`).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetSerialization.cpp` — `SendFleetSync` (`:12-43`).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `SendAssignPlayer` (`:357-367`), `SendPlayerState` (`:369-386`).
- `Projects/BrokenEngineSandbox/Source/Network/GamePacketType.h` — new spawn-request enumerator (append-only).
- Engine (spawn-request removal): `NetworkProtocol.h` (`kClientSpawnRequest`, `ClientRequestFlags`, `PacketTypeName`, `kuiProtocolVersion`), `Server/ServerTypes.h` (`PendingSpawnRequest`), `Client/ClientSend.cpp` + `Client/Client.h`, `Server/ServerReceive.cpp` + `Server/Server.{h,cpp}`.
- Game spawn-request consumer: `Projects/.../Network/Server/ServerClientManager.cpp:29-38` (drain source moves from `DrainPendingSpawnRequests` to the game queue).
- Wherever the engine plan colocates paired defs (`NetworkProtocol.h` or a new `NetworkMessages.h`) — the game messages need an analogous game-layer home; new files go in the game vcxproj + filters.

## Out of scope

- Engine message conversions (a/b for engine packet types) — owned by `Architecture_WireFormatPairing.md`.
- The `StatusChange` batch codec (`NetworkSerialization.cpp`) — its own hardening plan (`StatusChangeCodecHardening.md`); not a paired-message target here.
- Fleet-sync **content**/semantics, `Fleet` fields, and the GUID envelope meaning — only its wire layout definition moves.
- Any behavior change to spawn/respawn *policy* — (c) relocates the transport only; `ServerClientManager`'s spawn handling is unchanged apart from the drain source.
- Subscription-lifecycle wire fields (`SubscriptionLifecycleRaceHardening.md` may add an unsubscribe epoch) — coordinate the version bump, don't implement here.

## Acceptance criteria

- Assign, player-state, and fleet-sync layouts are each defined once; a one-sided field insertion fails to compile or trips a written-size assert rather than desyncing.
- `PlayerStateWireType` send and decode derive from one definition.
- No `engine::` symbol names spawn/respawn; `kClientSpawnRequest`/`ClientRequestFlags`/`PendingSpawnRequest` are gone; spawn/respawn requests travel as a `GamePacketType`.
- `iFlagshipIndex` is validated in `ParseFleetSyncPayload`; the fleet-sync parse uses `operator[]`.
- (a)/(b) produce byte-identical wire output; interop verified (assign, spawn, changed-frame, died, fleet sync) after conversion.

## Coordination

- Protocol/version batch with `Documents/Plans/Network/SubscriptionLifecycleRaceHardening.md` and `Documents/Plans/Network/FleetRequestsByGuid.md`: co-landed wire breaks may share one new `kuiProtocolVersion` bump; otherwise each incompatible release bumps the current version again. The pack-integrity handshake already consumed version 6 independently.
- `Documents/Plans/Network/Architecture_WireFormatPairing.md`: never interleave send/receive-site restructuring with this wire change. WireFormatPairingGameSide's structured dependency requires the architecture plan first.

## Notes

- **Invariant exposure — two distinct exposures, stated explicitly:**
  - (a)/(b): **no wire change** — byte-identical, no `kuiProtocolVersion` bump. Highest risk is a non-identical conversion desyncing; mitigate with per-message written-size asserts against the current constants and interop smoke between conversions.
  - (c): **wire change** — retires `kClientSpawnRequest`, adds a `GamePacketType`. Requires a `kuiProtocolVersion` bump beyond the current version 6 unless co-landed with another incompatible wire change behind one new version. Coordinate with `SubscriptionLifecycleRaceHardening.md` and `FleetRequestsByGuid.md`. `GamePacketType` order is the wire protocol (append-only); appending the spawn-request enumerator keeps existing game packets stable, but the removed engine type shifts nothing after `kGamePacketStart` since engine and game enums are separate spaces.
- No CRC/determinism/`.pack`/`kiVersion` (`Frame::kiVersion`) exposure — this is transport, not sim state.
- Version ownership: (c) bumps `kuiProtocolVersion` beyond the current version 6 when it lands independently; only an atomic co-land with another incompatible wire change shares one new version.
