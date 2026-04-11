# Network Architecture

**IMPORTANT**: Every Frame must be independent from all others, except for generating/consuming StatusChange events.

## Server

- Multi-threaded Frame Tick update, spread over cores
- Each Tick is simulated exactly once: previous Frame Tick(Crc) + StatusChanges(Crc) → new Frame Tick(Crc)
- When a Tick is ready, send its StatusChanges + Crc to the client (one unreliable packet per active subscription slot)
  - Client confirms ASAP via an ACK stream carrying, per-slot, `(slot, epoch, floor, bitfield-low, bitfield-high)`. The bitfield covers 128 ticks above the floor
- **Resend**: per client, scan each slot's ACK bitfield for unset bits above the floor (oldest-first) and re-send only those specific missing ticks as `kServerCoordResend` packets, capped at `kiMaxResendFrames = 8` per call. The server does NOT blindly re-send the entire range from oldest-unacked — only the exact gaps the client reported. Because the scan is oldest-first and the floor never advances past an unacked tick, the oldest gap is always prioritized: if >8 gaps exist, newer ones wait for the next `SendResends` call. An unacked tick keeps getting retried until it either arrives or is evicted from the per-coord ring buffer (logged as `Evicted frame`)

## Client Global

- Client sim runs strictly **behind** `latestServerTick` by `miCurrentTargetBehind` ticks. In zero-loss steady state, the server's packet carrying a StatusChange for tick N always arrives before the client has simulated tick N, so StatusChanges fold in on the first simulation of that tick and reconcile never has to replay
- **`miCurrentTargetBehind`**: pure jitter buffer, computed as `ceil((measuredJitterUs + kiJitterSafetyUs) / tickTimeUs)` where `kiJitterSafetyUs = 125'000` (125 ms wall-clock, = 4 ticks at 32 Hz). RTT is deliberately NOT in the formula — it is already built into `latestServerTick` lagging server wall clock by one-way latency, so `targetBehind` only needs to cover arrival jitter plus a fixed safety margin. Hysteresis: updated only when the new value differs by 2+ ticks to avoid oscillation
- **Hard ceiling**: `miTickCounter` is clamped in `GameBase::ClientUpdate` so it can never advance past `latestServerTick - miCurrentTargetBehind`. Excess wall-clock time is absorbed back into the tick accumulator (bounded by the existing `kiMaxAccumulatorTicks` cap). Render smoothness during brief clamps is preserved by `mTickRemainderNs` + velocity extrapolation from the snapshot tail
- **Clock error** = `preReconcileTick - (latestServerTick - miCurrentTargetBehind)`. Positive error means sim is past the ceiling (transient only — the clamp prevents sustained drift ahead); negative error means sim is behind the ceiling and needs to catch up. Small errors are corrected gradually by nudging the tick accumulator up to 4 steps per frame. Errors ≥ `kiClockErrorDisconnectThreshold` accumulate a consecutive-frame counter and disconnect if sustained. Extreme errors (≥ `kiClockSnapThreshold`) hard-snap `miTickCounter` to `latestServerTick - miCurrentTargetBehind` and clear the accumulator — this covers first-packet initialization (sim = 0, target = ~1356 → snap fires) and long-stall recovery

## Client per Frame

- Ring buffer of Ticks, saving old Ticks + their Crc, plus a separate buffer of received server updates (StatusChanges + Crc) awaiting validation
- **Fast-path drop (CRC match)**: walk the ring oldest-first; for each Tick with a matching server Crc, advance `iConfirmedTick` past it and drop it from the ring (always keep at least the most recent). Validated server updates are consumed from their buffer in the same pass
- **Mismatch path (rollback + replay)**: when the oldest un-confirmed Tick's Crc disagrees with the server's:
  1. Rollback to `iConfirmedTick` (discard all speculative ticks past it)
  2. Replay each buffered server update tick-by-tick, applying its StatusChanges and CRC-validating against the server's Crc. Matching ticks advance `iConfirmedTick` in the same pass
  3. If a replayed Tick still CRC-fails *with* its StatusChanges applied, that is a genuine desync → escalate to resync/disconnect
  4. This is strictly better than "re-simulate forward with empty inputs": buffered server updates carry StatusChanges that must be folded in, otherwise every subsequent Tick would immediately mismatch again and thrash reconciliation every frame until the buffer drains
- **Forward sim**: from the most recent Tick remaining, simulate empty-input ticks until the ring tail is less than one Tick behind the target time (`miTickCounter`, advanced by `TickRealtime()` before Reconcile runs)
- **Render**: the ring tail is used as the base for the render Frame; the sub-tick remainder (`mTickRemainderNs`) is applied as `fDeltaTime` to extrapolate position = `tail.pos + fDeltaTime * tail.velocity` for smooth interpolation between ticks

## See Also

- [Game Reconciliation](GameReconciliation.md) — detailed reconciliation pipeline walkthrough
- [Frame Update Pipeline](FrameUpdatePipeline.md) — per-frame phase ordering
