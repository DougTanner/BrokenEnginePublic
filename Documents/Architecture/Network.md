# Network Architecture

**IMPORTANT**: Every Frame must be independent from all others, except for generating/consuming StatusChange events.

## Server

- Multi-threaded Frame Tick update, spread over cores via `common::gpMultithreading->Dispatch` (`GameBase::BuildAndDispatchFrameTicks`)
- Each Tick is simulated exactly once: previous Frame Tick(`sharedCrc`) + StatusChanges → new Frame Tick(`sharedCrc`)
- When a Tick is ready, send its StatusChanges + `sharedCrc` to the client (one unreliable packet per active subscription slot)
  - Client confirms ASAP via an ACK stream carrying, per-slot, `(slot, epoch, floor, bitfield-low, bitfield-high)`. The bitfield covers 128 ticks (= `kiNetworkBufferSize`) above the floor
  - **Epoch**: `AckState.uiEpoch` guards against ghost ACKs reactivating a slot that has been reassigned to a different coord. An ACK whose epoch doesn't match the current slot epoch is dropped
- **Resend**: per client, scan each slot's ACK bitfield for unset bits above the floor (oldest-first) and re-send only those specific missing ticks as `kServerCoordResend` packets, capped at `kiMaxResendFrames = 8` per call. The server does NOT blindly re-send the entire range from oldest-unacked — only the exact gaps the client reported. Because the scan is oldest-first and the floor never advances past an unacked tick, the oldest gap is always prioritized: if >8 gaps exist, newer ones wait for the next `SendResends` call. The resend source is a per-coord ring buffer of compressed frame deltas; an unacked tick keeps getting retried until it either arrives or is evicted from that ring buffer (logged as `Evicted frame`), at which point the client can only recover via full state

## Client Global

- Client sim runs strictly **behind** `latestServerTick` by `miCurrentTargetBehind` ticks. In zero-loss steady state, the server's packet carrying a StatusChange for tick N always arrives before the client has simulated tick N, so StatusChanges fold in on the first simulation of that tick and reconcile never has to replay
	- Network simulation states with simulated packet loss will sometimes cause genuine re-simulations of Ticks
- **`miCurrentTargetBehind`**: pure jitter buffer, computed as `ceil((measuredJitterUs + kiJitterSafetyUs) / tickTimeUs)` where `kiJitterSafetyUs = 125'000` (125 ms wall-clock, = 4 ticks at 32 Hz). RTT is deliberately NOT in the formula — it is already built into `latestServerTick` lagging server wall clock by one-way latency, so `targetBehind` only needs to cover arrival jitter plus a fixed safety margin. Hysteresis: updated only when the new value differs by 2+ ticks to avoid oscillation
- **Hard ceiling**: `miTickCounter` is clamped in `GameBase::ClientUpdate` so it can never advance past `latestServerTick - miCurrentTargetBehind`. Excess wall-clock time is absorbed back into the tick accumulator (bounded by the existing `kiMaxAccumulatorTicks` cap). Render smoothness during brief clamps is preserved by `mTickRemainderNs` + velocity extrapolation from the snapshot tail
- **Clock error** = `preReconcileTick - (latestServerTick - miCurrentTargetBehind)`. Positive error means sim is past the ceiling (transient only — the clamp prevents sustained drift ahead); negative error means sim is behind the ceiling and needs to catch up. Small errors are corrected gradually by nudging the tick accumulator up to 4 steps per frame. Errors ≥ `kiClockErrorDisconnectThreshold` (= 64, `NetworkProtocol.h`) accumulate a consecutive-frame counter and disconnect if sustained. Extreme errors (≥ `kiClockSnapThreshold` = 28, defined in the game layer at `ClientSession.cpp` — *not* alongside the engine-layer constants) hard-snap `miTickCounter` to `latestServerTick - miCurrentTargetBehind` and clear the accumulator — this covers first-packet initialization (sim = 0, target = ~1356 → snap fires) and long-stall recovery

## Client per Frame

- Ring buffer of Ticks, saving old Ticks + their `sharedCrc`, plus a separate buffer of received server updates (StatusChanges + `sharedCrc`) awaiting validation
- **Fast-path drop (CRC match)**: walk the ring oldest-first; for each Tick with a matching server `sharedCrc`, advance `iConfirmedTick` past it and drop it from the ring (always keep at least the most recent). Validated server updates are consumed from their buffer in the same pass
- **`iHighWaterValidatedTick`**: once a Tick's `sharedCrc` has matched the server, it is never re-simulated — even if a later rollback's target would reach back past it. Enforced in `ReconcileReplay.cpp`
- **Mismatch path (rollback + replay)**: when the oldest un-confirmed Tick's `sharedCrc` disagrees with the server's:
  1. Rollback — by default to the tick *before* the mismatch (**shrunk rollback**), not all the way to `iConfirmedTick`, to minimize re-sim cost. Full rollback to `iConfirmedTick` is used instead when any of the following hold (see `ReconcileReplay.cpp` rollback-base selection):
     - `iLowestUnresolvedMismatch <= iConfirmedTick + 1` — the mismatch sits immediately above the confirmed floor, so no intermediate speculative frame exists to shrink to
     - A `pendingFullState` is queued — it must be injected at `iConfirmedTick`, which forces the rollback base to be confirmed
     - No speculative snapshot exists in the ring at `iShrunkTick` (pruned or never captured)
     - Shrunk rollback was attempted but desynced at its very first replay tick — interpreted as the speculative starting state being bad. The two-tier fallback clears desync state, resets replay, and retries from `iConfirmedTick`
     - Note: the `FullReplay: true` field in `Reconcile post-replay` log lines means "rollback-and-replay pipeline ran" (non-fast-path), *not* "full rollback was used" — the shrunk-vs-full choice is not currently logged
  2. Replay each buffered server update tick-by-tick, applying its StatusChanges and CRC-validating against the server's `sharedCrc`. Matching ticks advance `iConfirmedTick` in the same pass
  3. If a replayed Tick still CRC-fails *with* its StatusChanges applied, that is a genuine desync → escalate to resync/disconnect
  4. This is strictly better than "re-simulate forward with empty inputs": buffered server updates carry StatusChanges that must be folded in, otherwise every subsequent Tick would immediately mismatch again and thrash reconciliation every frame until the buffer drains
- **Adaptive replay throttle**: the number of ticks replayed per call is jitter-aware. High measured jitter (> ~8 ms) caps replay to ~25% of available frames to avoid cascading overload; low jitter (< ~2 ms) lets replay run to completion. **Gap-aware override**: if the pending-replay backlog approaches `kiNetworkBufferSize` (128), the cap is overridden upward so the client doesn't slide into permanent desync
- **Pending full-state injection**: if a full state arrives while reconciliation is running, it is injected at its matching tick, replacing the speculative timeline there. Full states for ticks already passed during this reconcile are discarded as stale
- **`kRecalculated` frame flag**: frames touched by rollback/replay are marked so audio invalidation is skipped for them, preventing voice churn across every replay
- **Visual error offset smoothing**: after a full replay, the human-controlled entity's pre/post position delta is accumulated into `gpGame->mVecVisualErrorOffset` (applied via the camera) so the correction is spread over several render frames instead of snapping
- **Forward sim**: from the most recent Tick remaining, simulate empty-input ticks until the ring tail is less than one Tick behind the target time (`miTickCounter`, advanced by `TickRealtime()` before Reconcile runs)
- **Render**: the ring tail is used as the base Frame; the sub-tick remainder (`mTickRemainderNs`) is converted to `fDeltaTime` and passed to each collection's `game::FrameInterpolate::Update(...)` (`GameBase::Render`), which does per-collection sub-tick smoothing (positions, orientations, animation state) rather than a single global velocity extrapolation

## See Also

- [Game Reconciliation](GameReconciliation.md) — detailed reconciliation pipeline walkthrough
- [Frame Update Pipeline](FrameUpdatePipeline.md) — per-frame phase ordering
