<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Refactor: Server Per-Client Player Registry

## Context

Server per-client state lives in three homes kept in lockstep by hand — the game-server constellation's principal structural weakness:

1. **Engine `ClientConnection`** (`Engine/Source/Network/Server/Server.h`): `authorizedCoords`, plus four parallel per-slot vectors (`coordSubscriptions`, `coordAckStates`, `prevResendCounts`, `resendLogCooldowns`), all resized together in `Server::Connect` (`Server.cpp`).
2. **Game `ServerSession::mClientOwnedPlayerIds`** (`ServerSession.h`, `std::unordered_map<int64_t, std::vector<engine::global_id_t>>`) — index-aligned with `authorizedCoords` (the documented "parallel-vector invariant", `Engine/Source/Network/Server/AGENTS.md` and `Projects/BrokenEngineSandbox/Source/Network/Server/AGENTS.md`). Mutated from the game server managers listed in the scope contract.
3. **`ServerFleetManager`'s two GUID maps** (`ServerFleetManager.h`): `mFleets` and `mGuidToClientId`.

Two near-verbatim relink routines rebuild the game-side pair: `ServerClientManager::TryRelinkNewClient` and `ServerSession::TryRelinkClientForLoad`. Both define a local `RelinkEntry`, scan `gpGame->mCoordFrames` for `ClientGuid` matches, sort by global id, then reserve/push into the owned-id vector and `authorizedCoords` and send assignment/player state; they differ only in log category/level/text.

Related fleet-manager duplication: `ServerFleetManager::OnClientConnected` duplicates the member-refresh body of `ResetFleetForLoad` — the same `bAlive` recompute and `authorizedCoords`-by-index coord refresh. `OnClientConnected` lacks the dead-flagship shift performed after load; current death-processing order merely masks that asymmetry.

Engine-side, `ClientConnection::FreeSlot` carries defensive bounds guards (`iSlot < std::ssize(...)`) for `prevResendCounts` and `resendLogCooldowns` against a size mismatch that cannot occur because all four vectors are resized together. Those guards are evidence that the parallel-vector shape is fragile.

The composed-session seam is explicit: `engine::ServerSessionRuntime` owns the engine `Server` and runtime-only queue drains, while the game `ServerSession` façade owns gameplay policy and `mClientOwnedPlayerIds`. This plan keeps the registry game-owned; it does not move gameplay ownership into the engine runtime.

## Scope contract

The in-scope list below is both target and ceiling. Make the smallest complete change that satisfies the acceptance criteria; add no abstractions, configuration, extension points, or fixes to adjacent code encountered along the way. Naming a file grants permission to touch only the named functions/members/regions, plus the mechanical necessities the named change requires (includes, forward declarations, declaration lines in the matching header, vcxproj/filter membership for the new files). Exact identifier names below are decided; purely local details (loop variable names, log message wording within the stated category/level) are implementer discretion.

### In scope

**Game — new registry type:**
- New `Projects/BrokenEngineSandbox/Source/Network/Server/ClientPlayerRegistry.h` / `.cpp` (whole-file `BT_SERVER` guard; server-project vcxproj + filter membership).

**Game — registry owner (`Projects/BrokenEngineSandbox/Source/Network/Server/`):**
- `ServerSession.h` — replace the `mClientOwnedPlayerIds` member with a `ClientPlayerRegistry` value member; delete the `TryRelinkClientForLoad` declaration.
- `ServerSession.cpp` — `ResetClientsForLoad` (registry `Clear` + `RelinkFromFrames` call); delete `TryRelinkClientForLoad`.

**Game — mutation sites routed through the registry (`Projects/BrokenEngineSandbox/Source/Network/Server/`):**
- `ServerClientManager.h` — delete the `TryRelinkNewClient` declaration.
- `ServerClientManager.cpp` — `NewClients` (emptiness check via `Owned()`, relink call), `FinalizeNewClients` (spawn append via `Add`), `Disconnects` (entry erase via `Remove`), `DetectPlayerDeaths` (death removal via `Owned`/`RemoveAt`); delete `TryRelinkNewClient`.
- `ServerTransferManager.cpp` — `TrackClientTransfers` (owned-player coord move via `UpdateCoord`).

**Game — read sites become registry queries (`Projects/BrokenEngineSandbox/Source/Network/Server/`):**
- `ServerBroadcaster.cpp` — `ProcessUpdatePlayerRequests` (coord lookup from `Owned()` pairs).
- `ServerFleetManager.h` / `.cpp` — `OnClientConnected`, `OnResetForLoad`, `ResetFleetForLoad` (read `Owned()` pairs; helper extraction and signature change per Design).

**Engine — `SlotState` merge (`Engine/Source/Network/Server/`):**
- `Server.h` — in `ClientConnection`: new `SlotState` struct; replace the `coordSubscriptions`, `coordAckStates`, `prevResendCounts`, `resendLogCooldowns` members with one `std::vector<SlotState> slots`; rewrite `FindSlotForCoord`, `AllocateSlot`, `FreeSlot` (delete both bounds guards).
- `Server.cpp` — `Connect`: the four `resize` calls become one.
- `ServerSend.cpp` — accessor rewrites only, in `SendCoordFullState`, `SendCoordStaticData`, `SendSubscribeAccept`, `SendUpdate`, `SendResends`, `UpdateResendLogState`.
- `ServerReceive.cpp` — accessor rewrites only, in `ClientAckStream`, `ClientSubscribe`, `ClientUnsubscribe`.

**Game — engine slot-accessor rewrites only (mechanical `slots.at(i).<field>` form):**
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerTransferManager.cpp` — file-static `IsDestinationLive`.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `AddSubscribedCoords`, `SendNewSubscriptionFullStates`, `HandleResyncRequests`, and the slot-free loop in `ResetClientsForLoad`.
- `Projects/BrokenEngineSandbox/Source/Server/ServerDisplay.cpp` — `ServerDisplayContentChanged`, `PaintGridMap`.

### Out of scope

- Consolidating the `ServerFleetManager` GUID maps (`mFleets`, `mGuidToClientId`) — named as context only; this plan shares the refresh helper and reads the new registry from the fleet manager, nothing more.
- Any wire-format change: assign/spawn/state packets and their send order are untouched; this is server bookkeeping only.
- Engine/game session ownership: `ServerSessionRuntime` continues to own transport mechanics; the registry remains game policy behind the `ServerSession` façade. Direct pending-queue access patterns (`mpRuntime->mpServer->mPending*`) are not changed here.
- Landed subscription-lifecycle behavior — preserve the client/server slot-state contracts documented in `Engine/Source/Network/Client/AGENTS.md` and `Engine/Source/Network/Server/AGENTS.md`; fold no further slot state-machine changes in here.
- `Documents/Features/Network/ForwardErrorCorrection.txt` would add a fifth per-slot vector; `SlotState` makes that a future field add, but implementing FEC is not this plan.
- `AckState` / `ClientCoordSubscription` field layouts stay identical (moved into `SlotState`, not redesigned).
- `authorizedCoords` remains on `engine::ClientConnection` and remains the engine's authority for subscription adjacency (`ServerReceive.cpp` `ClientSubscribe`) and server display (`ServerDisplay.cpp`); it is not moved or removed.

## Design

### 1. Game: `ClientPlayerRegistry` owned by `ServerSession`

A value type owning per-client `{globalId, coord}` pairs, replacing `mClientOwnedPlayerIds` **and** every game-side by-index read of `authorizedCoords` for owned players. The registry **stores the coord** in the pair — this is what eliminates the parallel-vector invariant. `authorizedCoords` is reduced to the engine's subscription-adjacency/display list; the registry is the single source for the owned-player→coord mapping.

```cpp
struct OwnedPlayer
{
	engine::global_id_t globalId {};
	engine::GridCoord coord {};
};

class ClientPlayerRegistry
{
public:
	void Add(int64_t iClientId, engine::global_id_t globalId, engine::GridCoord coord);   // spawn / relink append
	void RemoveAt(int64_t iClientId, int64_t iIndex);                                     // death removal
	void UpdateCoord(int64_t iClientId, engine::global_id_t globalId, engine::GridCoord newCoord); // transfer move
	std::span<const OwnedPlayer> Owned(int64_t iClientId) const;                          // empty span when absent
	void Remove(int64_t iClientId);                                                       // disconnect erase (registry-only)
	void Clear(int64_t iClientId);                                                        // load reset: empties pairs and the connection's authorizedCoords
	enum class RelinkContext { kConnect, kLoad };
	int64_t RelinkFromFrames(int64_t iClientId, const engine::ClientGuid& rGuid, RelinkContext eContext); // returns owned count
private:
	std::unordered_map<int64_t, std::vector<OwnedPlayer>> mOwned;
};
```

**Mirror contract (the desync-proof-by-construction property):** `Add`, `RemoveAt`, `UpdateCoord`, and `Clear` internally locate the connection via `engine::gpServer->FindClient(iClientId)` and apply the matching `authorizedCoords` mutation (push_back / erase-at-same-index / in-place write / clear) themselves, preserving today's one-entry-per-owned-player content and order. Call sites make exactly one registry call and never touch `authorizedCoords` directly, so the pair cannot be half-updated. `Remove` is registry-only (the engine has already dropped the connection on the disconnect path). If `FindClient` returns null, the mutator updates only the registry.

**Call-site routing (behavior-preserving):**
- `ServerClientManager::NewClients` — replace the `try_emplace` owned-id lookup with `Owned(rClient.iClientId).empty()`; replace the `TryRelinkNewClient` call with `RelinkFromFrames(rClient.iClientId, rClient.clientGuid, RelinkContext::kConnect) > 0`.
- `ServerClientManager::FinalizeNewClients` — replace the owned-id `push_back` + `authorizedCoords.push_back(engine::kOriginCoord)` pair with `Add(pClient->iClientId, globalPlayerId, engine::kOriginCoord)`.
- `ServerClientManager::Disconnects` — replace the map `erase` with `Remove(rDisconnect.iClientId)`; the player-count log reads `Owned(...).size()`.
- `ServerClientManager::DetectPlayerDeaths` — reverse-iterate `Owned(rClient.iClientId)`; on death, replace the two coordinated erases with one `RemoveAt(rClient.iClientId, i)`; coord for the liveness scan comes from the pair, not `authorizedCoords.at(i)`.
- `ServerTransferManager::TrackClientTransfers` — replace the owned-id scan + `authorizedCoords.at(k) = destination` write with a scan of `Owned(rClient.iClientId)` and one `UpdateCoord(rClient.iClientId, globalPlayerId, destination)`.
- `ServerBroadcaster::ProcessUpdatePlayerRequests` — find the request's `globalId` in `Owned(pClient->iClientId)` and take `coord` from the pair; drop the `try_emplace` insertion.
- `ServerSession::ResetClientsForLoad` — replace the owned-id/`authorizedCoords` clears with `Clear(rClient.iClientId)`, then call `RelinkFromFrames(..., RelinkContext::kLoad)`; a zero return keeps today's "no GUID match, will respawn" log.

### 2. Single relink routine

`RelinkFromFrames` subsumes `TryRelinkNewClient` and `TryRelinkClientForLoad`; both are deleted. Body (identical to both current routines): return 0 when `rGuid.IsEmpty()`; scan `gpGame->mCoordFrames` current frames' `PlayersPostRender` for `pClientGuids[i] == rGuid`, collecting `{pGlobalPlayerIds[i], coord}`; sort ascending by `globalId.iValue` (creation-order-stable assign send order); then for each entry `Add(...)`, `gpServerSession->SendAssignPlayer(...)`, `gpServerSession->SendPlayerState(iClientId, PlayerStateWireType::kSpawned, ...)`, and log. `RelinkContext` selects the log line only — connect keeps `kNetwork`/`kVerbose`, load keeps `kDefault`/`kDebug`; two distinct `LOG` branches inside the helper are fine.

### 3. Game: shared fleet-refresh helper

Extract the duplicated member-refresh body of `ServerFleetManager::OnClientConnected` and `ServerFleetManager::ResetFleetForLoad` (per-member `bAlive` recompute + coord refresh) into one private helper taking `Fleet&` and `std::span<const OwnedPlayer>`; both call it, reading pairs from the registry instead of zipping `rOwnedIds` against `pClient->authorizedCoords` by index. `ResetFleetForLoad`'s signature drops its `rOwnedIds`/`pClient` parameters accordingly (`OnResetForLoad` updates its call). Additionally give `OnClientConnected` the **same dead-flagship shift** `ResetFleetForLoad` performs (when the flagship member is dead, call `mNavigation.ShiftFlagshipAfterDeath`), removing the ordering-dependent asymmetry. `OnClientConnected` does **not** gain `ResetFleetForLoad`'s flagship-alive branch (`wantedCoord` set + `QueueFlagshipUpdate`) — that stays load-only.

### 4. Engine: collapse the four parallel per-slot vectors into `struct SlotState`

In `Server.h`, replace `coordSubscriptions` / `coordAckStates` / `prevResendCounts` / `resendLogCooldowns` on `ClientConnection` with:

```cpp
struct SlotState
{
	ClientCoordSubscription subscription;
	AckState ack;
	int64_t iPrevResendCount = 0;
	int64_t iResendLogCooldown = 0;
};
std::vector<SlotState> slots;
```

sized once in `Server::Connect` (`slots.resize(NetworkManager::kiMaxEnetCoordSlots)` replaces the four resizes). Rewrite `FindSlotForCoord` / `AllocateSlot` / `FreeSlot` over `slots`; `FreeSlot` keeps the epoch-preserving reset semantics and **deletes** the two `iSlot < std::ssize(...)` guards — with one vector the sizes cannot diverge. Every `.at(iSlot)` accessor at the in-scope sites in `ServerSend.cpp`, `ServerReceive.cpp`, `ServerSession.cpp`, `ServerTransferManager.cpp`, and `ServerDisplay.cpp` becomes the corresponding `slots.at(iSlot).<field>` form with no logic change.

## Risk tier and invariants

**Tier 3** — the change spans the independently owned engine server transport (`Engine/Source/Network/Server/`) and game server session (`Projects/BrokenEngineSandbox/Source/Network/Server/`) subsystems.

- No CRC/determinism sim-path exposure, no `.pack`/`kiVersion` change, no wire-byte change. **But** relink governs which client owns which player and the assign/spawn send order (creation-order-stable by global-id sort); a registry bug mis-assigns ownership on reconnect/load. Ownership/assign-order parity is the correctness bar.
- Preserve the slot-state contracts in `Engine/Source/Network/Server/AGENTS.md` (epoch increments on reuse, epoch-preserving `FreeSlot`, slot-count limits) exactly; `SlotState` is a storage merge, not a state-machine change.
- Allocation tracking: registry growth runs under the existing `ScopedSuppressAllocationTracking` scopes in `ServerClientManager` (`NewClients`, `FinalizeNewClients`, `Disconnects`, `DetectPlayerDeaths`), `ServerBroadcaster::ProcessUpdatePlayerRequests`, `ServerFleetManager::OnClientConnected` / `OnResetForLoad`, and `ServerSession::ResetClientsForLoad`; keep those guards on the registry calls per the manager guard convention.

## Acceptance criteria

- Exactly one relink routine exists (`ClientPlayerRegistry::RelinkFromFrames`); `ServerClientManager::NewClients` and `ServerSession::ResetClientsForLoad` both call it; `TryRelinkNewClient` and `TryRelinkClientForLoad` are deleted.
- No game code outside `ClientPlayerRegistry` indexes `authorizedCoords` by owned-player position or mutates it — the owned-player→coord mapping comes from the registry, and each ownership mutation is one registry call (no two coordinated erases at any call site).
- `ServerFleetManager::OnClientConnected` performs the dead-flagship shift; the member-refresh body exists once, shared with `ResetFleetForLoad`.
- `engine::ClientConnection` holds one slot vector; `FreeSlot` has no bounds guards; `AckState` and `ClientCoordSubscription` layouts are unchanged.
- Server build compiles; assign/spawn/death/transfer/reconnect/load paths behave identically (owned set unchanged, assign send order creation-stable).

## Coordination

- Prerequisite satisfied: subscription-lifecycle hardening is landed. Preserve its slot-state behavior while moving registry bookkeeping; the permanent contract is in `Engine/Source/Network/Client/AGENTS.md`, `Engine/Source/Network/Server/AGENTS.md`, and `Documents/Architecture/Network.md`.
- Both `Engine/Source/Network/Server/AGENTS.md` and `Projects/BrokenEngineSandbox/Source/Network/Server/AGENTS.md` document the parallel-vector invariant this plan removes; the post-change documentation pass updates that guidance.
