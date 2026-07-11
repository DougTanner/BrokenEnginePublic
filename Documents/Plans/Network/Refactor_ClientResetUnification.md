# Refactor: Unify Client Session Reset Paths

## Context

Four overlapping client-side reset paths each hand-maintain their own field checklist over the same session/game state. This is the audit's clearest accretion symptom and a known bug farm — `Engine/Source/Frame/CoordFrames` AGENTS.md already warns "any new per-coord counter must reset there or state leaks across sessions," and the same hazard applies to every clock/identity/subscription field these paths touch. Because each path resets a slightly *different* subset, adding a field means remembering up to four edit sites, and the paths already disagree.

The four paths (verified current source):

1. **`ClientSessionBase::DisconnectFromServerBase`** (`ClientSessionBase.cpp:56-79`) — full teardown; folds into `ClientSession` after `Refactor_SessionBaseCollapse.md`. Its caller `ClientSession::DisconnectFromServer` (`ClientSession.cpp:249-259`) adds reconciler + desync-manager reset and desired/sticky clears.
2. **`ClientSession::ResetForServerLoad`** (`ClientSession.cpp:336-387`) — server save-load re-bootstrap; connection persists.
3. **`ClientDesyncManager::ResetCoordStatesForResync`** (`ClientDesyncManager.cpp:106-115`) and **`ClientDesyncManager::Reset`** (`:117-121`) — soft-desync resync and desync-tracker reset.
4. **Initial-connect setup** — `ClientSession::ConnectToServer` (`ClientSession.cpp:237-241`) + ctor defaults (`ClientSession.cpp:32-41`, `miCoordSlots = kiDesiredCoordSlots`); relies on the session object carrying clean default-constructed state.

Concrete drift already present: the clock-sync fields (`miLatestServerTick`, `miClockError`, `miClockOffset`, `miClockTargetBehind`, `miCurrentTargetBehind`, `miLastLoggedClockTargetBehind`, `miLastPeriodicClockLogTick`, `miConsecutiveClockErrorFrames`, `miLastClockErrorLogTick`) are reset member-by-member. `DisconnectFromServerBase` resets six of them plus the three log-dedup trackers; `ResetForServerLoad` (`:347-351`) resets only four (`miLatestServerTick`, `miClockError`, `miCurrentTargetBehind`, `miConsecutiveClockErrorFrames`) and **silently leaves** `miClockOffset`, `miClockTargetBehind`, and the three `miLast*` log-tracker fields carrying stale values into the post-load session. These gate only log emission today, so the leak is currently benign — but it is exactly the class of drift that turns into a real desync the next time someone adds a clock field.

## Design

Replace all four with one entry point:

```cpp
enum class ResetReason : uint8_t { kConnect, kDisconnect, kServerLoad, kResync };
void ClientSession::ResetSession(ResetReason eReason);
```

Reason-gated steps, built on **grouped sub-structs reset via `= {}`**, mirroring the canonical `CoordFrames::ResetClientState()` convention (extend that convention, don't duplicate it). The key mechanic: give each field group in-class default-member-initializers (preserving the `-1` sentinels), so `mGroup = {}` value-initializes back to the declared baseline in one line — eliminating the member-by-member drift.

### Sub-structs to introduce (on `ClientSession`)

- **`ClockSyncState mClockSync`** — the nine `mi*` clock/log fields with their declared defaults (`miLatestServerTick = -1`, `miLastLoggedClockTargetBehind = -1`, `miLastPeriodicClockLogTick = -1`, `miLastClockErrorLogTick = -1`, the rest `= 0`). `mClockSync = {}` restores all nine. Callers referencing `miClockError` etc. become `mClockSync.miClockError` (mechanical rename across `ComputeClockCorrectionNs`, `Reconcile`, `ProfileManager` accessors).
- **`SubscriptionTracking mSubTracking`** — `mDesiredCoords`, `mUnwantedTimestamps`, `mSubscriptionQueue`. `mSubTracking = {}` clears all three; `mSubTracking.mUnwantedTimestamps.clear()` remains available for the sticky-only resync case.

`mSessionFlags`, the network peer / discovery scanner unique_ptrs, `mpReconciler`, and `mpDesyncManager` stay as-is; the reset table gates them per reason.

### Union field table (this is the plan's core — build/verify against source before coding)

| State group | kConnect | kDisconnect | kServerLoad | kResync | Current source |
|---|---|---|---|---|---|
| `mClockSync = {}` (all 9 clock/log fields) | ✓ | ✓ | ✓ (fixes drift: today only 4 of 9) | – (resync must not disturb clock) | `ClientSessionBase.cpp:61,71-77`; `ClientSession.cpp:347-351` |
| `mSessionFlags` — clear `kClockErrorDisconnect`, `kNoFreeSlotLogged` | ✓ | ✓ | ✓ | – | `ClientSessionBase.cpp:75,78`; `ClientSession.cpp:350` |
| `mSessionFlags` — clear `kServerDiscovered`, `kDiscoveryScanTimedOut` | ✓ | ✓ | – (connection persists) | – | `ClientSessionBase.cpp:69-70` |
| `mSubTracking = {}` (desired + queue + sticky) | ✓ | ✓ | ✓ | – | `ClientSessionBase.cpp:68`; `ClientSession.cpp:257-258,378,389-393` |
| `mSubTracking.mUnwantedTimestamps.clear()` only | – | (subsumed) | (subsumed) | ✓ | `ClientDesyncManager.cpp:114` (`ClearStickySubscriptions`) |
| Per-coord `ResetClientState()` on every `mCoordFrames` entry | – | ✓ | ✓ | ✓ | `ClientSessionBase.cpp:64-67`; `ClientSession.cpp:370-373`; `ClientDesyncManager.cpp:108-111` |
| `mCoordFrames.clear()` (drop entries after per-coord reset) | – | – | ✓ | – | `ClientSession.cpp:374` |
| `mpClientNetwork.reset()` + `mpDiscoveryScanner.reset()` (drop peer) | – | ✓ | – | – | `ClientSessionBase.cpp:62-63` |
| `mpClientNetwork->ResetAllSlots()` (keep peer, reset slots) | – | – | ✓ | – | `ClientSession.cpp:366` |
| `mpReconciler->Reset()` | – | ✓ | ✓ | ✓ | `ClientSession.cpp:254,377`; `ClientDesyncManager.cpp:113` |
| `mpDesyncManager->Reset()` (`mDesyncDebugState={}`, `miDesyncCount=0`) | – | ✓ | ✓ | **– (must NOT: preserves escalation-window count)** | `ClientSession.cpp:256,379` |
| Game clock: `SetTickCounter(0)` / `mTimeStep.ClearAccumulator()` / `mRealTime.Reset()` / `ResetRenderClock()` | – | – | ✓ | – | `ClientSession.cpp:341-344` |
| Game identity: `mClientPlayerIds`/`mClientPlayerCoords` clear, `SetClientGridCoord({})`, `SetPreviousClientArmor(0)`, `mVecVisualErrorOffset={}`, `mWeaponModeToggle.Reset()`, `mNavigationDelayControl.Reset()` | – | – | ✓ | – | `ClientSession.cpp:354-360` |
| Game fleet: `mFleetSelection.Clear()` | – | – | ✓ | – | `ClientSession.cpp:363` |
| Receive-buffer clears (full states + per-slot coord updates) | – | – | ✓ | – | `ClientSession.cpp:382-386` |

Notes on the table:
- **kResync deliberately narrow**: coord-frame reset + reconciler + sticky-timestamp clear only. It must **not** reset `mpDesyncManager` (the frequency counter drives escalation-to-disconnect across the desync window) and must **not** touch the clock or peer. This asymmetry is why a single flat "reset everything" would break recovery — the reason gating is load-bearing.
- **kServerLoad clock superset is the drift fix**: resetting all nine clock fields (vs today's four) is a deliberate, safe superset — the five extra fields are log-dedup trackers that gate only `LOG` emission. Flag for grill confirmation that this is not a behavior regression (see Notes).
- **kDisconnect leaves game identity/fleet/clock** — those are torn down by the `ChangeFrame(kMainMenu)` path, not the session reset (preserve current split).
- **kConnect is minimal** — establishes the clean baseline (clock, flags, subscription tracking) so a reconnect on the persistent session object cannot inherit stale state from a prior session even if a disconnect path was skipped; it does not touch coord frames or the freshly-created peer.

### Call-site migration

- `DisconnectFromServer` (`ClientSession.cpp:249-259`) → `ResetSession(ResetReason::kDisconnect)` + keep the peer teardown it triggers.
- `ResetForServerLoad` (`ClientSession.cpp:336-387`) → `ResetSession(ResetReason::kServerLoad)` (the `LOG` line and any non-reset side effects stay).
- `ClientDesyncManager::ResetCoordStatesForResync` (`ClientDesyncManager.cpp:106-115`) → calls `gpClientSession->ResetSession(ResetReason::kResync)`; `ClientDesyncManager::Reset` stays a distinct desync-tracker reset (invoked by kDisconnect/kServerLoad via the table's `mpDesyncManager->Reset()` row).
- Initial connect (`ConnectToServer`, `ClientSession.cpp:237-241`) → `ResetSession(ResetReason::kConnect)` before/after peer creation as appropriate.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.{h,cpp}` — new `ResetReason` enum, `ResetSession`, `ClockSyncState`/`SubscriptionTracking` sub-structs; retire `ResetForServerLoad`/`ClearSubscriptionState`/`DisconnectFromServerBase` field-lists.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDesyncManager.cpp` — `ResetCoordStatesForResync` delegates to `ResetSession(kResync)`; keep `Reset` as the tracker-only reset.
- `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.cpp` — clock-field accesses rename to `mClockSync.*` (grep `miClockOffset`/`miClockError`/`miClockTargetBehind`).
- `ComputeClockCorrectionNs` / `Reconcile` (wherever the clock fields land after `Refactor_SessionBaseCollapse.md`) — `mi*` → `mClockSync.mi*`.

## Out of scope

- No wire-format, `kuiProtocolVersion`, `Frame::kiVersion`, CRC, or sim-math change — this reorganizes reset bookkeeping only.
- No change to `CoordFrames::ResetClientState()` itself (the canonical per-coord reset) — this plan *reuses* it, and extends the same `= {}` convention to session-level groups.
- No change to reset **behavior** beyond the deliberate kServerLoad clock-field superset (drift fix) — every ✓/– cell reproduces current behavior.
- Not the server-side reset (`ServerSession::ResetClientsForLoad`) — different concern (client vectors + manager reset), left as-is.
- Not the subscription-lifecycle races or resync-classifier fix (`Network/SubscriptionLifecycleRaceHardening.md`, `Network/ResyncFullStateRepair.md`).

## Acceptance criteria

- One `ClientSession::ResetSession(ResetReason)` is the sole reset entry; no reset path hand-lists individual clock fields.
- Adding a clock field requires editing only `ClockSyncState`'s declaration — the `= {}` reset covers every reason automatically.
- kResync leaves `mpDesyncManager`'s escalation count and the clock/peer untouched (verified against the table); kServerLoad resets the full nine-field clock set.
- Client build compiles; behavior matches the union table cell-for-cell (aside from the documented clock superset).

## Notes

- **Sequence**: land **after** `Refactor_SessionBaseCollapse.md` — that collapse pulls all the reset fields (`mi*` clock, `mSubscriptionQueue`, `mSessionFlags`, coord-frame access) into the single `ClientSession` class, so this plan can group and reset them as members instead of reaching across the base seam. State this dependency in `Order.md`.
- **Sibling-plan overlap**: `Network/ResyncFullStateRepair.md` states "no change to `ResetCoordStatesForResync`" while this plan re-expresses it as `ResetSession(kResync)` — coordinate: land the resync-classifier fix first (it edits `ClientReceive.cpp`, disjoint) or refresh its citation. Both touch the resync recovery region.
- **Invariant exposure**: touches **cross-frame client session/desync/clock state and the resync/reconciliation-adjacent reset paths** — Risk: reconciliation-adjacent, hard to verify without exercising disconnect / server-load / `kbDesyncRecovery`. No sim/CRC/wire/`kiVersion` exposure. Allocation note: the reset paths already run under `ScopedSuppressAllocationTracking` (`ClientSession.cpp:252`, base `:59`); preserve that around any container clears.
- **Grill decision to pre-stage**: confirm the kServerLoad clock-field superset (resetting the five currently-skipped log-tracker fields) is an accepted drift-fix, not a behavior change to preserve — recommended: reset the full set (they gate only log emission; leaving them stale is the latent bug). Secondary: whether `kConnect` warrants a distinct reason or folds into `kDisconnect` (recommended: keep distinct for the persistent-session-object baseline guarantee).
