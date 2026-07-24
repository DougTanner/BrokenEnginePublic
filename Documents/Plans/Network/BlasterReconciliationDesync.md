<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Eliminate the Blaster Reconciliation Desync

## Context

Under the `kChina` network-simulation preset (`engine::NetworkSimulationLevel::kChina`, `Engine/Source/Network/NetworkSimulation.h`) with zoom-driven subscription churn, coord `(-1,0)` diverged at tick `3286`: server CRC `0x93E05E2A5C04036E`, client CRC `0x76BA63E0EE3EF0B1`, both sides reported zero StatusChanges, and the most recent full state was only 13 ticks earlier. A full rollback/replay still mismatched, emitting the `CONFIRMED DESYNC after full rollback/replay` marker (logged in `ClientReconciler.cpp`). The requested server debug-frame diff showed `BlastersInterpolate` and `BlastersPostRender` counts of client `0` / server `1`. This was the first of three recovery attempts and sent a resync request; the server handled the report, debug-frame request, and resync request.

This proves a true Blaster-state divergence, but not its root cause. Profiler-tolerance validation established healthy `kChina` reconciliation behavior; repairing this CRC/determinism failure is outside display-only tolerance calibration and remains an independent gap. The plan is diagnosis-first: the exact defective region is unknown until the first-divergence trace proves it, so the scope contract below names the closed candidate set and forbids behavior edits outside it.

## Design

1. **Reproduce deterministically before editing behavior.** Run the client/server under `kChina` with a fixed zoom/subscription-churn script (agent harness) and capture both sides at the first divergent tick: coord membership, full-state adoption tick, ordered StatusChanges, Blaster counts and shared fields, and the spawn/transfer/destroy phase that first differs. Preserve the reproduction inputs and decisive log anchors so the investigation follows the first divergence, not the later recovery symptom. Temporary diagnostic logging added for this step is confined to the in-scope regions listed below and is removed before finalization unless the fix proves a permanent log is required.
2. **Trace the shared creation and full-state boundaries first**, in this order: `game::SpawnTransfer` (`SpawnTransfer.cpp`), its server caller `ServerTransferManager::SpawnTransfers` and client replay caller `ReconcileRunTickCoord`, `Frame::ServerRead` (`Frame.cpp`), `Server::SendCoordFullState` (`ServerSend.cpp`), `Client::ServerCoordFullState` and `DecompressAndReadFrame` (`ClientReceive.cpp`), and `ClientSession::ApplyReceivedFullStates` (`ClientSessionReceive.cpp`). Then follow the divergent Blaster through every boundary that can change its presence: weapon-originated spawn (`PlayersCombat.cpp`, `Spaceships.cpp`), `BlastersInterpolate::ClientInitAll`, cell-boundary transfer collection/spawn (`ServerTransferManager::CollectTransfers`/`SpawnTransfers`/`HarvestTransfers`), `kTransferBlaster` replay in `ReconcileRunTickCoord`, destruction/collision (`BlastersUpdate.cpp`), rollback-base selection (`DetermineRollbackBase`, `ReconcileReplay.cpp`), per-tick replay (`ReconcileReplayCoord`), and CRC validation (`CrcValidateLoop`/`CrcFastPathProcessCoord`, `ReconcileReplayCrc.cpp`). Compare client/server phase ordering and source data at the first divergent tick. Treat the landed subscription-lifecycle and full-state edge handling as current architecture, not pending work.
3. **Fix only the proven source** of the missing or extra Blaster, inside the region the trace identified. Preserve deterministic client/server phase ordering and random-engine draw parity. Do not weaken mismatch escalation, exclude either Blaster collection from the shared CRC, or mask the divergence by broadening network-simulation tolerances.
4. **Re-verify.** Re-run the fixed deterministic reproduction, then repeat the fixed-script `kChina` zoom/subscription-churn soak for at least 120 seconds and at least 6 visible-neighbor/subscription transitions. Verify zero occurrences of the exact `CONFIRMED DESYNC after full rollback/replay` marker, that expected speculative mismatches reconcile, and that independent replay CRC/determinism remains intact.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change that fixes the proven divergence, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires.

### In scope — candidate regions (read/instrument all; behavior-edit only the proven source)

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.cpp` — `BlastersPostRender::Spawn` (both overloads), `BlastersPostRender::Transfer`, `BlastersPostRender::Destroy`, `BlastersInterpolate::ClientInitAll`, `BlastersInterpolate::LogDifferences`, `BlastersPostRender::LogDifferences`.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/BlastersUpdate.cpp` — `BlastersPostRender::Update`, `PreCollision`, `PostCollision`, `AreaDamage`: the update, collision, and destruction decisions that precede transfer/spawn phase changes.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersCombat.cpp` and `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp` — only the weapon-originated `BlastersPostRender::Spawn` call sites.
- `Projects/BrokenEngineSandbox/Source/SpawnTransfer.cpp` — `game::SpawnTransfer`, the shared transfer-creation boundary.
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — `Frame::ServerRead` and its collection deserialization path.
- `Engine/Source/Network/Server/ServerSend.cpp` — `Server::SendCoordFullState`, the authoritative full-state serialization/send boundary.
- `Engine/Source/Network/Client/ClientReceive.cpp` — `Client::ServerCoordFullState` and `DecompressAndReadFrame`, the client full-state decompression/adoption boundary.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionReceive.cpp` — `ClientSession::ApplyReceivedFullStates`, the full-state adoption and Blaster client-initialization site.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp` — `DetermineRollbackBase`, `RunPrimaryReplay`, `RunTwoTierFallback`, `ReconcileCoord`: rollback-base selection and replay orchestration.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplayTick.cpp` — `ReconcileRollbackCoord`, `ReconcileRunTickCoord` (including its `kTransferBlaster` replay handling), `ReconcileReplayCoord`.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplayCrc.cpp` — `CrcValidateLoop`, `CrcApplyMatchResult`, `CrcFastPathProcessCoord`: CRC validation.
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientReconciler.cpp` — the confirmed-desync aggregation and recovery handoff surrounding the `CONFIRMED DESYNC after full rollback/replay` log site.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerTransferManager.cpp` — `ServerTransferManager::CollectTransfers`, `SpawnTransfers`, `HarvestTransfers`, `TrackClientTransfers` — and `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — `SerializeBlasterTransfer`/`DeserializeBlasterTransfer`: authoritative transfer spawn and `TransferData` serialization boundaries. Behavior-edit only if the first-divergence trace reaches them.

### Out of scope

- Profiler annunciator or `NetworkSimulationBounds` tuning; this plan owns only the proven Blaster divergence.
- The landed subscription-lifecycle and full-state edge handling are current architecture to inspect; changes to them are out of scope unless first-divergence evidence proves one is the source.
- Redesigning reconciliation, transfer protocols, or Blaster gameplay beyond the smallest proven desync fix.
- Suppressing confirmed-desync logs, weakening CRC coverage, or excluding Blaster state from replay validation.
- Any function, member, or region in the listed files not named above, beyond the mechanical necessities the proven fix requires.

## Acceptance criteria

- A deterministic capture identifies the first client/server Blaster divergence and proves its source before the fix is selected.
- The proven reproduction no longer produces differing `BlastersInterpolate` or `BlastersPostRender` state after rollback/replay.
- A fixed-script `kChina` zoom/subscription-churn soak runs for at least 120 seconds and crosses at least 6 visible-neighbor/subscription transitions with zero occurrences of the exact `CONFIRMED DESYNC after full rollback/replay` marker; speculative mismatches may occur only when reconciliation subsequently resolves them.
- Replay determinism and shared CRC verification pass with Blasters still covered, alongside Debug client and server builds.
- Any wire, `Frame::kiVersion`, save/replay compatibility, or collection-layout change discovered as necessary is surfaced and versioned explicitly before implementation rather than inferred from this plan.

## Notes

- **Risk tier: Tier 3.** Triggers: CRC/determinism, rollback/replay, cross-frame full-state and transfer integration, and mirrored client/server simulation behavior.
- **Invariants:** shared CRC coverage of both Blaster collections, deterministic client/server phase order, random-engine draw parity, save/replay compatibility, client/server symmetry.
- **Execution card:** goal — eliminate the proven Blaster count divergence; boundary — diagnose first and behavior-edit only the proven source within the in-scope regions; decisive checks — fixed reproduction, repeated `kChina` churn soak, replay CRC, both Debug targets; roles — implementer for diagnosis/fix and harness evidence, reviewer for plan/correctness/adversarial review, mechanic for C++ style, builder for both targets.
- No dependency is recorded. If fresh first-divergence evidence proves an existing plan is prerequisite, update the queue relationship then; do not assume it from the current symptom.
