<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Refactor: Ring Layout & Retention Value Type

## Context

Ring-offset bookkeeping is the client reconciliation pipeline's concentrated fragility (`Projects/BrokenEngineSandbox/Source/Network/Client/`). The offsets that select rollback state and lay out the post-reconcile ring carry three overloaded, comment-warned meanings that must stay coherent by hand:

- **`CoordFrames::iConfirmedOffset`** is a logical offset from `iSnapshotHead` (`Engine/Source/GameBase.h:53-55`), but the CRC fast path mutates it to `iHighestMatchIndex` — an offset from the **OLD** head (`ReconcileReplayCrc.cpp:252`, `rFrames.iConfirmedOffset = validateResult.iHighestMatchIndex`). Downstream code re-reads it as "offset from OLD head to confirmed" to recompute retention (`EarlyReturnIfNoServerData`, `ReconcileReplay.cpp:137-142`) and to seed the rollback base (`DetermineRollbackBase`, `ReconcileReplay.cpp:161`; `RunTwoTierFallback`, `:251`) — a reinterpretation that only the inline comments keep straight.
- **`CoordScratch::iNewConfirmedOffset`** is a **physical** ring index that is actually the new **HEAD**, not the confirmed frame — the field's own header comment warns "physical ring index of the new head (NOT necessarily confirmed)" (`ClientReconciler.h:51`). `iNewConfirmedInnerOffset` (`:52`) patches the head→confirmed difference; `ApplyCoordWriteback` (`ReconcileReplay.cpp:40-55`) unpacks all three into `iSnapshotHead` / `iConfirmedOffset` / `iSnapshotCount`.
- The render-retention head-advance is **triplicated**: `iHeadAdvance = std::max<int64_t>(0, <confirmedIndex> - engine::kiRenderBehindTicks)` appears at `ReconcileReplayCrc.cpp:162`, `ReconcileReplay.cpp:141`, and `ReconcileReplay.cpp:274`, each recomputing `iOutputCount` from it and required to stay in sync by hand.

Additionally, `RunTwoTierFallback` (`ReconcileReplay.cpp:235-284`) re-implements the core of `RunPrimaryReplay` (`:193-228`): both rollback to base (`ReconcileRollbackCoord`), inject a due pending full state at confirmed (`ReconcileInjectPendingFullState`), find the consecutive replay range (`ReconcileFindReplayRangeCoord`), and replay (`ReconcileReplayCoord`). The fallback adds desync-state clearing (`:240-253`) and the empty-range short-circuit (`:267-280`); the primary path additionally carries two branches the fallback lacks — stale pending-full-state discard (`:209-213`) and unreachable-full-state adoption via `AdoptUnreachablePendingFullState` (`:217-224`).

## Design

### `RingLayout` value type + single `ComputeRetention`

Introduce a small value type capturing the ring's layout in one place, declared in `ClientReconciler.h` beside `CoordScratch` — fields as verified from `CoordFrames`: `{ int64_t iHead, int64_t iCount, int64_t iConfirmedInner }` (head physical index, valid count, head→confirmed offset). Provide one function:

- `RingLayout ComputeRetention(int64_t iHeadPhysical, int64_t iSnapshotCount, int64_t iConfirmedIndex)` — folds the triplicated `iHeadAdvance = max(0, iConfirmedIndex - kiRenderBehindTicks)` computation: returns the retained head (`SnapshotIndex(iHeadPhysical, iHeadAdvance)`), the `iConfirmedInner = iConfirmedIndex - iHeadAdvance`, and `iCount = iSnapshotCount - iHeadAdvance`.

The fast path (`CrcApplyMatchResult`, `ReconcileReplayCrc.cpp:162-165`) and the two replay-path retention sites (`EarlyReturnIfNoServerData`, `ReconcileReplay.cpp:141-142`; `RunTwoTierFallback` empty-range short-circuit, `:274-275`) all produce a `RingLayout` via `ComputeRetention` instead of open-coding it. `ApplyCoordWriteback` (`ReconcileReplay.cpp:40-55`) becomes the **one** consumer that unpacks a `RingLayout` into `iSnapshotHead`/`iConfirmedOffset`/`iSnapshotCount`. Replace the "offset relative to OLD head" reinterpretation of `iConfirmedOffset` with an explicit `RingLayout`-typed hand-off so the OLD-head vs NEW-head meaning is a type distinction, not a comment. The in-place `iConfirmedTick`/`iConfirmedOffset` advance at `ReconcileReplayCrc.cpp:251-252` stays (it upholds the no-re-simulation invariant); what changes is that downstream retention no longer re-derives meaning from that field by comment.

### Parameterize one replay routine

Merge `RunPrimaryReplay` (`ReconcileReplay.cpp:193-228`) and `RunTwoTierFallback` (`:235-284`) into one function taking the rollback base (tick + offset) and a flag for the fallback-only behavior. The merged routine's shared skeleton — rollback → inject-due-at-confirmed → find-range → replay — lives once; gated behavior preserves current control flow exactly:

- **Fallback-only** (flag set): the desync-state clearing preamble with its retry log and `kShrunkRollback` flag clear (current `:240-253`), and the empty-range short-circuit that applies retention via `ComputeRetention`, writes back, runs `ReconcileFastPathCatchUp`, and reports the coord resolved (current `:267-280`).
- **Primary-only** (flag clear): the stale pending-full-state discard branch (current `:209-213`) and the unreachable-full-state adoption branch calling `AdoptUnreachablePendingFullState` with its early return that skips replay but continues the main flow (current `:217-224`).

The merged routine returns whether the coord was fully resolved (true only on the fallback empty-range short-circuit, matching `RunTwoTierFallback`'s current bool contract). `ReconcileCoord` (`:362-377`) calls it first with the shrunk/confirmed base from `DetermineRollbackBase`, then — on first-tick desync of a shrunk rollback — re-invokes it with the confirmed base and the fallback flag set.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria; add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, forward declarations, signature updates in `ReconcileReplay.h`) the named change requires.

### In scope (named regions only)

- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientReconciler.h`
	- New `RingLayout` struct and `ComputeRetention` declaration beside `CoordScratch`.
	- `CoordScratch` members `iNewConfirmedOffset` / `iNewConfirmedInnerOffset` / `iOutputCount` (`:51-53`) and their `Reset()` lines (`:72-74`) — replaced or backed per the Notes decision.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplayCrc.cpp`
	- `CrcApplyMatchResult` (`:149-170`): retention block (`:162-165`) becomes a `ComputeRetention` call producing a `RingLayout`.
	- `CrcFastPathProcessCoord` match-apply block (`:244-263`): the transient OLD-head meaning written at `:251-252` is carried forward as a typed `RingLayout` hand-off.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp`
	- `ApplyCoordWriteback` (`:40-55`): sole `RingLayout` unpacker.
	- `AdoptUnreachablePendingFullState` scratch-output writes (`:76-80`): produce the layout instead of the three raw fields.
	- `ApplyCrcFastPath` floor-preserve/reset block (`:113-118`): reset expressed against the layout hand-off.
	- `EarlyReturnIfNoServerData` retention block (`:135-144`): retention via `ComputeRetention`.
	- `DetermineRollbackBase` (`:155-188`): only the `iRollbackOffset` seeding read (`:161`) if the typed hand-off changes its input — the shrunk-vs-full selection policy itself is out of scope.
	- `RunPrimaryReplay` (`:193-228`) + `RunTwoTierFallback` (`:235-284`): merged as designed above.
	- `ComputeOutputLayout` (`:289-319`): four subcases (`:294-315`) produce the layout; verify each maps onto `{iHead, iCount, iConfirmedInner}`.
	- `ReconcileCoord` (`:362-377`): call sites for the merged routine.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplayTick.cpp`
	- `ReconcileValidateCrcCoord` confirmed-frame recording tail (`:281-297`): produces the layout (its `iNewConfirmedInnerOffset = 0` retention-clear semantics preserved exactly).
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplayClientState.cpp`
	- `FindMatchingPlayerInCoord` fast-path frame lookup (`:30-36`): reads the layout instead of the raw scratch fields.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.h`
	- Declaration changes only as mechanically required by the merged routine and any hand-off carried through `CrcFastPathCoordResult`.

`Engine/Source/GameBase.h` — read-only reference. `CoordFrames` layout fields (`iSnapshotHead`/`iSnapshotCount`/`iConfirmedTick`/`iConfirmedOffset`, `:50-55`) are the writeback target; the `RingLayout` is scratch, `CoordFrames` stays the committed authority. Do not change its serialized/reset surface or any other `GameBase.h` content.

### Out of scope

- Any change to ring **indexing**: all physical↔logical conversion stays via `SnapshotIndex(iHead, iLogical)` (`GameBase.h:122-125`) — never raw `%` (engine `CoordFrames` invariant). `RingLayout` stores indices; it does not reimplement modular arithmetic.
- The CRC fast-path *matching* logic (`CrcValidateLoop` and the log-suppression state machine in `CrcFastPathProcessCoord`), `DetermineRollbackBase`'s shrunk-vs-full selection policy, replay throttling, and desync escalation — behavior unchanged; only the offset/retention plumbing and the replay-routine duplication are touched.
- Pending-full-state injection semantics (`ReconcileInjectPendingFullState`) and unreachable-adoption semantics (`AdoptUnreachablePendingFullState` beyond its scratch-output writes) — reused as-is by the merged replay routine.
- `serverUpdates` erase / `iHighWaterValidatedTick` advance — unchanged.
- `ReconcileUpdateClientState` beyond the named `FindMatchingPlayerInCoord` lookup; all rendering, session, and desync-manager code.

## Risk tier

Tier 3. Trigger: determinism/CRC exposure — this reshapes rollback/replay **state selection** on the client hot path. It is a behavior-preserving refactor, but any offset error IS a desync (wrong rollback base or wrong retained head → CRC mismatch cascade). Client-only (`BT_CLIENT`), no wire bytes, no `.pack`/`kiVersion`.

## Acceptance criteria

- The `iHeadAdvance = max(0, confirmedIndex - kiRenderBehindTicks)` expression appears exactly once (inside `ComputeRetention`).
- Exactly one function performs rollback → inject → find-range → replay; the two-tier fallback is that function with a flag, and the primary-only stale-discard/unreachable-adoption branches remain reachable only on the primary invocation.
- `iConfirmedOffset`'s "relative to OLD head" transient meaning is carried by a typed `RingLayout` hand-off, not reconstructed from comments at the consumer.
- Behavior-preserving: fast-path, shrunk rollback, two-tier fallback, gap catch-up, and pending-full-state injection/adoption all produce byte-identical `iSnapshotHead`/`iConfirmedOffset`/`iSnapshotCount`/`iConfirmedTick` writeback as before.

## Notes

- **Verification expectation**: validate under the game's network-simulation soak (`keNetworkSimulation` presets with simulated loss/jitter that force genuine re-simulations) — reconciliation must hold CRC parity across fast-path, shrunk, two-tier, and gap paths, with the per-region `NetworkSimulationBounds` reconcile-depth/CRC tolerances the Network profiler screen checks staying within range. This is the soak expectation, stated here rather than as a manual-test section.
- Pre-staged grill decision: does `RingLayout` **replace** the three `CoordScratch` fields (`iNewConfirmedOffset`/`iNewConfirmedInnerOffset`/`iOutputCount`) or sit **alongside** them as the shared producer/consumer type? Replacing is cleaner but touches every producer/consumer named in the scope contract, including `ComputeOutputLayout`'s four subcases; recommend replace, verifying each subcase maps onto `{iHead, iCount, iConfirmedInner}`.
