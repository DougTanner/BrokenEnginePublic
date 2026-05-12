# Network/Client/ - Client Session and Reconciliation

## Overview

Client-side networking: connection lifecycle, server data ingestion, rollback-and-replay reconciliation, and desync detection/recovery. Owned by `ClientSession` via `unique_ptr`. Client-only (`BT_CLIENT`).

## Key Classes

- **ClientSession** - Top-level orchestrator inheriting `engine::ClientSessionBase`. Drives connection, subscriptions, clock correction, and game-packet sends. Delegates to three owned managers below.
- **ClientDataReceiver** - Applies incoming static data, full states, and per-tick updates into `CoordFrames`. Static-data application also drives lazy island-texture acquisition: each placement triggers a per-CRC texture-slot mint so terrain GPU residency follows subscription arrivals.
- **ClientReconciler** - Single-pass rollback-and-replay per `ClientUpdate()`. Per-coord work runs in parallel via `common::gpMultithreading` on `CoordFrames` entries directly (no marshaling layer).
- **ClientDesyncManager** - Desync detection, debug-frame capture, resync coordination, and frequency-based escalation to disconnect.
- **ReconcileReplay** - Stateless pipeline helpers for the rollback path. Split across two siblings: `ReconcileReplay.cpp` holds coord-level entry points (pending-full-state injection, coord-result writeback, top-level coord reconcile); `ReconcileReplayTick.cpp` holds the tick-level primitives (rollback, replay-range scan, per-tick run + CRC validation, forward-step catch-up, fast-path catch-up).

## Architecture Notes

- Rollback is **shrunk-by-default** (one tick before lowest mismatch, CRC-validated). Full rollback to `iConfirmedTick` is reserved for fallback cases enumerated in [Network.md](../../../../../Documents/Architecture/Network.md).
- **Two-tier rollback fallback**: if shrunk rollback desyncs at its first replay tick, the speculative base was bad — desync state clears and a full-rollback retry runs in the same `Run()`.
- Sticky subscriptions: unwanted coords remain active briefly to avoid flicker during transitions.
- Soft desync recovery: CRC mismatch triggers resync; repeated desyncs within a short window escalate to disconnect.
- Visual error offset accumulates on `gpGame` after full replay, resetting to zero if it exceeds `Game::kfVisualErrorMaxDistance` rather than accumulating unboundedly.
- Initial full state sets `gpGame` tick behind `latestServerTick` by a jitter-safety floor so sim starts at the steady-state clock target — avoids startup freeze. Offset is clamped at the server's current tick so a fresh-from-save server (tick below the floor) doesn't drive the client tick negative.
- Hard clock snap (bypassing gradual correction) triggers on large clock error or explicit disconnect flag. Snapped tick is clamped at zero for the same fresh-from-save reason as the initial-state path.

## Invariants & Parallelism

- **No cross-coord writes**: per-coord workers touch only their own `CoordFrames` entry; first-wins desync selection and replay-flag aggregation run on the main thread post-dispatch. `mWorks.resize()` (never `clear()`) preserves scratch capacity.
- **Validated ticks are frozen**: a tick whose client CRC matched the server must never re-simulate; re-sim attempts trip `DEBUG_BREAK()`.
- **Stalled short-circuit**: while a debug frame is outstanding, poll/reconcile return early.
- High-frequency logs (mismatch, throttle, clock error) use hysteresis / periodic emission.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent hub (packet types, wire format, StatusChange batch)
- [Game Reconciliation](../../../../../Documents/Architecture/GameReconciliation.md)
- [Network Architecture](../../../../../Documents/Architecture/Network.md)
