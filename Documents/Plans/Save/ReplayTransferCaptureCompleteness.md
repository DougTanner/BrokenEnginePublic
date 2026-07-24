<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Capture Cross-Coordinate Transfers in Replays

## Context

Replay determinism verification for server pause/reset behavior failed on both the session build and the untouched `962fc79a74546f55df38a36063dd9610e6eac1a8` baseline. At recorded tick `39221`, coord `(2,0)` gained a Blaster and its `.fullframes` payload grew, but that coord's `.frames` input stream remained empty. Playback reached tick `39222` with saved `BlastersInterpolate::iCount == 1` and reconstructed `iCount == 0`.

The root cause is outside the pause-gating change. During recording, `GameSaveLoad::SyncReplayTick` (`Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp`) records only each coord's current `FrameInput` in its per-writer `Update` loop before simulation, and playback reconstructs only those recorded inputs (via `Game::ApplyTransferStatusChanges`) before `ValidateChecksum`. Cross-coordinate transfers are harvested after the tick: `GameBase::FinalizeFrameTick` (`Engine/Source/GameBase.cpp`) calls `Game::HarvestTransfers` → `ServerTransferManager::HarvestTransfers` (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerTransferManager.cpp`), which directly spawns transfers into destination frames and recomputes their CRCs, while replay playback deliberately disables harvesting (`IsReplaying()` guard in `FinalizeFrameTick`). The authoritative transfer event therefore changes the recorded frame and checksum without entering the recorded input stream that playback needs to reproduce it.

This is a proven pre-existing replay-capture defect, not the live-client Blaster reconciliation failure owned by `Documents/Plans/Network/BlasterReconciliationDesync.md`. The user asked the pause-semantics session to determine whether the crash belonged in that change or a bugfix plan; the main session adjudicated this independent root cause as a deferred follow-up under the minimum-sufficient scope boundary.

## Design

1. Add an explicit replay-only capture path for authoritative destination transfer events. Preserve the existing live simulation order: harvest and spawn transfers after `RunFrameTick`, then make the exact ordered transfer `StatusChange`s available to the destination coord's next replay-writer update so playback's existing `ApplyTransferStatusChanges` step reconstructs the recorded post-harvest frame before checksum validation.
2. Cover all contiguous transfer kinds accepted by `IsTransferType` (`Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h`): player, spaceship, Blaster, and missile. Preserve the server's deterministic transfer ordering, payload bytes, destination coord, RNG draw order, and CRC composition; do not re-run transfer discovery during playback.
3. Define and implement lifecycle ownership for the replay-only pending transfer data:
   - recording start (the `SyncReplayTick` block that creates one `ReplayWriterState` per active coord) must not duplicate transfers already present in the saved start frame;
   - normal multi-tick recording must capture each harvested transfer exactly once at the tick/checksum boundary it affected;
   - recording stop (the `SyncReplayTick` block that saves writers and publishes the manifest) must flush any transfers harvested after the last writer update before publishing the terminal frame/checksum;
   - coord retirement through `RetainReplayEndFrame` must retain or flush the transfer input needed to reconstruct that coord's terminal frame;
   - reset, failed recording publication, replay load (`SaveLoadReplay`), and repeated replay loops must clear transient capture state without leaking events into a later session.
4. Pre-stage the architectural decision for destination coords created after recording starts. The existing writer set is created only from the active coords at recording start, so it cannot represent a later `kTransferPlayer` that activates a missing destination. The preferred model is to capture a pre-transfer start snapshot when that destination first enters the recording, persist its exact activation/membership tick, and activate its reader at that tick: playback must omit the coord before activation and introduce it exactly when the recorded server did. Reject unbounded pre-creation of possible destination coords. The current per-coordinate manifest/difference format does not already encode this lifecycle; `/external-grill-plan` and explicit user approval must resolve the replay-format/version and invalidation implication before implementation.
5. Keep ordinary transfer capture in the existing per-coordinate replay input model only where that model represents the required tick boundary. Any persisted `FrameInput`, replay manifest, or difference-stream format change requires the approved design above plus explicit version/invalidation handling before editing; do not add compatibility readers or silently reuse an old version for new bytes.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the design and acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants permission to touch only the named functions, members, and regions in it, plus the mechanical necessities (includes, forward declarations, member declarations) the named change requires.

### In scope

- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — inside `SyncReplayTick` only: the recording-start block (per-coord `ReplayWriterState` creation), the recording-stop block (writer save/flush before manifest publication), the recording-tick writer `Update` loop, and the playback reader loop (`LoadDifference` / `ApplyTransferStatusChanges` / `ValidateChecksum`). Also `RetainReplayEndFrame`, `ResetStreams`, and the replay-load reset path inside `SaveLoadReplay`, each only as far as design item 3's lifecycle requires.
- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.h` — the `ReplayWriterState` struct and any new private members/declarations the capture lifecycle requires; no other API changes.
- `Engine/Source/GameBase.cpp` — `GameBase::FinalizeFrameTick` only: the ordering between replay capture, `Game::HarvestTransfers`, `SwapFrames`, `CompleteTick`, and the per-coord `statusChanges.clear()` loop.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerTransferManager.cpp` / `.h` — `HarvestTransfers` and the minimal member/accessor exposure needed to hand the ordered per-destination transfer `StatusChange`s to the replay writer; no change to transfer discovery, ordering, or spawn behavior.
- `Projects/BrokenEngineSandbox/Source/Game.cpp` / `.h` — `Game::HarvestTransfers` and `Game::ApplyTransferStatusChanges` only, at the capture/playback boundary.
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — inspect the `IsTransferType` range and `TransferData` payload contract; modify only if the explicitly approved persisted-format design of design item 4 requires it.
- `Engine/Source/File/DifferenceStream.h` — inspect first (writer tick/checksum indexing and persisted difference format); modify only if the existing `FrameInput` stream cannot carry the capture, and then only under design item 5's version/invalidation requirement.

### Out of scope

- Live client/server reconciliation and the separate `BlasterReconciliationDesync` investigation.
- Changing transfer gameplay, destination-liveness policy, subscription ownership, or RNG behavior.
- Weakening replay checksum validation, omitting transferred collections from CRC, or masking the mismatch.
- Backward-compatible replay readers or save/wire-format changes without a separately approved version design.
- Any function, member, or file not named above, including other regions of the named files.

## Acceptance criteria

- A deterministic agent-harness recording forces each transfer type across a coord boundary and proves its destination replay input contains the transfer needed to reproduce the recorded post-harvest checksum.
- Single-tick and multi-tick recordings replay without CRC differences, including a transfer on the first recorded tick and a transfer immediately before recording stop.
- A coord that retires after receiving or emitting a transfer replays through its terminal frame without a missing/duplicate entity or truncated reader.
- A player transfer into a previously inactive destination records that coord from a pre-transfer start snapshot: playback has no destination coord before the persisted activation tick, creates it exactly at activation with the transferred player present once, and preserves the same behavior across repeated loops and subsequent coord retirement.
- The same saved replay completes at least three automatic replay loops with identical ticks, coord membership, collection counts, and CRCs, with no event leaking across loops.
- The original tick-`39222` Blaster-transfer reproduction plays without a `BlastersInterpolate` count mismatch.
- Debug client and server builds pass. Any required replay-format/version invalidation is explicitly approved and verified; otherwise existing format/layout/version bytes remain unchanged.

## Notes

- Tier 3: changes replay persistence and CRC-relevant cross-coordinate simulation reconstruction. `/plan-audit` then `/external-grill-plan` apply; the grill must resolve design item 4's replay-format/version and invalidation decision with explicit user approval before implementation.
- Scoring: Effort 3, Impact 5, Risks 3. The change is several-file deterministic replay work with a proven critical acceptance failure and high invariant exposure.
- No wire protocol or `.pack` data exposure is intended. Frame/StatusChange serialization, replay versions, and client/server affinity must be re-audited if the selected capture mechanism changes persisted bytes.
