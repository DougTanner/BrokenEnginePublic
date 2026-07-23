<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Capture Cross-Coordinate Transfers in Replays

## Context

Replay determinism verification for server pause/reset behavior failed on both the session build and the untouched `962fc79a74546f55df38a36063dd9610e6eac1a8` baseline. At recorded tick `39221`, coord `(2,0)` gained a Blaster and its `.fullframes` payload grew, but that coord's `.frames` input stream remained empty. Playback reached tick `39222` with saved `BlastersInterpolate::iCount == 1` and reconstructed `iCount == 0`.

The root cause is outside the pause-gating change. `GameSaveLoad::SyncReplayTick` records only each coord's current `FrameInput` before simulation (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:459-468`) and playback reconstructs only those inputs before checksum validation (`:478-489`). Cross-coordinate transfers are harvested after the tick (`Engine/Source/GameBase.cpp:269-279`): `ServerTransferManager::HarvestTransfers` directly spawns them into destination frames (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerTransferManager.cpp:209-243`), while replay playback deliberately disables harvesting. The authoritative transfer event therefore changes the recorded frame and checksum without entering the recorded input stream that playback needs to reproduce it.

This is a proven pre-existing replay-capture defect, not the live-client Blaster reconciliation failure owned by `Documents/Plans/Network/BlasterReconciliationDesync.md`. The user asked the pause-semantics session to determine whether the crash belonged in that change or a bugfix plan; the main session adjudicated this independent root cause as a deferred follow-up under the minimum-sufficient scope boundary.

## Design

1. Add an explicit replay-only capture path for authoritative destination transfer events. Preserve the existing live simulation order: harvest and spawn transfers after `RunFrameTick`, then make the exact ordered transfer `StatusChange`s available to the destination coord's next replay-writer update so playback's existing `ApplyTransferStatusChanges` step reconstructs the recorded post-harvest frame before checksum validation.
2. Cover all contiguous transfer kinds accepted by `IsTransferType`: player, spaceship, Blaster, and missile. Preserve the server's deterministic transfer ordering, payload bytes, destination coord, RNG draw order, and CRC composition; do not re-run transfer discovery during playback.
3. Define and implement lifecycle ownership for the replay-only pending transfer data:
   - recording start must not duplicate transfers already present in the saved start frame;
   - normal multi-tick recording must capture each harvested transfer exactly once at the tick/checksum boundary it affected;
   - recording stop must flush any transfers harvested after the last writer update before publishing the terminal frame/checksum;
   - coord retirement through `RetainReplayEndFrame` must retain or flush the transfer input needed to reconstruct that coord's terminal frame;
   - reset, failed recording publication, replay load, and repeated replay loops must clear transient capture state without leaking events into a later session.
4. Pre-stage the architectural decision for destination coords created after recording starts. The existing writer set is created only from the active coords at recording start, so it cannot represent a later `kTransferPlayer` that activates a missing destination. The preferred model is to capture a pre-transfer start snapshot when that destination first enters the recording, persist its exact activation/membership tick, and activate its reader at that tick: playback must omit the coord before activation and introduce it exactly when the recorded server did. Reject unbounded pre-creation of possible destination coords. The current per-coordinate manifest/difference format does not already encode this lifecycle; `/external-grill-plan` and explicit user approval must resolve the replay-format/version and invalidation implication before implementation.
5. Keep ordinary transfer capture in the existing per-coordinate replay input model only where that model represents the required tick boundary. Any persisted `FrameInput`, replay manifest, or difference-stream format change requires the approved design above plus explicit version/invalidation handling before editing; do not add compatibility readers or silently reuse an old version for new bytes.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` / `.h` — `SyncReplayTick`, writer lifecycle, `RetainReplayEndFrame`, stop/failure cleanup, and replay-loop reset.
- `Engine/Source/GameBase.cpp` — `FinalizeFrameTick` ordering between replay capture, transfer harvesting, frame swap, and input clearing.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerTransferManager.cpp` / `.h` — authoritative ordered transfer collection and destination spawn boundary.
- `Projects/BrokenEngineSandbox/Source/Game.cpp` / `.h` — `HarvestTransfers` and playback `ApplyTransferStatusChanges` boundary.
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — transfer-kind range and `TransferData` payload contract; change only if an explicit persisted-format design requires it.
- `Engine/Source/File/DifferenceStream.h` — writer tick/checksum indexing and persisted difference format; inspect first, modify only if the existing `FrameInput` stream cannot carry the capture.

## Out of scope

- Live client/server reconciliation and the separate `BlasterReconciliationDesync` investigation.
- Changing transfer gameplay, destination-liveness policy, subscription ownership, or RNG behavior.
- Weakening replay checksum validation, omitting transferred collections from CRC, or masking the mismatch.
- Backward-compatible replay readers or save/wire-format changes without a separately approved version design.

## Acceptance criteria

- A deterministic agent-harness recording forces each transfer type across a coord boundary and proves its destination replay input contains the transfer needed to reproduce the recorded post-harvest checksum.
- Single-tick and multi-tick recordings replay without CRC differences, including a transfer on the first recorded tick and a transfer immediately before recording stop.
- A coord that retires after receiving or emitting a transfer replays through its terminal frame without a missing/duplicate entity or truncated reader.
- A player transfer into a previously inactive destination records that coord from a pre-transfer start snapshot: playback has no destination coord before the persisted activation tick, creates it exactly at activation with the transferred player present once, and preserves the same behavior across repeated loops and subsequent coord retirement.
- The same saved replay completes at least three automatic replay loops with identical ticks, coord membership, collection counts, and CRCs, with no event leaking across loops.
- The original tick-`39222` Blaster-transfer reproduction plays without a `BlastersInterpolate` count mismatch.
- Debug client and server builds pass. Any required replay-format/version invalidation is explicitly approved and verified; otherwise existing format/layout/version bytes remain unchanged.

## Notes

- Tier 3: changes replay persistence and CRC-relevant cross-coordinate simulation reconstruction.
- Scoring: Effort 3, Impact 5, Risks 3, Score 1. The change is several-file deterministic replay work with a proven critical acceptance failure and high invariant exposure.
- No wire protocol or `.pack` data exposure is intended. Frame/StatusChange serialization, replay versions, and client/server affinity must be re-audited if the selected capture mechanism changes persisted bytes.
