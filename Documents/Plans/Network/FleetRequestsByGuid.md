<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Fleet Requests by FleetGuid

## Context

The client addresses fleets in four request packets by **mutable vector index** (`iFleetIndex` into the server's `mFleets[guid]` vector), even though `FleetGuid` exists precisely as the stable client-facing identifier. `Fleet.h` documents it: the guid "survives disconnect/reconnect, save/load, and full client restart", and `Fleet::guid` is already synced to clients in every fleet-sync packet. Addressing by index contradicts the identifier's own design.

The four index-keyed request paths (client send → server decode → server action):

| Packet (`GamePacketType`) | Client send site (`ClientSession.cpp`) | Server decode (`ServerSession::ParseReceivedGamePackets`, `ServerSession.cpp`) | Server action (`ServerFleetManager.cpp`) |
|---|---|---|---|
| `kClientDeleteFleetRequest` | `SendDeleteFleetRequest(iFleetIndex)` ~line 350 | ~line 138 → `QueueDeleteRequest({iClientId, iFleetIndex})` | `ProcessDeleteFleetRequests` ~line 65 |
| `kClientSpawnIntoFleetRequest` | `SendSpawnIntoFleetRequest(iFleetIndex)` ~line 358 | ~line 149 → `QueueSpawnIntoRequest({iClientId, iFleetIndex})` | `ProcessSpawnIntoFleetRequests` ~line 94 |
| `kClientRespawnInFleetRequest` | `SendRespawnInFleetRequest(iFleetIndex, iMemberIndex)` ~line 366 | ~line 161 → `QueueRespawnRequest({iClientId, iFleetIndex, iMemberIndex})` | `ProcessRespawnInFleetRequests` ~line 119 |
| `kClientFleetNavigationDelay` | `SendFleetNavigationDelayRequest(iFleetIndex, fDelay)` ~line 374 | ~line 174 → `UpdateFleetNavigationDelay(pClient->clientGuid, iFleetIndex, fDelay)` | `UpdateFleetNavigationDelay` ~line 493 (immediate) |

The server drains queues in fixed order after each network poll: `ProcessDeleteFleetRequests` **before** `ProcessSpawnIntoFleetRequests` / `ProcessRespawnInFleetRequests` (`ServerSession::AfterNetworkPoll`, `ServerSession.cpp` ~line 284). A delete of fleet index N shifts every fleet above N down one (the `erase` in `ProcessDeleteFleetRequests`). An in-flight spawn-into/respawn request that was addressed at the pre-delete indexing then resolves against the **wrong fleet** for that same client. Blast radius is limited to the requesting client's own fleets (all four paths resolve `guid = pClient->clientGuid` first and bounds-check the index), so this is a correctness bug, not a cross-client exploit — but the protocol contradicts its own stable-identifier design and the delete-vs-spawn drain order makes the race reachable in one tick.

`FleetGuid` (`Fleet.h`, ~line 8) mirrors `engine::ClientGuid`: `{ uint64_t uiHigh; uint64_t uiLow; }` with defaulted `operator==` and a `FleetGuidHash`. Every `Fleet` already carries its `guid`, minted once server-side in `ProcessCreateFleetRequests` (`ServerFleetManager.cpp` ~line 36) and never mutated. Fleet-sync decode already reads the guid as two `engine::ReadUint64` calls (`PlayerEvents.cpp` ~line 95) — the same wire shape this plan adopts client-to-server.

## Design

Key the four request payloads by `FleetGuid` and have the server resolve guid → fleet, eliminating the index-shift race.

- **Payload change**: each of the four packets replaces its leading `int64 iFleetIndex` (8 bytes) with a `FleetGuid` (`uint64 uiHigh` then `uint64 uiLow`, 16 bytes). `kClientRespawnInFleetRequest` keeps its trailing `int64 iMemberIndex`; `kClientFleetNavigationDelay` keeps its trailing `float fDelay`.
- **Client send encoding**: the four `Send*` methods forward args through `ClientSession::SendGameRequest` (declared `ClientSession.h` ~line 103) into `SendSimplePacket`, whose `PushSimplePacketArg` dispatcher (`Engine/Source/Network/NetworkCursor.h` ~line 157) handles arithmetic args only. Callers therefore unwrap the guid at the call site — pass `rGuid.uiHigh, rGuid.uiLow` as two `uint64_t` args — matching the existing "unwrap enums/ids/flags at the call site" convention enforced by that dispatcher's `static_assert`.
- **Client send signatures** (`ClientSession.{h,cpp}`): change the four `Send*` signatures to take `const FleetGuid&` in place of `int64_t iFleetIndex` (respawn keeps `iMemberIndex`; nav-delay keeps `fDelay`). Update each method's LOG line to print `uiHigh`/`uiLow` instead of the index.
- **UI callers** (`HudScreen.cpp`, the only call sites — ~lines 296, 346, 362, 386): each currently passes `gpGame->FocusedFleetIndex()`. Convert index→guid at this boundary using the existing accessor `gpGame->FocusedFleet()` (`Game.h` ~line 116, forwarding `FleetSelection::FocusedFleet()` which returns `const Fleet*`): pass `->guid`. All four call sites sit inside UI branches where a focused fleet is already established; guard against a null `FocusedFleet()` only if the surrounding branch does not already guarantee it.
- **Server decode** (`ServerSession::ParseReceivedGamePackets`): in each of the four branches, replace the leading `engine::ReadInt64` with two `engine::ReadUint64` reads into a `FleetGuid`, and update the branch's minimum post-strip size check and its size comment: delete 8→16, spawn-into 8→16, respawn 16→24, nav-delay 12→20.
- **Server resolution** (`ServerFleetManager`): add one private guid→fleet lookup helper scoped to the client's `mFleets[clientGuid]` vector (returning `Fleet*` or a vector index; implementer's choice). `ProcessDeleteFleetRequests`, `ProcessSpawnIntoFleetRequests`, `ProcessRespawnInFleetRequests`, and `UpdateFleetNavigationDelay` each resolve the guid to the current fleet at drain time (post any deletes), replacing today's raw `iFleetIndex` bounds-check-and-index into `it->second`. A request whose guid no longer resolves is dropped (same `continue`/early-out shape as today's out-of-range index). Update the four functions' LOG lines to print the guid instead of the index.
- **Pending-request structs** (`ServerFleetManager.h` ~lines 18–35): `PendingDeleteFleetRequest`, `PendingSpawnIntoFleetRequest`, and `PendingRespawnInFleetRequest` change their `iFleetIndex` field to `FleetGuid fleetGuid`; `UpdateFleetNavigationDelay`'s `int64_t iFleetIndex` parameter likewise becomes `const FleetGuid&`. Member index (respawn) stays an index into the resolved fleet's members — member identity is out of scope.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change below, add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission beyond the named regions plus the mechanical necessities (includes, forward declarations) the named change requires.

**In scope:**

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.h` — declarations of `SendDeleteFleetRequest`, `SendSpawnIntoFleetRequest`, `SendRespawnInFleetRequest`, `SendFleetNavigationDelayRequest` (~lines 72–75) only.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` — the four matching definitions (~lines 350–380): signatures, guid unwrapping into `SendGameRequest` args, LOG lines.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/HudScreen.cpp` — the four `gpClientSession->Send*` call expressions (~lines 296, 346, 362, 386) only: swap the fleet-index argument for the focused fleet's guid.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — inside `ParseReceivedGamePackets`, the four `case` branches for `kClientDeleteFleetRequest`, `kClientSpawnIntoFleetRequest`, `kClientRespawnInFleetRequest`, `kClientFleetNavigationDelay` (~lines 138–190): size checks, size comments, decode reads, queue/dispatch argument.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.h` — the three `Pending*FleetRequest` struct fields, the `UpdateFleetNavigationDelay` parameter, and one new private guid→fleet lookup helper declaration.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — `ProcessDeleteFleetRequests`, `ProcessSpawnIntoFleetRequests`, `ProcessRespawnInFleetRequests`, `UpdateFleetNavigationDelay` (guid resolution replacing index lookup, LOG lines), plus the new helper's definition.

**Out of scope** (do not touch, even in the files above):

- **Member identity**: `kClientRespawnInFleetRequest`'s `iMemberIndex` stays an index into the fleet's members; members have no stable id. A member-guid scheme is a separate, larger change (members are added/removed, but not reordered the way a delete shifts the fleet vector, so the acute race is the fleet-index one).
- `kClientCreateFleetRequest` / `ProcessCreateFleetRequests` — no fleet target (creates one), unchanged.
- Server→client fleet-sync layout and its decode (`PlayerEvents.cpp`) — already carries `Fleet::guid`; no change.
- The `ProcessDeleteFleetRequests`-before-spawn drain ordering in `AfterNetworkPoll` — keying by guid removes the ordering *hazard*; reordering the drain is neither needed nor in scope.
- Server-side DoS caps / dedup on these same handlers — already in place (`ServerFleetManager` fleet-count/member caps + spawn dedup).
- `Fleet.h` — read-only reference; `FleetGuid` already exists and needs no change.
- `FleetSelection` / `Game` fleet-focus state and accessors — read-only; callers use the existing `FocusedFleet()`.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.h` / `.cpp` — the four `Send*` declarations and definitions.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/HudScreen.cpp` — sole UI caller of all four `Send*` methods.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `ParseReceivedGamePackets` decode branches.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.h` / `.cpp` — pending-request structs, the four handlers, new lookup helper.
- `Projects/BrokenEngineSandbox/Source/Fleet.h` — `FleetGuid` (read-only; the identifier this plan adopts on the wire).

## Risk tier and invariants

**Tier 3** — wire/protocol change (client-to-server game payload layout), a surface excluded from Tier 2.

- **Wire change in game-layer payloads** (`>= kGamePacketStart`, engine-opaque). No engine `kuiProtocolVersion` mechanic gates game payloads — the engine forwards them as raw bytes — so nothing auto-detects a client/server skew here: **client and server builds must move together**. Risk: a mixed-build pairing silently misparses fleet requests (wrong fleet or dropped), hard to catch without a version signal. See Coordination for the version-bump batching rule.
- **No determinism/CRC exposure**: no `Frame::kiVersion` change; `fNavigationDelay` is server-authoritative and guids do not enter sim CRC.
- Malformed/short payloads must still be rejected without partial application (existing size-check-then-`break` shape per branch).

## Acceptance criteria

- A delete of one fleet in the same tick as an in-flight spawn-into/respawn/nav-delay request for a *different* fleet never applies the second request to the wrong fleet.
- A request whose `FleetGuid` no longer resolves (fleet already deleted) is dropped, not misapplied.
- Fleet UI actions (create/delete/spawn-into/respawn/nav-delay) function identically for a single client with a stable fleet list — no user-visible behavior change absent the race.
- Client and server compile for both targets; the four decode branches' size checks match the new payload sizes exactly (16/16/24/20 post-strip).

## Coordination

- Protocol/version batch with `Documents/Plans/Network/WireFormatPairingGameSide.md`: these future wire breaks may share one new `kuiProtocolVersion` bump only when atomically co-landed; otherwise each incompatible release bumps beyond the current version 7 (`Engine/Source/Network/NetworkProtocol.h`) so skew rejects cleanly even though game payloads themselves are not version-checked. The unsubscribe-epoch wire change consumed version 7 independently.
- `Documents/Plans/Network/Architecture_WireFormatPairing.md`: never interleave send/receive-site restructuring with this wire change. WireFormatPairingGameSide's structured dependency requires the architecture plan first. (That architecture plan file is not currently present in this tree — treat its absence as the standard missing-plan-file condition, not a blocker for this plan.)

## Notes

- **No open architectural decision** — the identifier (`FleetGuid`) and its wire presence already exist; this is a mechanical re-key. The only judgment call (member identity) is deferred above. Trivial local choices left to the implementer: helper name, and whether the lookup returns `Fleet*` or a vector index.
