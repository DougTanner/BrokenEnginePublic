# Network/Client/ - Client Session and Reconciliation

## Overview

Client-side networking: connection lifecycle, server data ingestion, rollback-and-replay reconciliation, and desync detection/recovery. Three managers owned by `ClientSession` via `unique_ptr`; driven once per frame by `engine::GameBase::ClientUpdate()` through the `game::gpClientSession` global (Poll → UpdateSubscriptions → Reconcile). Client-only (`BT_CLIENT`).

## Key Classes

- **ClientSession** - Top-level orchestrator inheriting `engine::ClientSessionBase`. Drives connection, server-load reset, clock correction/snap, queue-based subscriptions, game-packet sends, and decode of the server timescale broadcast. Delegates to three owned managers below. Subscription bookkeeping lives in `ClientSessionSubscriptions.cpp`.
- **ClientDataReceiver** - Applies incoming static data, full states, and per-tick updates into `CoordFrames`. Static-data application also drives lazy island-texture acquisition: each placement triggers a per-CRC texture-slot mint so terrain GPU residency follows subscription arrivals.
- **ClientReconciler** - Single-pass per `ClientUpdate()`. Per-coord work runs in parallel via `common::gpMultithreading` on `CoordFrames` entries directly (no marshaling layer); merge of profiling, first-wins desync, and visual-error offset runs on the main thread post-dispatch.
- **ClientDesyncManager** - Desync detection, debug-frame capture, resync coordination, and frequency-based escalation to disconnect.
- **ReconcileReplay** - Stateless pipeline helpers (declared in `ReconcileReplay.h`) split across four siblings: `ReconcileReplay.cpp` (per-coord orchestration — fast-path gate, rollback-base selection, primary replay, two-tier fallback, output layout, writeback); `ReconcileReplayCrc.cpp` (the CRC fast-path walk that advances `iConfirmedTick` and reports the lowest unresolved mismatch); `ReconcileReplayTick.cpp` (tick-level primitives — rollback, replay-range scan, per-tick run + CRC validation, forward-step, catch-up); `ReconcileReplayClientState.cpp` (cross-coord transfer migration of the client player).

## Architecture Notes

- **CRC fast path is primary**: each coord first walks its `serverUpdates` against the speculative ring; matching frames advance `iConfirmedTick` in place and the coord catches up forward without re-simulating. Full rollback-and-replay is the fallback, entered when the walk finds an unresolved mismatch or pending full state past confirmed, or pending updates with no ring frame to compare against.
- **Render-behind retention**: the fast path keeps `kiRenderBehindTicks` frames *before* the confirmed match as the new ring head so the renderer always has prev-tails for interpolation; `iConfirmedOffset` tracks head-to-confirmed delta.
- Rollback is **shrunk-by-default** (one tick before the lowest unresolved mismatch, only if that base frame was CRC-validated). Full rollback to `iConfirmedTick` is the fallback; cases enumerated in [Network.md](../../../../../Documents/Architecture/Network.md).
- **Per-frame replay bound**: replay runs to `iMaxConsecutive`; `ReconcileCatchUpCoord` then forward-sims the remainder to `iTargetTick` unconditionally, so replay count does not bound per-frame `RunFrameTick` cost. The real per-frame bound is the ring budget `iBudget = kiNetworkBufferSize - iReplayWriteCount`.
- **Server-load reset**: a load notification drains/clears all coord, clock, identity, fleet, and reconciler state so the client re-bootstraps from the post-load server tick.
- Sticky subscriptions: unwanted coords remain active for `kStickySubscriptionDuration` to avoid flicker during transitions. The desired set (current cell + `mVisibleNeighbors`, or origin when unfocused) feeds a subscribe/unsubscribe queue throttled by the engine's slot budget; `SubscriptionChangeReason` tags each recompute for logging.
- Soft desync recovery: CRC mismatch triggers resync; repeated desyncs within a short window escalate to disconnect.
- Visual error offset accumulates on `gpGame` after full replay of the client coord, resetting to zero if it exceeds `Game::kfVisualErrorMaxDistance` rather than accumulating unboundedly.
- Initial full state sets `gpGame` tick behind `latestServerTick` by a jitter-safety floor so sim starts at the steady-state clock target — avoids startup freeze. Offset is clamped at the server's current tick so a fresh-from-save server (tick below the floor) doesn't drive the client tick negative.
- Hard clock snap (bypassing gradual correction) triggers on large clock error only. Snapped tick is clamped at zero for the same fresh-from-save reason as the initial-state path.

## Invariants & Parallelism

- **No cross-coord writes during dispatch**: per-coord workers touch only their own `CoordFrames` entry; first-wins desync selection and flag aggregation run on the main thread post-dispatch. The lone cross-coord read (transfer migration matching the client player against a destination coord's result) runs single-threaded in `ReconcileReplayClientState.cpp` after the merge. Per-frame `mWorks` grows via `resize()` (never per-frame `clear()`) so each `CoordScratch::replayStack` retains allocated capacity across `Run()` calls.
- **Validated ticks are frozen**: a tick at or below `iHighWaterValidatedTick` (client CRC matched the server) must never re-simulate; re-sim attempts trip `DEBUG_BREAK()`. Full replay also `DEBUG_BREAK()`s if it would repeat identical work (unchanged `iConfirmedTick` + `serverUpdates` count, no pending full state).
- Transfer `StatusChange`s apply *after* each tick (matching server Destroy/Spawn ordering); when transfers occurred, the frame CRC is recomputed — required for fast-path matching.
- **Stalled short-circuit**: while a debug frame is outstanding, poll/reconcile return early.
- High-frequency logs (mismatch, clock error, visual error) use hysteresis / cooldown / periodic emission, plus per-coord stuck-state dedup.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent hub (packet types, wire format, StatusChange batch)
- [Game Reconciliation](../../../../../Documents/Architecture/GameReconciliation.md)
- [Network Architecture](../../../../../Documents/Architecture/Network.md)
