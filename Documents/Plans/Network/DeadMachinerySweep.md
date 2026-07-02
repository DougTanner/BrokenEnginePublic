# Dead-Machinery Sweep (Network reconciliation + server client/fleet managers)

## Context

A six-agent audit of `Engine/Source/Network` and `Projects/BrokenEngineSandbox/Source/Network` surfaced six independent dead or vestigial mechanisms — each verified by repo-wide grep. All are residue of superseded designs (destroy-on-disconnect, generation numbering, a mis-modeled replay throttle) that mint/store/pass state nothing consumes. Removing them shrinks the trickiest subsystem's surface and deletes three actively misleading mechanisms. Items (a)–(e) are behavior-preserving deletions; (f) is a decision item.

## Design (each item independent)

### (a) Delete the never-populated player-destroy pipeline

`ServerClientManager::mPendingPlayerDestroys` (`ServerClientManager.h:39`, struct `PendingPlayerDestroy` `:16`) is read and cleared but has **zero push sites** repo-wide. `DetectPlayerDeaths` — the obvious would-be producer — handles death via `PlayerStateWireType::kDied` + parallel-vector erase and never enqueues. So the whole consumer chain is unreachable:
- `ServerSession::EnsureDestroyCoords` (`ServerSession.cpp:310-319`) + its call at `ComputeActiveSet` (`:347`)
- the destroy-injection loop in `ServerBroadcaster::BuildFrameInputs` (`ServerBroadcaster.cpp:52-65`, the only `kDestroyPlayer` construction site, `:62`)

Delete: `mPendingPlayerDestroys`, `PendingPlayerDestroy` (only used by this path), `EnsureDestroyCoords` (+ the `ComputeActiveSet` call), the `ServerBroadcaster` injection loop, and the corresponding `.clear()` in `ServerClientManager::ResetState` (`:345`).

**KEEP (reserved, do not delete):** the `kDestroyPlayer` enumerator (`StatusChange.h:14`), `DestroyPlayerData` (`:53`), its `DefaultDataForType`/name-table arms, the `NetworkSerialization.cpp` serialize/deserialize cases (`:186-188`, `:318-320`), and the frame-tick consumer `Players::ProcessSpawnStatusChanges` (`Players.cpp:267-283`). Rationale below (Notes / grill).

### (b) Delete write-only `ServerFleetManager::mPlayerToGuid`

`mPlayerToGuid` (`ServerFleetManager.h:93`) is `insert_or_assign`-written in `OnPlayerSpawned` (`:282`), `OnResetForLoad` (`:396`), and the free `::game::ReadFleetData` via `ReadFleet` (`ServerFleetManagerUtils.cpp:100`); cleared in `ResetState` (`:549`). **Never read** (no `find`/`at`/`contains`/`operator[]`). It is rebuilt from fleet-member `globalPlayerId`s on read (no dedicated serialized block), so removal does not change save format. Delete the member, all four write/clear sites, and the `rPlayerToGuid` output parameter from `ReadFleetData` (`ServerFleetManagerUtils.{h,cpp}` — decl `:14`, def `:125`) and its call at `ServerFleetManager.cpp:519`.

### (c) Delete the four dead `ReconcileInputs` fields + the self-max no-op

`ReconcileInputs` (`ClientReconciler.h:30-38`) — only `iTargetTick`/`iJitterUs` drive the pipeline. Dead: `confirmedClientState` (`ReconcileUpdateClientState` uses `rInOutState`, never `inputs.confirmedClientState`), `playerAlignment`, `alignments` (all reads are `Frame::postRender.*`). `uiNextFrameId` is read only at `ClientReconciler.cpp:212` — `SetNextFrameId(std::max(NextFrameId(), inputs.uiNextFrameId))` — a guaranteed no-op self-max (`inputs.uiNextFrameId` was captured from `NextFrameId()` at `:47`; `muiNextFrameId` is monotonic and not minted during `Run()`). Delete all four fields, their assignments at `ClientReconciler.cpp:46-47,51-52`, and line `:212` entirely. The self-max also misleadingly implies replay mints frame IDs, contradicting the "clients never mint" invariant — its removal is a clarity win.

### (d) Delete the vestigial generation-numbering mechanism

`engine::CoordFrames::uiGeneration` (`Engine/Source/GameBase.h:81`, reset in `ResetClientState()` `:116`) plus `ClientReconciler::NextGeneration()` (`ClientReconciler.h:117`) and its backing `muiNextGeneration` (`:131`, reset `ClientReconciler.cpp:226`). The minted value is never consumed — the sole `uiGeneration` read is a `== 0` first-arrival sentinel at `ClientDataReceiver.cpp:77-79` that only gates minting the next unused id. Delete the field (from `CoordFrames` + `ResetClientState`), `NextGeneration()`, `muiNextGeneration`, and the entire `:77-79` mint block (no code depends on a "first arrival" branch here).

### (e) Reduce `FindMatchingPlayerInCoord` to a `bool` presence check

`FindMatchingPlayerInCoord` (`ReconcileReplayClientState.cpp:13-55`) returns `std::optional<engine::global_id_t>` but the only non-null value it returns (`:48`) is the unmodified input `globalPlayerId`; the caller (`:143-147`) self-assigns `*matchedId` back to `clientState.clientGlobalPlayerId`. Change the signature to `bool` (found/not-found), keep the `kVerbose` logging side effects, and drop the no-op assignment at the call site.

### (f) Replay throttle — decision item (delete vs. make effective)

The jitter-adaptive replay throttle in `ReconcileReplayCoord` (`ReconcileReplayTick.cpp:217-253`) caps validated-replay ticks to `iMaxReplay`, but `RunPrimaryReplay` then **unconditionally** calls `ReconcileCatchUpCoord(…, rInputs.iTargetTick, …)` (`ReconcileReplay.cpp:339`), which forward-sims the *remainder* to the same target via `ReconcileForwardStepCoord` → the same `ReconcileRunTickCoord`/`RunFrameTick`, folding the same `serverUpdates` StatusChanges. So the throttle does **not** bound per-frame `RunFrameTick` cost — it only defers CRC-validate + `serverUpdates.erase` on the truncated ticks to next frame's fast path (slower `iConfirmedTick` advance + extra fast-path walks). The true per-frame bound is the ring budget (`iBudget = kiNetworkBufferSize - iReplayWriteCount`), not jitter. Three docs describe it as overload protection (`Network.md` §Client-per-Frame "Adaptive replay throttle"; both client Network CLAUDE.mds) — inaccurate.

Two options (pre-stage for grill):
- **A — delete** the throttle: `iMaxReplay` computation (`:222-247`), the delta-gated logging block (`:248-253`), the `iReplayCount >= iMaxReplay` break, and the now-inert logging fields `iLastLoggedMaxReplay`/`bLastLoggedGapOverride` on `engine::CoordFrames` (`GameBase.h:101-102`, reset `:124-125`). Replay runs to `iMaxConsecutive`, catch-up covers the rest — net sim cost unchanged, `iConfirmedTick` advances faster. Also correct the three docs.
- **B — make catch-up honor the cap**: bound `ReconcileCatchUpCoord`'s forward sim by the same per-frame budget so truncation genuinely reduces `RunFrameTick` calls. Higher risk (changes steady-state clock catch-up behavior under loss).

## Critical files

- Server: `Projects/.../Network/Server/ServerClientManager.{h,cpp}`, `ServerBroadcaster.cpp`, `ServerSession.cpp`, `ServerFleetManager.{h,cpp}`, `ServerFleetManagerUtils.{h,cpp}` — (a), (b)
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
- **Grill decisions (two):**
  1. **(a) enum disposition** — keep `kDestroyPlayer` reserved (recommended; zero risk, no bump) vs. remove it with a `Frame::kiVersion` bump (if a bump is already scheduled — coordinate with `Frame/CrcVersionGateBump.md`). Default: keep reserved.
  2. **(f) throttle** — delete (A, recommended) vs. make catch-up honor the cap (B).
- (d) removes a field from the `BT_CLIENT` `CoordFrames` span in `Engine/Source/GameBase.h` — client-only, no server guard interaction.
