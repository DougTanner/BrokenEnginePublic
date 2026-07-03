# Dead-Machinery Sweep (Network reconciliation + server client/fleet managers)

## Context

A six-agent audit of `Engine/Source/Network` and `Projects/BrokenEngineSandbox/Source/Network` surfaced six independent dead or vestigial mechanisms — each verified by repo-wide grep. All are residue of superseded designs (destroy-on-disconnect, generation numbering, a mis-modeled replay throttle) that mint/store/pass state nothing consumes. Removing them shrinks the trickiest subsystem's surface and deletes three actively misleading mechanisms. Items (a)–(e) are behavior-preserving deletions; (f) deletes the ineffective throttle (decided — see Notes); (g), added by the 2026-07-03 Frame review sweep, is a keep-reserved + document item for the issuer-less `kRespawnPlayer` sibling.

## Design (each item independent)

### (a) Delete the never-populated player-destroy pipeline

`ServerClientManager::mPendingPlayerDestroys` (`ServerClientManager.h:39`, struct `PendingPlayerDestroy` `:16`) is read and cleared but has **zero push sites** repo-wide. `DetectPlayerDeaths` — the obvious would-be producer — handles death via `PlayerStateWireType::kDied` + parallel-vector erase and never enqueues. So the whole consumer chain is unreachable:
- `ServerSession::EnsureDestroyCoords` (`ServerSession.cpp:310-319`) + its call at `ComputeActiveSet` (`:347`)
- the destroy-injection loop in `ServerBroadcaster::BuildFrameInputs` (`ServerBroadcaster.cpp:52-65`, the only `kDestroyPlayer` construction site, `:62`)

Delete: `mPendingPlayerDestroys`, `PendingPlayerDestroy` (only used by this path), `EnsureDestroyCoords` (+ the `ComputeActiveSet` call), the `ServerBroadcaster` injection loop, and the corresponding `.clear()` in `ServerClientManager::ResetState` (`:345`).

**KEEP (reserved, do not delete):** the `kDestroyPlayer` enumerator (`StatusChange.h:14`), `DestroyPlayerData` (`:53`), its `DefaultDataForType`/name-table arms, the `NetworkSerialization.cpp` serialize/deserialize cases (`:186-188`, `:318-320`), and the frame-tick consumer `Players::ProcessSpawnStatusChanges` (`Players.cpp:267-283`). Rationale below (Notes).

### (b) Delete write-only `ServerFleetManager::mPlayerToGuid`

`mPlayerToGuid` (`ServerFleetManager.h:93`) is `insert_or_assign`-written in `OnPlayerSpawned` (`:282`), `OnResetForLoad` (`:396`), and the free `::game::ReadFleetData` via `ReadFleet` (`ServerFleetManagerUtils.cpp:100`); cleared in `ResetState` (`:549`). **Never read** (no `find`/`at`/`contains`/`operator[]`). It is rebuilt from fleet-member `globalPlayerId`s on read (no dedicated serialized block), so removal does not change save format. Delete the member, all four write/clear sites, and the `rPlayerToGuid` output parameter from `ReadFleetData` (`ServerFleetManagerUtils.{h,cpp}` — decl `:14`, def `:125`) and its call at `ServerFleetManager.cpp:519`.

### (c) Delete the four dead `ReconcileInputs` fields + the self-max no-op

`ReconcileInputs` (`ClientReconciler.h:30-38`) — only `iTargetTick`/`iJitterUs` drive the pipeline. Dead: `confirmedClientState` (`ReconcileUpdateClientState` uses `rInOutState`, never `inputs.confirmedClientState`), `playerAlignment`, `alignments` (all reads are `Frame::postRender.*`). `uiNextFrameId` is read only at `ClientReconciler.cpp:212` — `SetNextFrameId(std::max(NextFrameId(), inputs.uiNextFrameId))` — a guaranteed no-op self-max (`inputs.uiNextFrameId` was captured from `NextFrameId()` at `:47`; `muiNextFrameId` is monotonic and not minted during `Run()`). Delete all four fields, their assignments at `ClientReconciler.cpp:46-47,51-52`, and line `:212` entirely. The self-max also misleadingly implies replay mints frame IDs, contradicting the "clients never mint" invariant — its removal is a clarity win.

### (d) Delete the vestigial generation-numbering mechanism

`engine::CoordFrames::uiGeneration` (`Engine/Source/GameBase.h:81`, reset in `ResetClientState()` `:116`) plus `ClientReconciler::NextGeneration()` (`ClientReconciler.h:117`) and its backing `muiNextGeneration` (`:131`, reset `ClientReconciler.cpp:226`). The minted value is never consumed — the sole `uiGeneration` read is a `== 0` first-arrival sentinel at `ClientDataReceiver.cpp:77-79` that only gates minting the next unused id. Delete the field (from `CoordFrames` + `ResetClientState`), `NextGeneration()`, `muiNextGeneration`, and the entire `:77-79` mint block (no code depends on a "first arrival" branch here).

### (e) Reduce `FindMatchingPlayerInCoord` to a `bool` presence check

`FindMatchingPlayerInCoord` (`ReconcileReplayClientState.cpp:13-55`) returns `std::optional<engine::global_id_t>` but the only non-null value it returns (`:48`) is the unmodified input `globalPlayerId`; the caller (`:143-147`) self-assigns `*matchedId` back to `clientState.clientGlobalPlayerId`. Change the signature to `bool` (found/not-found), keep the `kVerbose` logging side effects, and drop the no-op assignment at the call site.

### (f) Delete the ineffective replay throttle

The jitter-adaptive replay throttle in `ReconcileReplayCoord` (`ReconcileReplayTick.cpp:217-253`) caps validated-replay ticks to `iMaxReplay`, but `RunPrimaryReplay` then **unconditionally** calls `ReconcileCatchUpCoord(…, rInputs.iTargetTick, …)` (`ReconcileReplay.cpp:339`), which forward-sims the *remainder* to the same target via `ReconcileForwardStepCoord` → the same `ReconcileRunTickCoord`/`RunFrameTick`, folding the same `serverUpdates` StatusChanges. So the throttle does **not** bound per-frame `RunFrameTick` cost — it only defers CRC-validate + `serverUpdates.erase` on the truncated ticks to next frame's fast path (slower `iConfirmedTick` advance + extra fast-path walks). The true per-frame bound is the ring budget (`iBudget = kiNetworkBufferSize - iReplayWriteCount`), not jitter. Three docs describe it as overload protection (`Network.md` §Client-per-Frame "Adaptive replay throttle"; both client Network CLAUDE.mds) — inaccurate.

Decision (2026-07-03): **delete (A)** — remove the `iMaxReplay` computation (`:222-247`), the delta-gated logging block (`:248-253`), the `iReplayCount >= iMaxReplay` break (`:258-261`), and the now-inert logging fields `iLastLoggedMaxReplay`/`bLastLoggedGapOverride` on `engine::CoordFrames` (`GameBase.h:101-102`, reset `:124-125`). Replay runs to `iMaxConsecutive`, catch-up covers the rest — net sim cost unchanged, `iConfirmedTick` advances faster. Also correct the three docs. Deleting the throttle removes the `ReconcileReplayCoord Throttle` kVerbose log with it (fine — the mechanism it reports is gone; the ceiling-stall / render-clock / replay-burst smoothness logs in `GameBase.cpp` / `ClientReconciler.cpp` are separate and stay).

Considered and rejected: **B — make catch-up honor the cap** (bound `ReconcileCatchUpCoord`'s forward sim by the same per-frame budget). Capping forward-sim below the sim tick under loss/jitter stalls the ring tail behind `iTargetTick`, which starves the render clock (top-clamp freeze) — the exact stall-then-burst artifact the sim-ceiling-slack / render-behind work eliminated. Violates the smoothness-over-latency priority (`Engine/Source/Network/CLAUDE.md` hub conventions); kChina measurements show zero ≥8-tick single-frame re-sim bursts, so there is no CPU-bounding problem for B to solve.

### (g) Document the issuer-less `kRespawnPlayer` path as reserved (2026-07-03 Frame-sweep addition)

`kRespawnPlayer` (`StatusChange.h:9`) has **zero issuers repo-wide** — it appears only in the enum + name table (`StatusChange.h:31`), the wire codec (`NetworkSerialization.cpp:172,304`), and the consumer branch in `Players::ProcessSpawnStatusChanges` (`Players.cpp:333-349`, spawning from an empty `TransferData` per `StatusChange.h:169`). The only spawn issuer is `ServerBroadcaster::BuildFrameInputs`, which always sends `kSpawnPlayer` with a fresh `GenerateGlobalId()`. Same family as (a)'s `kDestroyPlayer`: **keep the enumerator, codec arms, and consumer branch reserved** (identical wire/save-byte rationale — see Notes), but add a comment at the `Players.cpp` consumer documenting the latent trap: the respawn branch does not extract `SpawnPlayerData`, so a wired-up respawn spawns with `globalPlayerId` 0 — invisible to **both** uuid re-resolution scans (`ServerBroadcaster.cpp:220`, `FleetNavigationController.cpp:161`), i.e. it would never receive fleet updates or weapon toggles. Any future respawn implementation must carry a real global id (or reuse `kSpawnPlayer`).

## Critical files

- Server: `Projects/.../Network/Server/ServerClientManager.{h,cpp}`, `ServerBroadcaster.cpp`, `ServerSession.cpp`, `ServerFleetManager.{h,cpp}`, `ServerFleetManagerUtils.{h,cpp}` — (a), (b)
- Frame: `Projects/.../Frame/Collections/Players/Players.cpp` (`ProcessSpawnStatusChanges` respawn-branch comment) — (g)
- Client: `Projects/.../Network/Client/ClientReconciler.{h,cpp}`, `ClientDataReceiver.cpp`, `ReconcileReplayClientState.cpp`, `ReconcileReplayTick.cpp`, `ReconcileReplay.cpp` — (c), (d), (e), (f)
- Engine: `Engine/Source/GameBase.h` (`CoordFrames::uiGeneration`, and for (f)-A the two logging fields) — (d), (f)
- Docs (for (f)-A only): `Documents/Architecture/Network.md`, `Projects/.../Network/Client/CLAUDE.md`, `Projects/.../Network/CLAUDE.md`

## Out of scope

- **Removing the `kDestroyPlayer` enumerator or its wire/frame decode** — see Notes; it is a wire+save format value.
- The `Send*Request` boilerplate, pass-through collapses, and other mechanical fixes — those live in `Network/AuditSweepQuickWins.md`.
- Any change to the frame-tick destroy handler's *behavior* — (a) only removes the unreachable producer.
- The `ServerBroadcaster` role-split (its `mSpawns` misnomer is handled in `AuditSweepQuickWins.md` item 6).

## Acceptance criteria

- No `mPendingPlayerDestroys`, `mPlayerToGuid`, `NextGeneration`, `uiGeneration`, or dead `ReconcileInputs` field survives a repo-wide grep.
- `kDestroyPlayer` enumerator and its serialization/frame-tick arms remain compilable and untouched.
- `FindMatchingPlayerInCoord` returns `bool`.
- Save/replay files round-trip byte-identically (no `kiVersion` bump): (b) map is rebuilt-on-read, (d) is client-only runtime state, none touch serialized layout.

## Notes

- **Invariant exposure.** (a) is scoped precisely to AVOID touching wire/save format: `StatusChangeType` enumerator order **is** the wire + save byte value (`SerializeGroup`/`DeserializeStatusChangeBatch` prefix each group with the `uint8_t` enum value), yet — verified — `Frame::kiVersion` does **not** incorporate `StatusChangeType` and the enum carries no append-only comment, so deleting/reordering `kDestroyPlayer` would silently break replay/save compatibility with no version gate to catch it. Keeping it reserved is the safe, zero-version-bump choice. (c)–(f) touch client reconciliation cross-frame state; (a)–(e) are behavior-preserving; only (f)'s resolution changes behavior (faster `iConfirmedTick` advance under loss). No CRC/determinism-math change anywhere.
- **Decisions (both resolved by code analysis 2026-07-03 — no open grill items):**
  1. **Decision (2026-07-03): (a) keep `kDestroyPlayer` reserved** — do NOT remove it or bump `Frame::kiVersion`. Verified: `SerializeGroup` writes the raw enum byte (`NetworkSerialization.cpp:153`), and `Frame::kiVersion` (`Frame.cpp:11`) sums only the base constant + collection versions — no `StatusChangeType` term, so removal/reorder would silently break wire + replay compatibility with no gate. `Frame/CrcVersionGateBump.md` does schedule a 115→116 bump, but piggybacking removal onto it would couple two independent plans, expand this diff to `DestroyPlayerData`, both codec arms, name-table/`DefaultDataForType` arms, and the `Players.cpp:267` consumer, and still leave the enum unguarded for future edits. Reserved enumerator costs one line; KISS/YAGNI.
  2. **Decision (2026-07-03): (f) delete the throttle (A)** — verified `ReconcileCatchUpCoord(rWork, rInputs.iTargetTick, …)` at `ReconcileReplay.cpp:339` runs unconditionally after primary replay (absent desync early-return), forward-simulating the truncated remainder in the same frame, so the cap never bounds per-frame `RunFrameTick` cost. Option B (cap catch-up too) rejected: it stalls the ring tail behind `iTargetTick` under jitter — the stall-then-burst artifact the render-behind work eliminated — violating smoothness-over-latency, and kChina measurements show no re-sim burst problem for it to solve.
- (d) removes a field from the `BT_CLIENT` `CoordFrames` span in `Engine/Source/GameBase.h` — client-only, no server guard interaction.
