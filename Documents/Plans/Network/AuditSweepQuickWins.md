# Network Audit-Sweep Quick Wins Batch

## Context

The six-agent audit of `Engine/Source/Network` + `Projects/BrokenEngineSandbox/Source/Network` surfaced a batch of small, independent fixes — pass-through collapses, guard/ordering nits, a save-byte determinism gap, a stale doc line, and a file rename — each verified, none big enough for its own plan. One mechanical session; the only judgment calls are items 10 and 13c (flagged inline). Dead-mechanism deletions from the same audit live separately in `Network/DeadMachinerySweep.md`; the pause/reset semantic fixes in `Network/ServerPauseAndResetSemantics.md`.

## Design (checklist — each item is independent)

1. **Collapse the six `Send*Request` methods** — `ClientSession::SendUpdatePlayerRequest`/`SendCreateFleetRequest`/`SendDeleteFleetRequest`/`SendSpawnIntoFleetRequest`/`SendRespawnInFleetRequest`/`SendFleetNavigationDelayRequest` (`ClientSession.cpp:396-502`) share identical boilerplate: `if (!CanSend()) return;` → optional `common::LogTickScope` when `miLogTickCounter < 0` → one `LOG(kNetwork, …)` → `SendSimplePacket(type, kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, args…)`. Only packet type, log text/level, and forwarded args differ. Extract one variadic private helper (`~70` lines removed).

2. **Collapse fleet-data pass-throughs.** `ServerFleetManager::SendFleetSync` (`ServerFleetManager.cpp:186-189`) is a pure forward to `::game::SendFleetSync`. `WriteFleetData`/`ReadFleetData` are triple hops: `ServerSession::WriteFleetData` (`ServerSession.cpp:588-591`) → `ServerFleetManager::WriteFleetData` (`:509-512`) → free `::game::WriteFleetData`, and identically for Read. Collapse the redundant middle layer (keep the `ScopedSuppressAllocationTracking` wrap that `ServerFleetManager::ReadFleetData` adds — fold it into whichever layer survives). Coordinate with item 7's rename.

3. **Pass the GUID down instead of reverse-scanning.** `ServerFleetManager::FindGuidForClient` (`ServerFleetManager.cpp:21-31`) linearly scans `mGuidToClientId` per request. Callers `SendFleetSyncToClient` (`:174`), `OnPlayerSpawned` (`:263`), `LookupFleetWantedCoord` (`:441`) are reached from request handlers that already hold `pClient->clientGuid` (`engine::ClientConnection::clientGuid`, `Server.h:41`). Thread the guid down and drop the scan where the caller has it in hand.

4. **Deterministic fleet-block save output.** `::game::WriteFleetData` (`ServerFleetManagerUtils.cpp:110`) iterates `rFleets` — an `unordered_map<ClientGuid, …>` — writing serialized bytes in hash-iteration order, so fleet-block save bytes are iteration-order dependent. This diverges from the codebase's grid-save "coords sorted by key for deterministic output" convention (documented for grid cells, not the fleet block — so a divergence, not a documented-property break). Sort owners by `ClientGuid` before writing. Runtime determinism is unaffected — `ReadFleetData` rebuilds all maps via `insert_or_assign`.

5. **Remove the always-origin `spawnCoord` flexibility.** `QueueSpawnForClient`'s `spawnCoord` param and `ClientSpawnInfo::spawnCoord` (`ServerClientManager.h:11,20`) are always `engine::kOriginCoord` (both call sites: `ServerFleetManager.cpp:115,151`); `BuildFrameInputs` consumes it as `rInfo.spawnCoord` (`ServerBroadcaster.cpp:48-49`) and `RefreshPreSpawnSnapshot`/`FinalizeNewClients` already hardcode origin (`ServerBroadcaster.cpp:105-107`). Drop the parameter and field (YAGNI), make the origin assumption explicit at the spawn sites.

6. **Rename the mislabeled `mSpawns`.** `ServerBroadcaster::mSpawns` (`ServerBroadcaster.h:30`) holds **all** non-transfer status changes — spawns, `kDestroyPlayer`, weapon toggles (`kUpdatePlayer`), fleet updates (`kUpdateFleet`) — copied from every non-empty `mFrameInputs` entry (`ServerBroadcaster.cpp:96-102`). Rename to `mBroadcastStatusChanges` (+ `ClearSpawns`/`mSpawns` references) and fix the "spawns only" comment at `:95`.

7. **Rename `ServerFleetManagerUtils.{h,cpp}` → `ServerFleetSerialization.{h,cpp}`.** The unit declares exactly three serialization free functions (`SendFleetSync` wire, `WriteFleetData`/`ReadFleetData` disk) — a coherent codec, not a grab-bag. Update all four server-project listings: `BrokenEngineSandboxServer.vcxproj:398,509` and `.vcxproj.filters:165,521` (server-only, `BT_SERVER`-gated — not in the client project).

8. **Simplify the always-true post-check.** `engine::ClientSessionBase::UnsubscribeStaleCoords` (`Engine/Source/Network/Client/ClientSessionBase.cpp:167-196`) checks `if (rSlots.at(i).eState == kUnsubscribing)` (`:189`) after `SendUnsubscribe`, which unconditionally sets `eState = kUnsubscribing` (`ClientSend.cpp:138`) — observed here through the aliased `rSlots` ref (bound to the peer's `mCoordSlots` via `GetCoordSlots()`). The check is always true. Simplify to unconditional erase, or add the aliasing comment if the branch is kept for clarity.

9. **Guard-before-mutate in `SendUnsubscribe`.** `engine::Client::SendUnsubscribe` (`Engine/Source/Network/Client/ClientSend.cpp:136-140`) mutates slot state as its first statement with **no** `CanSend()` guard, unlike every sibling `Send*` (`SendAck`/`SendSubscribe`/`SendResyncRequest`/…) which guards first. Add the guard before the state mutation.

10. **Two redundancies in the client reconcile path** (`ClientSession.cpp`):
    - `Reconcile` re-checks `!IsStalled()` at `:203` after an early `if (IsStalled()) return;` at `:193`; nothing between (`:198-202`) mutates stalled state. Drop the redundant check.
    - `UpdateSubscriptions()` is called twice per frame: once in `PollNetwork` (`:116`, inside `Poll()`) and again in `GameBase::ClientUpdate` (`GameBase.cpp:57`) **after** `UpdateDesiredCoords(kPollTick)` (`:56`). The first acts on the previous frame's desired set; the kPollTick recompute + second call subsume it. **Verify first:** the in-poll call currently runs after full-state adoption (`ApplyReceivedFullStates`, `:115`) but before `ApplyReceivedUpdates` (`:117`); confirm the full-state-adoption-before-subscribe ordering doesn't depend on the in-poll placement before removing the `:116` call. If the ordering is load-bearing, keep both and document why.

11. **Simplify the `ServerSession` destructor.** The manual member-reset order (`ServerSession.cpp:34-37`) reproduces the implicit reverse-declaration order (`ServerSession.h:50-53`), and the `if (gpServerSession == this)` guard (`:38-41`) is redundant given the ctor's `ASSERT(gpServerSession == nullptr)` singleton invariant (`:23`). Drop the manual resets; keep only `gpServerSession = nullptr`. (Belt-and-suspenders note: `ASSERT` compiles out in release — if the guard is retained for that reason, comment it; the game `ClientSession` dtor uses the identical pattern.)

12. **Doc fix — stale forward-sim description.** `Documents/Architecture/Network.md:41` ("Client per Frame" → "Forward sim") says empty-input sim "stops one tick before the lowest pending server update." The implementation deliberately advances **through** pending updates, folding their StatusChanges unvalidated (`ReconcileForwardStepCoord`, `ReconcileReplayTick.cpp:329-354`) and promoting via next frame's CRC fast path. Code is right; the doc describes a removed design. Rewrite that bullet to match.

13. **`PlayerEvents.cpp` polish:**
    - **(a)** Validate `iFlagshipIndex` at the parse boundary in `ParseFleetSyncPayload` (`:96`, read raw via `engine::ReadInt64` from network — a trust boundary) against `[0, iMemberCount)`, so consumers (`FleetSelection.cpp:153`, `ServerFleetManager.cpp:290,424,429`, `FleetNavigationController.cpp:39,130`) stop re-guarding. Invariant belongs at the boundary.
    - **(b)** Replace `.at()` with `operator[]` on the just-resized in-range indices in `ParseFleetSyncPayload` (`:92` after `resize(iFleetCount)` `:84`; `:108` after `members.resize(iMemberCount)` `:103`) — no defensive bounds check needed between our own lines.
    - **(c)** *(consistency, optional)* Add explicit `#include "Pch.h"` at the top of `PlayerEvents.cpp` (currently relies on the force-included PCH). Note this is a repo-wide inconsistency — `ClientSession.cpp` also omits it while `ServerSession.cpp` includes it — so this is polish, not a correctness fix. Decide inline (default: add it).

## Critical files

- Game client: `Projects/.../Network/Client/ClientSession.cpp` (1, 10)
- Game server: `Projects/.../Network/Server/ServerFleetManager.{h,cpp}` (2, 3, 4), `ServerSession.cpp` (2, 11), `ServerBroadcaster.{h,cpp}` (5, 6), `ServerClientManager.h` (5), `ServerFleetManagerUtils.{h,cpp}` → renamed (2, 4, 7)
- Engine client: `Engine/Source/Network/Client/ClientSessionBase.cpp` (8), `ClientSend.cpp` (9)
- Game top-level: `Projects/.../Network/PlayerEvents.cpp` (13)
- vcxproj/filters: `Projects/.../Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj{,.filters}` (7)
- Docs: `Documents/Architecture/Network.md` (12)

## Out of scope

- Anything needing a real design decision — the `ServerBroadcaster` role-split (item 6 only renames), the send/receive pairing restructure (`Network/Architecture_WireFormatPairing.md`), and the dead-mechanism deletions (`Network/DeadMachinerySweep.md`).
- Item 3 does not change the `ClientGuid`↔`iClientId` map structures — it only avoids the scan where a caller already has the guid.
- Item 13 touches only the cited parse sites; no wider `PlayerEvents` refactor.

## Notes

- **Invariant exposure.** Item 4 changes save-file **byte** output only (fleet block ordering) — deterministic-output improvement, byte-identical runtime state on read, **no** CRC/`kiVersion`/wire change. Item 12 is docs-only. Item 13a is a trust-boundary tightening on network input (no wire change — malformed payloads are already rejected whole downstream). Item 7 is a file rename (no wire/format change). Everything else is client/server-internal refactor with compile-checked invariants. No determinism-math change anywhere.
- **Grill/judgment calls:** item 10's `UpdateSubscriptions` dedup (gated on the ordering-dependency check — keep both if load-bearing) and item 13c (add `Pch.h` vs. leave — default add). Both resolvable inline; no architectural decision.
- Item 7 (rename) invalidates any `#include "Network/Server/ServerFleetManagerUtils.h"` sites — refresh includes and the two vcxproj + two filters listings.
