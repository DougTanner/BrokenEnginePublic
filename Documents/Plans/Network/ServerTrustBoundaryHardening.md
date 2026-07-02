# Server Trust-Boundary Hardening

## Context

The server is the side that must assume hostile / buggy input, but several receive-path and init sites treat client bytes and OS API results as trusted. This is a batch of small, independent robustness fixes sharing the "network/client input is a trust boundary" theme (root CLAUDE.md: validate anything opaque — network input, OS/third-party API results — while keeping no defensive checks between our own functions). Each item stands alone; group them because they touch the same handful of files and one review sweep.

The headline defects are an unclamped float that flows into deterministic sim state (a), an unbounded-growth DoS class (b, c), and two connection-state bugs in the handshake (d, e). The rest are observability / parity gaps.

### (a) `fNavigationDelay` read raw off the wire (two packets)

`game::ServerSession::ParseReceivedGamePackets` (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp`) reads a `float` with no range/NaN check in two branches:
- `kClientUpdatePlayerRequest` (~line 129): `float fNavigationDelay = engine::ReadFloat(pCursor);` → `QueueUpdatePlayerRequest({... fNavigationDelay})`.
- `kClientFleetNavigationDelay` (~line 183): `float fDelay = engine::ReadFloat(pCursor);` → `mpFleetManager->UpdateFleetNavigationDelay(guid, iFleetIndex, fDelay)`.

The value lands in `Fleet::fNavigationDelay` and seeds `fFrameChangeTimer` (`ServerFleetManager::OnPlayerSpawned` ~line 297, `ResetFleetForLoad` ~line 434). A `NaN` makes every `fFrameChangeTimer <= 0` comparison false → fleet navigation permanently frozen; it is also server-authoritative sim state. `fNavigationDelay` is server-only (drives server nav; clients only read it via fleet sync), so a server-side clamp introduces no cross-side determinism split.

### (b) Fleet/spawn request DoS (no cap, no dedup)

- `ServerFleetManager::ProcessCreateFleetRequests` (`ServerFleetManager.cpp` ~line 43) grows `mFleets[guid]` by one Fleet per `kClientCreateFleetRequest` with **no per-client cap**, and answers each with a reliable `SendFleetSyncToClient`. A client spamming creates grows server memory unbounded and amplifies each request into a reliable send.
- `ProcessSpawnIntoFleetRequests` (~line 95) and `ProcessRespawnInFleetRequests` (~line 120) call `gpServerSession->mpClientManager->QueueSpawnForClient(...)` with **no dedup** — unlike `ServerClientManager::ProcessSpawnRequests` (`ServerClientManager.cpp` ~line 42), which guards with `std::ranges::contains(mClientsWaitingForSpawn, iClientId, &ClientSpawnInfo::iClientId)`. Multiple spawn-into/respawn requests in one drain batch (or across ticks before `FinalizeNewClients` runs) each queue a spawn for the same slot.

### (c) Paused-queue unbounded growth + unpause burst

`Server::mPendingNewSubscriptions` (pushed in `Server::ClientSubscribe`, `ServerReceive.cpp` ~line 381) and `Server::mPendingResyncClientIds` (pushed in `Server::ClientResyncRequest` ~line 427) are deliberately persist-until-served (`Server::Poll` ~line 72 leaves them intact; consumers `SendNewSubscriptionFullStates` / game `HandleResyncRequests` run only post-tick, skipped while paused at `iFullTicks == 0`). While paused, a client spamming subscribe/unsubscribe cycles or 1-byte `kClientResyncRequest` packets grows these without bound, and the first unpaused tick emits one full-state send per duplicate entry.

### (d) `ClientHello` ghost-accept on null client

`Server::ClientHello` (`ServerReceive.cpp` ~line 305): after version checks pass, `FindClient(iClientId)` may return `nullptr` (e.g. a second Hello arriving after a prior version-mismatch rejection already ran `RemoveClient` + `enet_peer_disconnect_later`). The handler only sets fields `if (pClient != nullptr)` but then unconditionally logs `"Accepted"` (~line 312), sends `SendConnectionResponse(pPeer, true, ...)` (~line 313), and `game::gpServerSession->SendTimespeedToNewClient(pPeer)` (~line 315). The client believes it is connected while the server holds no `ClientConnection`.

### (e) Repeated `ClientHello` not idempotent

`Server::Receive` dispatches `kClientHello` to `ClientHello` unconditionally (no handshake gate). A second Hello from an already-handshaken client re-runs ~lines 296-310: if the payload GUID is empty a **fresh** GUID is minted (`UuidCreate`), then `pClient->clientGuid = clientGuid` **overwrites** the established identity mid-session. Fleet ownership is GUID-keyed (`mFleets`, `mGuidToClientId`) and `PendingDisconnect` carries the current GUID, so existing state is orphaned under the old identity while the client continues under a new one.

### (f) Server `Receive` lacks the trust-boundary try/catch

`Client::Receive` (`Engine/Source/Network/Client/Client.cpp` ~lines 190-242) wraps its dispatch switch in `try { ... } catch (const std::exception&)` so a corrupt payload drops one packet instead of tearing down the peer. `Server::Receive` (`Engine/Source/Network/Server/Server.cpp` ~lines 169-223) has **no** such backstop — yet the server is the side that must assume hostile input.

### (g) `NetworkManager::SendPacket` leaks `ENetPacket` on send failure

`NetworkManager::SendPacket` (`Engine/Source/Network/NetworkManager.h` ~lines 28-35) calls `enet_peer_send(pPeer, uiChannel, pPacket)` and ignores the result. `enet_peer_send` returns `< 0` without taking ownership when it fails (peer not in connected state — disconnect races; channel/size rejection), leaking the `enet_packet_create` allocation.

### (h) Silent init failures (ignored OS API results)

- `NetworkDiscoveryResponder::NetworkDiscoveryResponder` (`Engine/Source/Network/NetworkDiscoveryResponder.cpp` ~lines 12-21) ignores the results of `socket`, `bind`, and `ioctlsocket`. An occupied discovery port (`kuiDiscoveryPort`) leaves LAN discovery silently dead forever with no log.
- `NetworkDiscoveryScanner::NetworkDiscoveryScanner` (`NetworkDiscoveryScanner.cpp` ~lines 12-18) ignores `socket`, `setsockopt(SO_BROADCAST)`, and `ioctlsocket` (no `bind` here — corrected from the audit note).
- `NetworkManager::NetworkManager` (`NetworkManager.cpp` ~line 10) ignores `enet_initialize()`'s non-zero failure return.

### (i) Resync log level

`game::ServerSession::HandleResyncRequests` (`ServerSession.cpp` ~line 462) logs at `kError`; the house log-level guidance (root CLAUDE.md Key Patterns) puts desync-investigation events at `kWarning` ("investigate ... may spam"). (`Server::ClientResyncRequest` ~line 423 logs the same event at `kError` — fold it in for consistency.)

## Design

- **(a)** Add a validate-at-parse helper (reject `NaN`/`Inf`, clamp to a sane closed range) applied in both `ParseReceivedGamePackets` branches before the value leaves the parser. Range is a grill decision — see Notes.
- **(b)** In `ProcessCreateFleetRequests`, skip the create (and its sync send) when `mFleets[guid].size()` is at a per-client cap; log at `kWarning`. In `ProcessSpawnIntoFleetRequests` / `ProcessRespawnInFleetRequests`, dedup before `QueueSpawnForClient` — mirror the `ProcessSpawnRequests` `contains`-check, keyed on the full spawn identity `{iClientId, iFleetIndex, iMemberIndex}` (a `contains` predicate over `mClientsWaitingForSpawn`, reusing `ClientSpawnInfo`'s fields). Optionally cap members per fleet in `OnPlayerSpawned`'s new-member push.
- **(c)** Dedup on push instead of unbounded append: in `ClientSubscribe`, replace any existing `mPendingNewSubscriptions` entry for the same `{iClientId, iSlot}` rather than appending; in `ClientResyncRequest`, `contains`-check `mPendingResyncClientIds` before push. Both preserve semantics with an inherent bound (≤ 64 clients × 16 slots), and collapse the unpause burst to one send per slot.
- **(d)** In `ClientHello`, early-return (reject) when `FindClient` returns `nullptr` after the version checks — do not log Accepted / send the accept response / send timespeed for a client with no server state.
- **(e)** In `ClientHello`, if the looked-up client already has `bHandshakeComplete`, re-send the existing accept response using the **stored** `clientGuid` and ignore the payload GUID (idempotent replay) — do not overwrite identity or mint a new GUID.
- **(f)** Wrap the `Server::Receive` dispatch switch in the same `try/catch(const std::exception&)` as `Client::Receive`, logging the dropped packet at `kWarning` with type + `what()`. Confirm the receive handlers land parsed values in locals before mutating client state (they do today) so a mid-parse throw leaves no partial mutation.
- **(g)** Capture `enet_peer_send`'s return; on `< 0`, `enet_packet_destroy(pPacket)`. (`enet_packet_create` returning `nullptr` on OOM is a separate, out-of-scope concern — see Out of scope.)
- **(h)** Check each result; on failure log at `kError` (`INVALID_SOCKET` / `SOCKET_ERROR` for Winsock, non-zero for `enet_initialize`). No recovery path required — the log turns a silent dead subsystem into a diagnosable one.
- **(i)** Lower `HandleResyncRequests` (and `ClientResyncRequest`) resync logs from `kError` to `kWarning`.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `ParseReceivedGamePackets` float validation (a); `HandleResyncRequests` log level (i).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — `ProcessCreateFleetRequests` cap (b), `ProcessSpawnIntoFleetRequests` / `ProcessRespawnInFleetRequests` dedup (b), optional `OnPlayerSpawned` member cap.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.{h,cpp}` — `QueueSpawnForClient` / `mClientsWaitingForSpawn` dedup helper reused by (b); `ProcessSpawnRequests` is the existing dedup pattern.
- `Engine/Source/Network/Server/ServerReceive.cpp` — `Server::ClientHello` null-reject (d) + handshake-idempotency (e); `Server::ClientSubscribe` `mPendingNewSubscriptions` dedup-on-push (c); `Server::ClientResyncRequest` `mPendingResyncClientIds` dedup + log level (c, i).
- `Engine/Source/Network/Server/Server.cpp` — `Server::Receive` try/catch backstop (f).
- `Engine/Source/Network/NetworkManager.h` — `SendPacket` send-failure `enet_packet_destroy` (g).
- `Engine/Source/Network/NetworkManager.cpp` — `enet_initialize()` result check (h).
- `Engine/Source/Network/NetworkDiscoveryResponder.cpp` / `NetworkDiscoveryScanner.cpp` — ctor socket/bind/setsockopt/ioctlsocket result checks (h).

## Out of scope

- Any wire-format / payload-layout change or `kuiProtocolVersion` bump — every item preserves the wire. Fleet-request-by-guid is the separate `Network/FleetRequestsByGuid.md`; subscribe/unsubscribe epoch payload is `Network/SubscriptionLifecycleRaceHardening.md`.
- Recovery/retry behavior for the init failures in (h) — log-only; no rebind loop, no port fallback.
- `enet_packet_create` returning `nullptr` (OOM) in `SendPacket` — a distinct hardening concern, not the peer-send-failure leak this plan fixes.
- The `mClientsWaitingForSpawn` order-sensitive spawn-assignment invariant (Server/CLAUDE.md) — the (b) dedup must preserve queue order; it does not restructure the assignment flow.
- Rate-limiting inbound packets generally, or any global connection-flood mitigation — (b)/(c) bound only the specific unbounded server-side containers.

## Acceptance criteria

- A `kClientUpdatePlayerRequest` / `kClientFleetNavigationDelay` carrying `NaN`/`Inf`/out-of-range `fNavigationDelay` is clamped/rejected at parse; fleet navigation cannot be frozen by a hostile nav-delay value.
- A client cannot grow `mFleets`, `mClientsWaitingForSpawn`, `mPendingNewSubscriptions`, or `mPendingResyncClientIds` without bound via repeated requests; duplicate spawn-into/respawn requests queue at most one spawn.
- A second `ClientHello` after rejection does not produce an "Accepted" response for a client with no `ClientConnection`; a repeat Hello from a handshaken client preserves its established GUID.
- A corrupt server-bound packet drops one packet (logged `kWarning`) rather than propagating an exception out of `Server::Receive`.
- Failed discovery socket setup / `enet_initialize` / `enet_peer_send` are logged (and the packet freed) instead of failing silently.

## Notes

- **Invariant exposure**: no wire change; no `kuiProtocolVersion` / `Frame::kiVersion` change. Item (a) touches a value entering server-authoritative sim state (clamp only, server-side; `fNavigationDelay` is not client-simulated, so no CRC/determinism split). Items (d)/(e) touch the handshake connection state machine (Risk: needs a reconnect/version-mismatch playtest). Server allocation-tracked paths — new caps/dedup live inside the existing `ScopedSuppressAllocationTracking` scopes.
- **Grill decisions to pre-stage**:
  - (a) clamp range for `fNavigationDelay` — the audit suggested `[1.0f, 3600.0f]`; confirm the min against the smallest intended in-game nav delay (default is `60.0f`, `Fleet.h`) so a legit low setting is not clamped up.
  - (b) per-client fleet cap and per-fleet member cap values (and whether a member cap is worth adding at all vs fleet-count cap alone).
  - Whether to split this batch: the DoS/state-machine items (a-e) are the load-bearing correctness fixes; (f-i) are parity/observability and could land separately if the batch feels too broad.
- **Co-scheduling**: touches `ServerReceive.cpp` (`ClientSubscribe`, `ClientHello`, `ClientResyncRequest`) which `Network/SubscriptionLifecycleRaceHardening.md` and `Network/Architecture_WireFormatPairing.md` also edit, and `ServerSession.cpp` which `Network/FleetRequestsByGuid.md` edits — refresh citations if co-scheduled; never interleave with the wire-format restructure.
