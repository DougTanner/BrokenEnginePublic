<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T02:29:26.000Z","dependsOn":[]} -->
# Refactor: Ring Layout & Retention Value Type

## Context

Ring-offset bookkeeping is the client reconciliation pipeline's concentrated fragility (`Projects/BrokenEngineSandbox/Source/Network/Client/`). The offsets that select rollback state and lay out the post-reconcile ring carry three overloaded, comment-warned meanings that must stay coherent by hand:

- **`CoordFrames::iConfirmedOffset`** is a logical offset from `iSnapshotHead`, but the CRC fast path mutates it to `iHighestMatchIndex` — an offset from the **OLD** head (`ReconcileReplayCrc.cpp:252`, `rFrames.iConfirmedOffset = validateResult.iHighestMatchIndex`). Downstream code re-reads it as "offset from OLD head to confirmed" to recompute retention (`ReconcileReplay.cpp:101-106`), a reinterpretation that only the inline comments keep straight.
- **`CoordScratch::iNewConfirmedOffset`** is a **physical** ring index that is actually the new **HEAD**, not the confirmed frame — the field's own header comment warns "physical ring index of the new head (NOT necessarily confirmed)" (`ClientReconciler.h:57`). `iNewConfirmedInnerOffset` (`:58`) patches the head→confirmed difference; `ApplyCoordWriteback` (`ReconcileReplay.cpp:44-53`) unpacks all three into `iSnapshotHead` / `iConfirmedOffset` / `iSnapshotCount`.
- The render-retention head-advance is **triplicated**: `iHeadAdvance = std::max<int64_t>(0, <confirmedIndex> - engine::kiRenderBehindTicks)` appears at `ReconcileReplayCrc.cpp:162`, `ReconcileReplay.cpp:105`, and `ReconcileReplay.cpp:229`, each recomputing `iOutputCount` from it and required to stay in sync by hand.

Additionally, `RunTwoTierFallback` (`ReconcileReplay.cpp:190-239`) re-implements ~80% of `RunPrimaryReplay` (`:157-183`): both rollback to base (`ReconcileRollbackCoord`), inject pending full state at confirmed, find the consecutive replay range (`ReconcileFindReplayRangeCoord`), and replay (`ReconcileReplayCoord`). The fallback adds only desync-state clearing (`:199-203`) and the empty-range short-circuit (`:223-235`).

## Design

### `RingLayout` value type + single `ComputeRetention`

Introduce a small value type capturing the ring's layout in one place — fields as verified from `CoordFrames`: `{ int64_t iHead, int64_t iCount, int64_t iConfirmedInner }` (head physical index, valid count, head→confirmed offset). Provide one function:

- `RingLayout ComputeRetention(int64_t iHeadPhysical, int64_t iSnapshotCount, int64_t iConfirmedIndex)` — folds the triplicated `iHeadAdvance = max(0, iConfirmedIndex - kiRenderBehindTicks)` computation: returns the retained head (`SnapshotIndex(iHeadPhysical, iHeadAdvance)`), the `iConfirmedInner = iConfirmedIndex - iHeadAdvance`, and `iCount = iSnapshotCount - iHeadAdvance`.

The fast path (`CrcApplyMatchResult`, `ReconcileReplayCrc.cpp:162-165`) and the two replay-path retention sites (`ReconcileReplay.cpp:105-106`, `:229-230`) all produce a `RingLayout` via `ComputeRetention` instead of open-coding it. `ApplyCoordWriteback` becomes the **one** consumer that unpacks a `RingLayout` into `iSnapshotHead`/`iConfirmedOffset`/`iSnapshotCount`. Replace the "offset relative to OLD head" reinterpretation of `iConfirmedOffset` with an explicit `RingLayout`-typed hand-off so the OLD-head vs NEW-head meaning is a type distinction, not a comment.

### Parameterize one replay routine

Merge `RunPrimaryReplay` and `RunTwoTierFallback` into one function taking the rollback base (tick + offset) and a flag for the fallback-only behavior (desync-state clear + empty-range short-circuit). `ReconcileCoord` (`:324-332`) calls it first with the shrunk/confirmed base from `DetermineRollbackBase`, then — on first-tick desync of a shrunk rollback — re-invokes it with the confirmed base and the fallback flag set. Rollback → inject-at-confirmed → find-range → replay lives once.

## Critical files

- `ReconcileReplayCrc.cpp` — `CrcApplyMatchResult` (`:149-170`, retention via `ComputeRetention`), `CrcFastPathProcessCoord` (`:244-252`, the `iConfirmedOffset = iHighestMatchIndex` write becomes a typed `RingLayout` producer).
- `ReconcileReplay.cpp` — `ApplyCoordWriteback` (`:39-54`, sole `RingLayout` consumer), `EarlyReturnIfNoServerData` (`:99-108`), `RunPrimaryReplay` (`:157-183`) + `RunTwoTierFallback` (`:190-239`) merge, `ComputeOutputLayout` (`:244-274`, aligns with `RingLayout`).
- `ClientReconciler.h` — `CoordScratch` fields `iNewConfirmedOffset`/`iNewConfirmedInnerOffset`/`iOutputCount` (`:57-59`) replaced or backed by a `RingLayout`; update `Reset()` (`:69-88`).
- `GameBase.h` — `CoordFrames` layout fields (`iSnapshotHead`/`iSnapshotCount`/`iConfirmedTick`/`iConfirmedOffset`, `:50-55`) are the writeback target; the `RingLayout` is scratch, `CoordFrames` stays the committed authority (do not change its serialized/reset surface beyond what writeback already touches).

## Out of scope

- Any change to ring **indexing**: all physical↔logical conversion stays via `SnapshotIndex(iHead, iLogical)` — never raw `%` (engine `CoordFrames` invariant). `RingLayout` stores indices; it does not reimplement modular arithmetic.
- The CRC fast-path *matching* logic, `DetermineRollbackBase`'s shrunk-vs-full selection policy, replay throttling, and desync escalation — behavior unchanged; only the offset/retention plumbing and the replay-routine duplication are touched.
- Pending-full-state injection semantics (`ReconcileInjectPendingFullState`) — reused as-is by the merged replay routine.
- `serverUpdates` erase / `iHighWaterValidatedTick` advance — unchanged.

## Acceptance criteria

- The `iHeadAdvance = max(0, confirmedIndex - kiRenderBehindTicks)` expression appears exactly once (inside `ComputeRetention`).
- Exactly one function performs rollback → inject → find-range → replay; the two-tier fallback is that function with a flag.
- `iConfirmedOffset`'s "relative to OLD head" transient meaning is carried by a typed `RingLayout` hand-off, not reconstructed from comments at the consumer.
- Behavior-preserving: fast-path, shrunk rollback, two-tier fallback, gap catch-up, and pending-full-state injection all produce byte-identical `iSnapshotHead`/`iConfirmedOffset`/`iSnapshotCount`/`iConfirmedTick` writeback as before.

## Notes

- **Invariant exposure: HIGH determinism/CRC.** This reshapes rollback/replay **state selection** on the client hot path. It is a behavior-preserving refactor, but any offset error IS a desync (wrong rollback base or wrong retained head → CRC mismatch cascade). Risks 3. Expectation: validate under the game's network-simulation soak (`keNetworkSimulation` presets with simulated loss/jitter that force genuine re-simulations) — reconciliation must hold CRC parity across fast-path, shrunk, two-tier, and gap paths, with the per-region `NetworkSimulationBounds` reconcile-depth/CRC tolerances the Network profiler screen checks staying within range. This is the soak expectation, stated here rather than as a manual-test section.
- Client-only (`BT_CLIENT`), no wire bytes, no `.pack`/`kiVersion`.
- Pre-staged grill decision: does `RingLayout` **replace** the three `CoordScratch` fields (`iNewConfirmedOffset`/`iNewConfirmedInnerOffset`/`iOutputCount`) or sit **alongside** them as the shared producer/consumer type? Replacing is cleaner but touches `ComputeOutputLayout`'s four subcases (`:249-270`); recommend replace, verifying each subcase maps onto `{iHead, iCount, iConfirmedInner}`.
