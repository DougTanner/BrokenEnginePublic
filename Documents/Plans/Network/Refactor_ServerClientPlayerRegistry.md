# Refactor: Server Per-Client Player Registry

## Context

Server per-client state lives in three homes kept in lockstep by hand — the game-server constellation's principal structural weakness:

1. **Engine `ClientConnection`** (`Engine/Source/Network/Server/Server.h`): `authorizedCoords`, plus four parallel per-slot vectors (`coordSubscriptions`, `coordAckStates`, `prevResendCounts`, `resendLogCooldowns`), all `resize(kiMaxEnetCoordSlots)` at `Server::Connect` (`Server.cpp:129-132`).
2. **Game `ServerSession::mClientOwnedPlayerIds`** (`ServerSession.h:48`, `std::unordered_map<int64_t, std::vector<global_id_t>>`) — index-aligned with `authorizedCoords` (the documented "parallel-vector invariant", `Network/Server/AGENTS.md`). Mutated from four TUs (see Critical files).
3. **`ServerFleetManager`'s three GUID maps** (`ServerFleetManager.h`): `mFleets`, `mPlayerToGuid`, `mGuidToClientId`.

Two near-verbatim relink routines rebuild the game-side pair: `ServerClientManager::TryRelinkNewClient` (`ServerClientManager.cpp:122-166`) and `ServerSession::TryRelinkClientForLoad` (`ServerSession.cpp:542-586`) — identical local `RelinkEntry` struct, identical GUID-match scan over `mCoordFrames`, identical global-id sort lambda, identical reserve/push/`SendAssignPlayer`/`SendPlayerState` loop; they differ only in log text (`kNetwork`/`kVerbose` vs `kDefault`/`kDebug`) and self-vs-`gpServerSession` qualification.

Related fleet-manager duplication: `ServerFleetManager::OnClientConnected`'s member-refresh loop (`ServerFleetManager.cpp:338-356`) duplicates the body of `ResetFleetForLoad` (`:406-421`) — same `bAlive` recompute + `authorizedCoords`-by-index coord refresh. `OnClientConnected` **lacks** the dead-flagship shift that `ResetFleetForLoad` has (`:423-428`); today the asymmetry is harmless only because `DetectDisconnectedPlayerDeaths` ordering happens to cover it — an asymmetry trap.

Engine-side, `ClientConnection::FreeSlot` (`Server.h:85-100`) carries defensive `if (iSlot < std::ssize(prevResendCounts))` / `resendLogCooldowns` guards against a size mismatch that cannot occur (all four vectors are resized together, once) — exactly the defensive-validation-between-our-own-code the house rules prohibit, and evidence the four-parallel-vector shape is fragile.

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

`authorizedCoords` on `ClientConnection` stays the engine's authority for subscription adjacency (`ServerReceive.cpp:346`, `ServerDisplay.cpp:150` read it); the registry becomes the single source for the *owned-player→coord* mapping that game code currently derives by zipping `mClientOwnedPlayerIds` against `authorizedCoords` by index. Decide at grill whether the registry stores coord (duplicating `authorizedCoords`) or the two stay linked — see Notes.

### Game: shared fleet-refresh helper

Extract the `OnClientConnected`/`ResetFleetForLoad` member-refresh body (`bAlive` recompute + coord refresh from the registry) into one helper both call, and give `OnClientConnected` the **same dead-flagship shift** `ResetFleetForLoad` performs — removing the ordering-dependent asymmetry.

### Engine: collapse the four parallel per-slot vectors into `struct SlotState`

Replace `coordSubscriptions` / `coordAckStates` / `prevResendCounts` / `resendLogCooldowns` on `ClientConnection` with a single `std::vector<SlotState>` (`SlotState { ClientCoordSubscription subscription; AckState ack; int64_t iPrevResendCount; int64_t iResendLogCooldown; }`), sized once in `Connect`. Update `FindSlotForCoord` / `AllocateSlot` / `FreeSlot` and every `.at(i)` accessor across `ServerSend.cpp` (26), `ServerReceive.cpp` (14), `ServerSessionBase.cpp` (3), and the game `coordSubscriptions` reads (`ServerSession.cpp` `ResetClientsForLoad` slot-free loop, `ServerTransferManager.cpp`) to `slots.at(i).<field>`. **Delete** the `iSlot < std::ssize(...)` guards in `FreeSlot` — with one vector the sizes cannot diverge.

## Critical files

**Game — every `mClientOwnedPlayerIds` / `authorizedCoords` mutation site (must route through the registry):**
- `ServerClientManager.cpp` — `TryRelinkNewClient` push (`:154-162`, folds into `RelinkFromFrames`); `FinalizeNewClients` spawn push (`:215-216`); `Disconnects` erase (`:250`, → `Remove`); `DetectPlayerDeaths` paired erase (`:317-318`, → `RemoveAt`).
- `ServerTransferManager.cpp` — `HarvestTransfers` in-place coord write (`:173-178`, → `UpdateCoord`).
- `ServerSession.cpp` — `ResetClientsForLoad` clear (`:511-513`, → `Clear`); `TryRelinkClientForLoad` (`:542-586`, folds into `RelinkFromFrames`).

**Game — read sites (become registry queries):**
- `ServerFleetManager.cpp` — `OnClientConnected` (`:331,338-356`), `OnResetForLoad`/`ResetFleetForLoad` (`:376,406-421`).
- `ServerBroadcaster.cpp` — `:182,195`.
- `ServerSession.cpp` — `Disconnects` log (`:238`).

**Game — owner / new type:**
- `ServerSession.h` — replace `mClientOwnedPlayerIds` member with the registry; new `ClientPlayerRegistry.{h,cpp}` (game vcxproj + `BT_SERVER` guard + filters).

**Engine — `SlotState` merge:**
- `Server.h` — `ClientConnection` struct + `FindSlotForCoord`/`AllocateSlot`/`FreeSlot` (16 hits); `Server.cpp:129-132` Connect resizes → one; `ServerSend.cpp` (26), `ServerReceive.cpp` (14), `ServerSessionBase.cpp` (3) accessors.

## Out of scope

- The three `ServerFleetManager` GUID maps — named as context, but consolidating them is a separate follow-up (this plan only *shares the refresh helper* and reads the new registry from the fleet manager).
- Any wire-format change: assign/spawn/state packets and their send order are untouched; this is server bookkeeping only.
- Subscription-lifecycle races (owned by `SubscriptionLifecycleRaceHardening.md`) — do not fold slot state-machine fixes in here.
- `ForwardErrorCorrection.txt` (Features) would add a fifth per-slot vector; `SlotState` makes that a field add, but implementing FEC is not this plan.
- `AckState`/`ClientCoordSubscription` field layouts stay identical (moved into `SlotState`, not redesigned).

## Acceptance criteria

- Exactly one relink routine exists; connect and load both call it.
- No game code indexes `authorizedCoords` by owned-player position — that mapping comes from the registry, which cannot desynchronize by construction (removal is one call, not two coordinated erases).
- `OnClientConnected` performs the dead-flagship shift.
- `ClientConnection` holds one slot vector; `FreeSlot` has no bounds guards.
- Server build compiles; assign/spawn/death/transfer/reconnect/load paths behave identically (owned set, assign send order creation-stable).

## Coordination

- `Documents/Plans/Network/SubscriptionLifecycleRaceHardening.md`: never interleave the shared slot-state work; this plan's structured dependency requires the hardening first.

## Notes

- **Invariant exposure**: server-side bookkeeping only — no CRC/determinism sim path, no `.pack`/`kiVersion`, no wire bytes. **But relink governs which client owns which player and the assign/spawn send order** (creation-order-stable by global-id sort); a registry bug mis-assigns ownership on reconnect/load, so treat ownership/assign-order parity as the correctness bar (Risks 2-3). No client-visible protocol change.
- Allocation-tracking: registry growth happens under the existing `ScopedSuppressAllocationTracking` scopes at the current mutation sites (`FinalizeNewClients`, `Disconnects`, `DetectPlayerDeaths`, `ResetClientsForLoad`); keep those guards on the registry calls per the manager guard convention.
- Pre-staged grill decision: does the registry **store** `coord` (a second copy alongside `authorizedCoords`, kept in sync by `Add`/`UpdateCoord`/`RemoveAt`) or hold only `globalId` and derive coord from `authorizedCoords` at the same index? Storing the pair is what eliminates the parallel-vector invariant (the whole point); the alternative keeps the coupling but centralizes it. Recommend storing the pair and reducing `authorizedCoords` to the engine's subscription-adjacency list only.
