# Jitter Measurement Skew at Sub-Tick-Rate Client FPS

## Context

The client's interarrival-jitter estimate over-reports when the client renders below the sim tick rate, inflating `miCurrentTargetBehind` and adding unwarranted simulation latency for slow-rendering clients.

The jitter block lives in the coord-update receive handler (`Client::ServerCoordUpdate`, `ClientReceive.cpp:313-323`, engine, `BT_CLIENT`). It runs only when the echoed ack timestamp advances (`iEchoedTimestampNs > miLastEchoedTimestampNs`) — a monotonic guard that also skips resends. It measures the wall interval since the last processed update (`now - mLastUpdateArrival`), compares it to one timescale-scaled tick period (`iExpectedUs = mTimeStep.SimToWall(game::kTickNs)`), and feeds `iDeviation = |interval - expected|` into `mSmoothedJitterUs`.

The skew: the server broadcasts once per tick, echoing the same client timestamp on every broadcast until the client sends a fresh ack. `Client::SendAck` now sends at most one ack per sim-tick interval (tick-rate-locked), but a client whose render FPS is *below* the tick rate cannot ack every tick — it renders (and polls) less often than the server broadcasts. So consecutive broadcasts carry a repeated echoed timestamp; the guard advances only on the poll that first sees a newly-echoed timestamp, by which point `mLastUpdateArrival` spans *n* ticks of accumulated broadcasts while `iExpectedUs` is still one tick. `iDeviation` inflates by ~(n−1) tick periods, `mSmoothedJitterUs` over-reports, and `ComputeClockCorrectionNs` (`ClientSessionBase.cpp:326-335`) derives a larger `miCurrentTargetBehind`, pushing the sim further behind `latestServerTick` than arrival jitter warrants.

The tick-rate-locked ack throttle (landed in the client→server contract work) does **not** fix this: a sub-tick-rate-FPS client still cannot ack faster than it renders, so the repeated-echo interval remains multi-tick.

## Design

Normalize the measured interval by the number of ticks it actually spans before feeding `mSmoothedJitterUs`. Two directions to weigh at grill:

- **A — divide by processed tick delta.** Track the tick the last jitter sample was taken at (the coord-update `iTick` is already read in the handler); divide the measured `iIntervalUs` by `(iTick - iLastJitterTick)` before computing `iExpectedUs`-relative deviation, so a multi-tick gap contributes its per-tick average interval. Handles arbitrary FPS ratios.
- **B — sample only when the tick advanced by exactly one.** Skip the jitter update when `iTick - iLastJitterTick != 1`; only true one-tick arrivals feed the estimate. Simpler, but at steady sub-tick-rate FPS it may sample rarely (jitter estimate goes stale) — verify the sample cadence stays adequate before choosing this.

Either way the deviation is measured against a single-tick expectation, so a slow-rendering client's `miCurrentTargetBehind` reflects real arrival jitter rather than render-cadence aliasing.

## Critical files

- `Engine/Source/Network/Client/ClientReceive.cpp` — `Client::ServerCoordUpdate` jitter block (`:313-323`): the interval measurement + `mSmoothedJitterUs` update; add tick-delta normalization / gating. `mLastUpdateArrival`, `miLastEchoedTimestampNs`, and any new last-jitter-tick member live on `Client` (`Client.h`).
- `Engine/Source/Network/Client/ClientSessionBase.cpp` — `ComputeClockCorrectionNs` (`:326-335`): consumer of `mSmoothedJitterUs` → `miCurrentTargetBehind`; no change expected, cited as the downstream effect to validate.

## Notes

- **Invariant exposure**: client-only (`BT_CLIENT`), send/receive-timing and clock-correction only. `mSmoothedJitterUs` / `miCurrentTargetBehind` are **not** CRC'd and never enter the deterministic sim — this is a metric-quality fix, no wire-format / `kuiProtocolVersion` / `kiVersion` change, no determinism exposure. Consistent with the hub's smoothness-over-latency convention (removes *excess* buffer, so validate it does not under-buffer a genuinely jittery slow client).
- Grill decision to pre-stage: A (tick-delta normalize) vs B (one-tick-only sample) — B's sample-cadence adequacy at steady low FPS is the open question.
- Verify against a live client capped below the tick rate: `miCurrentTargetBehind` should track the same arrival jitter as an uncapped client, not inflate with falling FPS.

## Out of scope

- The ack send cadence itself — already tick-rate-locked by the client→server contract work; this plan is purely the *measurement* side.
- Packet-loss-percent metric expected-frame scaling (separate `ClientSessionBase` metric; not affected by the repeated-echo skew).
- Any change to `kiJitterSafetyUs` or the 2-tick `targetBehind` hysteresis (fixed wall-clock margins, deliberately FPS-independent).
- Server-side broadcast/echo behavior (server correctly echoes the last-received timestamp).
