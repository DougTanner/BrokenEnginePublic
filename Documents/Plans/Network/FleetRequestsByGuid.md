# Fleet Requests by FleetGuid

## Context

The client addresses fleets in four request packets by **mutable vector index** (`iFleetIndex` into the server's `mFleets[guid]` vector), even though `FleetGuid` exists precisely as the stable client-facing identifier. `Fleet.h` documents it: the guid "survives disconnect/reconnect, save/load, and full client restart", and `Fleet::guid` is already synced to clients in every fleet-sync packet (`SendFleetSync`). Addressing by index contradicts the identifier's own design.

The four index-keyed request paths (client send → server decode → server action):

| Packet (`GamePacketType`) | Client send site (`ClientSession.cpp`) | Server decode (`ServerSession::ParseReceivedGamePackets`) | Server action |
|---|---|---|---|
| `kClientDeleteFleetRequest` | `SendDeleteFleetRequest(iFleetIndex)` ~line 432 | ~line 138 → `QueueDeleteRequest({iClientId, iFleetIndex})` | `ServerFleetManager::ProcessDeleteFleetRequests` ~line 66 |
| `kClientSpawnIntoFleetRequest` | `SendSpawnIntoFleetRequest(iFleetIndex)` ~line 450 | ~line 149 → `QueueSpawnIntoRequest({iClientId, iFleetIndex})` | `ProcessSpawnIntoFleetRequests` ~line 95 |
| `kClientRespawnInFleetRequest` | `SendRespawnInFleetRequest(iFleetIndex, iMemberIndex)` ~line 468 | ~line 161 → `QueueRespawnRequest({iClientId, iFleetIndex, iMemberIndex})` | `ProcessRespawnInFleetRequests` ~line 120 |
| `kClientFleetNavigationDelay` | `SendFleetNavigationDelayRequest(iFleetIndex, fDelay)` ~line 486 | ~line 174 → `UpdateFleetNavigationDelay(pClient->clientGuid, iFleetIndex, fDelay)` ~line 522 | (immediate) |

The server drains queues in fixed order each `PreTickNetwork`: `ProcessDeleteFleetRequests` **before** `ProcessSpawnIntoFleetRequests` / `ProcessRespawnInFleetRequests` (`ServerSession::PreTickNetwork` ~lines 270-273). A delete of fleet index N shifts every fleet above N down one (`it->second.erase(begin + N)`, `ServerFleetManager.cpp` ~line 89). An in-flight spawn-into/respawn request that was addressed at the pre-delete indexing then resolves against the **wrong fleet** for that same client. Blast radius is limited to the requesting client's own fleets (all four paths resolve `guid = pClient->clientGuid` first and bounds-check the index), so this is a correctness bug, not a cross-client exploit — but the protocol contradicts its own stable-identifier design and the delete-vs-spawn drain order makes the race reachable in one tick.

`FleetGuid` (`Fleet.h`) mirrors `engine::ClientGuid`: `{ uint64 uiHigh; uint64 uiLow; }`. Every `Fleet` already carries its `guid`, minted once server-side in `ProcessCreateFleetRequests` (~line 58) and never mutated.

## Design

Key the four request payloads by `FleetGuid` and have the server resolve guid → fleet, eliminating the index-shift race.

- **Payload change**: each of the four packets replaces its leading `int64 iFleetIndex` (8 bytes) with a `FleetGuid` (`uint64 uiHigh` + `uint64 uiLow`, 16 bytes). `kClientRespawnInFleetRequest` keeps its trailing `int64 iMemberIndex`; `kClientFleetNavigationDelay` keeps its trailing `float fDelay`. `SendSimplePacket`'s `PushSimplePacketArg` dispatcher handles arithmetic args only, so callers unwrap the guid at the call site (pass `rGuid.uiHigh, rGuid.uiLow` as two `uint64` args) — matching the existing "unwrap enums/ids/flags at the call site" convention (Engine Network hub). Server decode reads two `uint64` via `engine::ReadUint64` and updates each branch's minimum-size check (delete/spawn-into: 8→16; respawn: 16→24; nav-delay: 12→20).
- **Client send sites** (`ClientSession.cpp`): change the four `Send*` signatures to take a `FleetGuid` (nav-delay/respawn keep their extra arg). The UI callers already know which `Fleet` they act on (`FleetSelection` owns the client fleet list); pass `fleet.guid` instead of the list index. Trace each caller and convert index→guid at the UI boundary.
- **Server resolution**: add a `FleetGuid`→`Fleet&` (or `→ vector index`) lookup on `ServerFleetManager` scoped to the client's `mFleets[clientGuid]` vector; each `Process*` handler and `UpdateFleetNavigationDelay` resolves the guid to the current fleet at drain time (post any deletes), replacing today's raw index into `it->second`. Member index (respawn) stays an index into the resolved fleet's `members` — member identity by index is a separate concern (see Out of scope). Requests whose guid no longer resolves are dropped (same as today's out-of-range index → `continue`).
- **`PendingDeleteFleetRequest` / `PendingSpawnIntoFleetRequest` / `PendingRespawnInFleetRequest`** (`ServerFleetManager.h`) change their `iFleetIndex` field to `FleetGuid fleetGuid`.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp` — `SendDeleteFleetRequest`, `SendSpawnIntoFleetRequest`, `SendRespawnInFleetRequest`, `SendFleetNavigationDelayRequest` signatures + payloads.
- UI / selection callers of those four `Send*` methods (locate via `FleetSelection` and the HUD fleet controls under `Projects/BrokenEngineSandbox/Source/Ui/`) — convert index→`fleet.guid` at the call site.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `ParseReceivedGamePackets` decode for the four `GamePacketType` branches (size checks + `ReadUint64` pair).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.{h,cpp}` — request-struct fields (`PendingDeleteFleetRequest` etc.), `ProcessDeleteFleetRequests` / `ProcessSpawnIntoFleetRequests` / `ProcessRespawnInFleetRequests` / `UpdateFleetNavigationDelay` guid resolution, new guid→fleet lookup helper.
- `Projects/BrokenEngineSandbox/Source/Fleet.h` — `FleetGuid` (already present; the identifier this plan adopts on the wire).

## Out of scope

- **Member identity**: `kClientRespawnInFleetRequest`'s `iMemberIndex` stays an index into `Fleet::members`; members have no stable id. A member-guid scheme is a separate, larger change (members are added/removed, but not reordered the way a delete shifts the fleet vector, so the acute race is the fleet index one).
- `kClientCreateFleetRequest` — no fleet target (creates one), unchanged.
- `kServerFleetSync` layout — already carries `Fleet::guid`; no change needed for the server→client direction.
- The `ProcessDeleteFleetRequests`-before-spawn drain ordering itself — keying by guid removes the ordering *hazard*; reordering the drain is neither needed nor in scope.
- Server-side DoS caps / dedup on these same handlers — already in place (`ServerFleetManager` fleet-count/member caps + `QueueSpawnForClient` dedup).

## Acceptance criteria

- A delete of one fleet in the same tick as an in-flight spawn-into/respawn/nav-delay request for a *different* fleet never applies the second request to the wrong fleet.
- A request whose `FleetGuid` no longer resolves (fleet already deleted) is dropped, not misapplied.
- Fleet UI actions (create/delete/spawn-into/respawn/nav-delay) function identically for a single client with a stable fleet list — no user-visible behavior change absent the race.

## Notes

- **Invariant exposure**: **wire change in game-layer payloads** (`>= kGamePacketStart`, engine-opaque). No engine `kuiProtocolVersion` mechanic gates game payloads — the engine forwards them as raw bytes — so nothing auto-detects a client/server skew here: **client and server builds must move together**. No `Frame::kiVersion` / CRC / determinism exposure (`fNavigationDelay` is server-authoritative; guids do not enter sim CRC). Risk: a mixed-build pairing silently misparses fleet requests (wrong fleet or dropped), hard to catch without a version signal.
- **Version-break batching**: coordinate the compatibility break with the queue's other pending wire changes — `Network/PackIntegrityHandshake.md` and `Network/SubscriptionLifecycleRaceHardening.md` (both bump `kuiProtocolVersion`). Landing all wire-affecting changes in one client/server release, gated behind a single protocol-version bump, gives a clean rejection on skew even though the game payloads themselves aren't version-checked. Do **not** interleave with `Network/Architecture_WireFormatPairing.md`.
- **No open architectural decision** — the identifier (`FleetGuid`) and its wire presence already exist; this is a mechanical re-key. The only judgment call (member identity) is deferred above.
