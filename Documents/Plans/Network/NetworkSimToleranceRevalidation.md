# NetworkSim Reliable-Latency Tolerance Re-validation

## Context

The landed `NetworkSimulationReliableDelay` change now applies simulated latency to **reliable** control packets (full-state / static-data / resend / subscribe-accept / debug-frame / load-notification), where previously they were zero-latency. Reliable full-state latency deepens client reconciliation/rollback under adverse presets (especially `kChina`). The game profiler's Network-screen annunciators — which append `"!"` when a metric is out of tolerance — were calibrated against the pre-change behavior (zero reliable latency):

- `GetNetworkSimulationBounds` (per-region `NetworkSimulationBounds` in `Engine/Source/Network/NetworkSimulation.h`) consumed at `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.cpp:339-344` (CRC / Assumed / fast / status / knock-on replay-per-second maxima).
- Hard-coded rollback-cap `if (iRollbackValue > 8)` at `ProfileManager.cpp:291` — **not** derived from `NetworkSimulationBounds`.

These may now systematically trip under reliable latency, either as the *intended* "instability surfaced" signal (the reliable-delay change deliberately exposes latent subscription-race bugs) or as *stale* tolerances that no longer distinguish expected latency from a real problem.

This is a **measurement-gated** tuning task: it needs a live simulated run — now agent-executable via the agent-harness skill — so it was recorded as a follow-up rather than resolved inline when the source plan completed.

## Design

1. Run client + server under `keNetworkSimulation = kChina` (spot-check lighter presets too), observe the Network-screen annunciators (`!`) and the rollback cap over a sustained session with subscription churn.
2. If the `NetworkSimulationBounds` maxima (`iCrcMin` / `iAssumedMax` / `iFastReplayMax` / `iStatusReplayMax` / `iKnockOnReplayMax`) no longer bracket steady-state reconciliation depth with reliable latency, re-tune the per-region rows in `GetNetworkSimulationBounds`.
3. If the hard-coded `iRollbackValue > 8` cap cries wolf under the new baseline, adjust it (or derive it from the bounds like the others).
4. If the annunciators read correctly as-is (tripping only on genuine trouble), **close accept-and-document** — no code change; the surfacing IS the intended behavior.

## Out of scope

- Fixing the subscription-race bugs the reliable latency now exposes (separate queued plans: `SubscriptionLifecycleRaceHardening`, `ClientFullStateEdgeFixes`, …).
- Any change to the reliable-delay mechanism itself (landed).

## Notes

- Client-only profiler-overlay tuning (the annunciators are display-only). No wire / CRC / `kiVersion` / determinism exposure — the `NetworkSimulationBounds` values feed only the profiler validation, never the sim.
- Likely outcome is accept-and-document once measured, unless a tolerance is clearly stale.
