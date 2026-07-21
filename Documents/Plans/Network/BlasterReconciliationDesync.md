# Eliminate the Blaster Reconciliation Desync

## Context

Under the `kChina` network-simulation preset with zoom-driven subscription churn, coord `(-1,0)` diverged at tick `3286`: the server CRC was `0x93E05E2A5C04036E`, the client CRC was `0x76BA63E0EE3EF0B1`, both sides reported zero StatusChanges, and the most recent full state was only 13 ticks earlier. A full rollback/replay still mismatched. The requested server debug-frame diff showed `BlastersInterpolate` and `BlastersPostRender` counts of client `0` / server `1`. This was the first of three recovery attempts and sent a resync request; the server handled the report, debug-frame request, and resync request.

This proves a true Blaster-state divergence, but not its root cause. Profiler-tolerance validation established healthy `kChina` reconciliation behavior; repairing this CRC/determinism failure is outside display-only tolerance calibration and remains an independent gap.

## Design

1. Deterministically reproduce the failure before editing behavior. Run the client/server under `kChina` with a fixed zoom/subscription-churn script and capture both sides at the first divergent tick, including coord membership, full-state adoption tick, ordered StatusChanges, Blaster counts and shared fields, and the spawn/transfer/destroy phase that first differs. Preserve the reproduction inputs and decisive log anchors so the investigation follows the first divergence rather than the later recovery symptom.
2. Inspect the shared creation and full-state serialization/adoption boundaries first: `game::SpawnTransfer`, its server `ServerTransferManager` and client `ReconcileReplayTick` callers, `Frame::ServerRead`, `Server::SendCoordFullState`, `Client::ServerCoordFullState`, and `DecompressAndReadFrame`. Then trace the divergent Blaster through every boundary that can change its presence: weapon-originated spawn, `ClientInitAll`, cell-boundary transfer request and server/client transfer spawn, destruction/collision, rollback-base selection, per-tick replay, and CRC validation. Compare client/server phase ordering and source data at the first divergent tick. Treat the landed subscription-lifecycle and full-state edge handling as current architecture, not as pending plans or dependencies; unrelated changes remain out of scope unless first-divergence evidence requires them.
3. Fix only the proven source of the missing or extra Blaster. Preserve deterministic client/server phase ordering and random-engine draw parity. Do not weaken mismatch escalation, exclude either Blaster collection from shared CRC, or mask the divergence by broadening network-simulation tolerances.
4. Re-run the fixed deterministic reproduction, then repeat the fixed-script `kChina` zoom/subscription-churn soak for at least 120 seconds and at least 6 visible-neighbor/subscription transitions. Verify zero occurrences of the exact `CONFIRMED DESYNC after full rollback/replay` marker, verify that expected speculative mismatches reconcile, and verify that independent replay CRC/determinism remains intact.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.cpp` — `BlastersPostRender::Spawn`, `Transfer`, and `Destroy`; `BlastersInterpolate::ClientInitAll`; both collections' `LogDifferences` members.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/BlastersUpdate.cpp` — Blaster update, collision, and destruction decisions that precede transfer/spawn phase changes.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersCombat.cpp` and `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp` — weapon-originated calls to `BlastersPostRender::Spawn`.
- `Projects/BrokenEngineSandbox/Source/SpawnTransfer.cpp` — `game::SpawnTransfer`, the shared transfer-creation boundary called by `ServerTransferManager` and `ReconcileReplayTick`.
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — `Frame::ServerRead` and its collection deserialization path.
- `Engine/Source/Network/Server/ServerSend.cpp` — `Server::SendCoordFullState`, the authoritative full-state serialization/send boundary.
- `Engine/Source/Network/Client/ClientReceive.cpp` — `Client::ServerCoordFullState` and `DecompressAndReadFrame`, the client full-state decompression/adoption boundary.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp` — full-state adoption and Blaster client initialization.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp`, `ReconcileReplayTick.cpp`, and `ReconcileReplayCrc.cpp` — rollback-base selection, `kTransferBlaster` replay, per-tick reconstruction, and CRC validation.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientReconciler.cpp` — final confirmed-desync aggregation and recovery handoff.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerTransferManager.cpp` and `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — authoritative transfer spawn and `TransferData` serialization boundaries; modify only if the first-divergence trace reaches them.

## Out of scope

- Profiler annunciator or `NetworkSimulationBounds` tuning; this plan owns only the proven Blaster divergence.
- The landed subscription-lifecycle and full-state edge handling are current architecture to inspect; unrelated changes remain out of scope unless first-divergence evidence proves they are prerequisites.
- Redesigning reconciliation, transfer protocols, or Blaster gameplay beyond the smallest proven desync fix.
- Suppressing confirmed-desync logs, weakening CRC coverage, or excluding Blaster state from replay validation.

## Acceptance criteria

- A deterministic capture identifies the first client/server Blaster divergence and proves its source before the fix is selected.
- The proven reproduction no longer produces differing `BlastersInterpolate` or `BlastersPostRender` state after rollback/replay.
- A fixed-script `kChina` zoom/subscription-churn soak runs for at least 120 seconds and crosses at least 6 visible-neighbor/subscription transitions with zero occurrences of the exact `CONFIRMED DESYNC after full rollback/replay` marker; speculative mismatches may occur only when reconciliation subsequently resolves them.
- Replay determinism and shared CRC verification pass with Blasters still covered, alongside Debug client and server builds.
- Any wire, `Frame::kiVersion`, save/replay compatibility, or collection-layout change discovered as necessary is surfaced and versioned explicitly before implementation rather than inferred from this plan.

## Notes

- Future implementation is Tier 3: it touches CRC/determinism, rollback/replay, cross-frame full-state and transfer integration, and mirrored client/server simulation behavior.
- Execution card: goal — eliminate the proven Blaster count divergence; boundary — diagnose first and change only the proven source; invariants — shared CRC coverage, deterministic phase order, RNG parity, replay compatibility, and client/server symmetry; decisive checks — fixed reproduction, repeated `kChina` churn, replay CRC, and both Debug targets; roles — implementer for diagnosis/fix and harness evidence, reviewer for plan/correctness/adversarial review, mechanic for C++ style, builder for both targets.
- No dependency is recorded. If fresh first-divergence evidence proves an existing plan is prerequisite, update the queue relationship then; do not assume it from the current symptom.
