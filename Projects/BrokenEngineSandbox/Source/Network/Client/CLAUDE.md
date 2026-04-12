# Network/Client/ - Client Session and Reconciliation

## Overview

Client-side networking: connection lifecycle, server data ingestion, rollback-and-replay reconciliation, and desync detection/recovery. All classes are `#ifdef BT_CLIENT` only and are owned by `ClientSession` via `unique_ptr`.

## Key Classes

- **ClientSession** - Top-level orchestrator inheriting `engine::ClientSessionBase`. Drives connection, subscription management, clock correction, and game packet sends (player settings, fleet operations including `kClientFleetNavigationDelay` for the per-fleet navigation delay slider). Delegates data handling, reconciliation, and desync management to three owned managers
- **ClientDataReceiver** - Handles incoming server data: applies static data, full states, and per-tick updates into `CoordFrames`
- **ClientReconciler** - Single-pass rollback-and-replay reconciliation. `Run()` is invoked synchronously from `ClientSession::Reconcile()` once per `ClientUpdate()`. In one call it drops server frames already validated against the snapshot ring, replays from the confirmed frame on CRC mismatch, and catches up the simulation forward to the post-advance `miTickCounter` target (`ClientUpdate` advances the counter before calling; `Run()` no longer snaps it). Per-coord work is parallelized via the shared `common::gpMultithreading` dispatch pool operating directly on `engine::CoordFrames` entries (no marshaling layer). Captures pre/post human-coord position delta as a visual error offset on `gpGame` for smooth camera correction after full replay
- **ClientDesyncManager** - Desync detection, debug frame capture, resync coordination, and frequency-based escalation to disconnect
- **ReconcileReplay** - Stateless pipeline helpers for the rollback path: CRC fast-path, full replay (rollback → replay → catch-up), pending full-state injection, and adaptive replay throttle based on `iJitterUs`. Three early-outs skip replay when data is insufficient: gap at confirmed+1 (no server update to replay from), unvalidated shrunk base (speculative frame above high-water mark), and empty range after two-tier fallback. Rollback is **shrunk-by-default** (one tick before the lowest mismatch, must be CRC-validated via `iHighWaterValidatedTick`); full rollback to `iConfirmedTick` is used only under the conditions enumerated in [Network.md](../../../../../Documents/Architecture/Network.md) (mismatch adjacent to confirmed floor, pending full state, missing speculative snapshot, unvalidated shrunk base, or shrunk-first-tick desync fallback). A repeated-work invariant (`iLastReplayConfirmedTick`/`iLastReplayServerUpdateCount`) trips `DEBUG_BREAK` if full replay is entered with identical inputs as the previous attempt

## Architecture Notes

- `ClientSession` splits across two `.cpp` files: `ClientSession.cpp` (core, reconcile integration) and `ClientSessionSubscriptions.cpp` (coord subscription management, full-state application)
- `ReconcileReplay` splits across three `.cpp` files by concern: core replay pipeline, CRC fast-path, and human state tracking
- Sticky subscriptions: unwanted coords stay active for 2 s via `mUnwantedTimestamps` to prevent flicker during brief coord transitions
- Soft desync recovery: CRC mismatch triggers `kClientResyncRequest`; after 3 desyncs within 10 s `ClientDesyncManager` escalates to disconnect
- After full replay, reconciliation sets a skip-invalidation flag on the audio manager to prevent transient voice churn from the replayed frame diff
- Log deduplication: when reconciliation is stuck replaying the same desync repeatedly, per-coord state in `CoordFrames` suppresses repeated mismatch/replay logging and only emits periodic updates (every 64 frames). Clock error logging in `ClientSessionBase` is similarly throttled to once per second

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Network root (shared packet types and serialization)
- Engine base: `Engine/Source/Network/Client/ClientSessionBase.h`
- [Game Reconciliation](../../../../../Documents/Architecture/GameReconciliation.md)
- [Network Architecture](../../../../../Documents/Architecture/Network.md)
