# Architecture: Network File Size Reduction

Source: /external-architecture-review on Projects/BrokenEngineSandbox/Source/Network

Two files exceed the 500-line soft threshold. Both have natural split points along responsibility boundaries. Each split keeps a single .h file with multiple companion .cpp files (same pattern as ClientSession.cpp / ClientSessionSubscriptions.cpp).

## Changes

### Projects/BrokenEngineSandbox/Source/Network/ReconcileReplay.cpp (682 lines)
- Split into 3 companion .cpp files sharing ReconcileReplay.h:
  - `ReconcileReplay.cpp` — CRC fast-path: FindSnapshotIndex, CrcValidateLoop, CrcApplyMatchResult, CrcFastPathProcessCoord (~130 lines, current lines 42-186)
  - `ReconcileReplayFullPath.cpp` — Full replay pipeline: CloneFrameViaSerialization, ReconcileInjectPendingFullState, ReconcileRollbackCoord, ReconcileFindReplayRangeCoord, ReconcileRunTickCoord, ReconcileValidateCrcCoord, ReconcileReplayCoord, ReconcileCatchUpCoord, ReconcileCoord (~350 lines, current lines 188-558)
  - `ReconcileReplayHumanState.cpp` — Human state tracking: ReconcileUpdateHumanState + extracted FindMatchingPlayerInCoord helper (~120 lines, current lines 560-678)
- Also move logging helpers (LogStatusChangeDetail, LogStatusChangeList) to whichever file uses them, or a shared location [~1h]

### Projects/BrokenEngineSandbox/Source/Network/ServerSession.cpp (636 lines)
- Split into companion .cpp files sharing ServerSession.h:
  - `ServerSession.cpp` — Core hooks: PreTickNetwork, PostTickNetwork, PrepareTick, BroadcastTick, SendResends (~100 lines)
  - `ServerSessionActiveSet.cpp` — Active set computation: ComputeActiveSet, AddSubscribedCoords, AddNeighborCoords, EnsureSpecialCoords, SyncActiveFrames (~100 lines)
  - `ServerSessionBroadcast.cpp` — Frame I/O: BuildFrameInputs, BroadcastStatusChanges (~135 lines)
  - `ServerSessionTransfers.cpp` — Transfer harvesting: HarvestTransfers, CollectTransfers, SpawnTransfers, TrackHumanTransfers (~125 lines)
  - `ServerSessionLifecycle.cpp` — Client lifecycle: FinalizeNewClients, HandleDisconnects, DetectPlayerDeaths, SubscriptionUpdates, HandleResyncRequests (~175 lines)
- [~1h]

## Verification Notes
- Line counts and split points verified against actual code. Both files confirmed over 500-line threshold. Follows existing companion .cpp pattern (ClientSession.cpp / ClientSessionSubscriptions.cpp). Logging helpers (LogStatusChangeDetail, LogStatusChangeList) are used only in ReconcileReplay.cpp and should stay with the full replay path file.
