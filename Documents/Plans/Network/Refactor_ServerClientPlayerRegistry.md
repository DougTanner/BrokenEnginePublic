# Refactor: Server Per-Client Player Registry

## Context

Server per-client state lives in three homes kept in lockstep by hand — the game-server constellation's principal structural weakness:

1. **Engine `ClientConnection`** (`Engine/Source/Network/Server/Server.h`): `authorizedCoords`, plus four parallel per-slot vectors (`coordSubscriptions`, `coordAckStates`, `prevResendCounts`, `resendLogCooldowns`), all resized together in `Server::Connect` (`Server.cpp`).
2. **Game `ServerSession::mClientOwnedPlayerIds`** (`ServerSession.h`, `std::unordered_map<int64_t, std::vector<global_id_t>>`) — index-aligned with `authorizedCoords` (the documented "parallel-vector invariant", `Network/Server/AGENTS.md`). Mutated from the game server managers listed under Critical files.
3. **`ServerFleetManager`'s three GUID maps** (`ServerFleetManager.h`): `mFleets`, `mPlayerToGuid`, `mGuidToClientId`.

Two near-verbatim relink routines rebuild the game-side pair: `ServerClientManager::TryRelinkNewClient` and `ServerSession::TryRelinkClientForLoad`. Both define a local `RelinkEntry`, scan `mCoordFrames` for GUID matches, sort by global id, then reserve/push and send assignment/player state; they differ mainly in log context and owner qualification.

Related fleet-manager duplication: `ServerFleetManager::OnClientConnected` duplicates the member-refresh body of `ResetFleetForLoad` — the same `bAlive` recompute and `authorizedCoords`-by-index coord refresh. `OnClientConnected` lacks the dead-flagship shift performed after load; current death-processing order merely masks that asymmetry.

Engine-side, `ClientConnection::FreeSlot` carries defensive bounds guards for `prevResendCounts` and `resendLogCooldowns` against a size mismatch that cannot occur because all four vectors are resized together. Those guards are evidence that the parallel-vector shape is fragile.

The composed-session seam is now explicit: `ServerSessionRuntime` owns the engine `Server` and runtime-only queue drains, while the game `ServerSession` façade owns gameplay policy and `mClientOwnedPlayerIds`. This plan keeps the registry game-owned and reaches runtime-only pending data through the façade; it does not move gameplay ownership into the engine runtime.

## Design

### Game: `ClientPlayerRegistry` owned by `ServerSession`

Introduce a value type owning per-client `{global_id_t globalId, GridCoord coord}` pairs, replacing `mClientOwnedPlayerIds` **and** the by-index reads of `authorizedCoords` for owned players. Sketch (final names/signatures at grill):

- `void Add(int64_t iClientId, global_id_t globalId, GridCoord coord)` — spawn / relink append.
- `void RemoveAt(int64_t iClientId, int64_t iIndex)` — death removal (keeps pair alignment by construction — one erase, not two).
- `void UpdateCoord(int64_t iClientId, global_id_t globalId, GridCoord newCoord)` — transfer coord move (replaces the `authorizedCoords.at(k) = destination` in-place write).
- `std::span<const OwnedPlayer> Owned(int64_t iClientId) const` — read access for fleet-refresh / death scan / broadcast.
- `void Remove(int64_t iClientId)` — disconnect erase.
- `void Clear(int64_t iClientId)` — load reset.
- `int64_t RelinkFromFrames(int64_t iClientId, const ClientGuid& rGuid, ...)` — the **single** relink routine that both connect and load call, parameterized on the log context and any connect-vs-load difference (returns owned count). This subsumes both `TryRelinkNewClient` and `TryRelinkClientForLoad`.

`authorizedCoords` on `ClientConnection` stays the engine's authority for subscription adjacency (`ServerReceive.cpp`) and server display queries (`ServerDisplay.cpp`); the registry becomes the single source for the *owned-player→coord* mapping that game code currently derives by zipping `mClientOwnedPlayerIds` against `authorizedCoords` by index. Decide at grill whether the registry stores coord (duplicating `authorizedCoords`) or the two stay linked — see Notes.

### Game: shared fleet-refresh helper

Extract the `OnClientConnected`/`ResetFleetForLoad` member-refresh body (`bAlive` recompute + coord refresh from the registry) into one helper both call, and give `OnClientConnected` the **same dead-flagship shift** `ResetFleetForLoad` performs — removing the ordering-dependent asymmetry.

### Engine: collapse the four parallel per-slot vectors into `struct SlotState`

Replace `coordSubscriptions` / `coordAckStates` / `prevResendCounts` / `resendLogCooldowns` on `ClientConnection` with a single `std::vector<SlotState>` (`SlotState { ClientCoordSubscription subscription; AckState ack; int64_t iPrevResendCount; int64_t iResendLogCooldown; }`), sized once in `Connect`. Update `FindSlotForCoord` / `AllocateSlot` / `FreeSlot` and every `.at(i)` accessor across `ServerSend.cpp`, `ServerReceive.cpp`, and the game `coordSubscriptions` reads in `ServerSession.cpp` and `ServerTransferManager.cpp` to `slots.at(i).<field>`. **Delete** the `iSlot < std::ssize(...)` guards in `FreeSlot` — with one vector the sizes cannot diverge.

## Critical files

**Game — every `mClientOwnedPlayerIds` / `authorizedCoords` mutation site (must route through the registry):**
- `ServerClientManager.cpp` — `TryRelinkNewClient`, `FinalizeNewClients`, `Disconnects`, and `DetectPlayerDeaths`.
- `ServerTransferManager.cpp` — `TrackClientTransfers` in-place owned-player coord update.
- `ServerSession.cpp` — `ResetClientsForLoad` and `TryRelinkClientForLoad`.

**Game — read sites (become registry queries):**
- `ServerFleetManager.cpp` — `OnClientConnected`, `OnResetForLoad`, and `ResetFleetForLoad`.
- `ServerBroadcaster.cpp` — `ProcessUpdatePlayerRequests`.
- `ServerClientManager.cpp` — disconnect logging and death scans.

**Game — owner / new type:**
- `ServerSession.h` — replace `mClientOwnedPlayerIds` member with the registry; new `ClientPlayerRegistry.{h,cpp}` (game vcxproj + `BT_SERVER` guard + filters).
- `ServerSessionRuntime.{h,cpp}` and `ServerSession.{h,cpp}` — preserve runtime ownership of `Server` and façade ownership of the gameplay registry; pending queue access continues through typed façade/runtime methods.

**Engine — `SlotState` merge:**
- `Server.h` — `ClientConnection` plus `FindSlotForCoord`/`AllocateSlot`/`FreeSlot`; `Server.cpp` connect sizing becomes one vector; update `ServerSend.cpp`, `ServerReceive.cpp`, game `ServerSession.cpp`, and `ServerTransferManager.cpp` accessors.

## Out of scope

- The three `ServerFleetManager` GUID maps — named as context, but consolidating them is a separate follow-up (this plan only *shares the refresh helper* and reads the new registry from the fleet manager).
- Any wire-format change: assign/spawn/state packets and their send order are untouched; this is server bookkeeping only.
- Engine/game session ownership: `ServerSessionRuntime` continues to own transport mechanics; the registry remains game policy behind the `ServerSession` façade.
- Landed subscription-lifecycle behavior — this bookkeeping refactor must preserve the client/server slot-state contracts documented in `Engine/Source/Network/Client/AGENTS.md` and `Engine/Source/Network/Server/AGENTS.md`; do not fold further slot state-machine changes in here.
- `ForwardErrorCorrection.txt` (Features) would add a fifth per-slot vector; `SlotState` makes that a field add, but implementing FEC is not this plan.
- `AckState`/`ClientCoordSubscription` field layouts stay identical (moved into `SlotState`, not redesigned).

## Acceptance criteria

- Exactly one relink routine exists; connect and load both call it.
- No game code indexes `authorizedCoords` by owned-player position — that mapping comes from the registry, which cannot desynchronize by construction (removal is one call, not two coordinated erases).
- `OnClientConnected` performs the dead-flagship shift.
- `ClientConnection` holds one slot vector; `FreeSlot` has no bounds guards.
- Server build compiles; assign/spawn/death/transfer/reconnect/load paths behave identically (owned set, assign send order creation-stable).

## Coordination

- Prerequisite satisfied: subscription-lifecycle hardening is landed. Preserve its slot-state behavior while moving registry bookkeeping; the permanent contract is in `Engine/Source/Network/Client/AGENTS.md`, `Engine/Source/Network/Server/AGENTS.md`, and `Documents/Architecture/Network.md`.

## Notes

- **Invariant exposure**: server-side bookkeeping only — no CRC/determinism sim path, no `.pack`/`kiVersion`, no wire bytes. **But relink governs which client owns which player and the assign/spawn send order** (creation-order-stable by global-id sort); a registry bug mis-assigns ownership on reconnect/load, so treat ownership/assign-order parity as the correctness bar (Risks 2-3). No client-visible protocol change.
- Allocation-tracking: registry growth happens under the existing `ScopedSuppressAllocationTracking` scopes in `ServerClientManager` and `ServerSession::ResetClientsForLoad`; keep those guards on registry calls per the manager guard convention.
- Pre-staged grill decision: does the registry **store** `coord` (a second copy alongside `authorizedCoords`, kept in sync by `Add`/`UpdateCoord`/`RemoveAt`) or hold only `globalId` and derive coord from `authorizedCoords` at the same index? Storing the pair is what eliminates the parallel-vector invariant (the whole point); the alternative keeps the coupling but centralizes it. Recommend storing the pair and reducing `authorizedCoords` to the engine's subscription-adjacency list only.
