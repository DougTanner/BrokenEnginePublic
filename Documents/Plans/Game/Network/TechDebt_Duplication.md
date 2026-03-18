# Tech Debt: Network Code Duplication

Source: /external-tech-debt on Projects/BrokenEngineSandbox/Source/Network

## Changes

### Projects/BrokenEngineSandbox/Source/Network/ClientReconciler.cpp
- Lines 267-280 (ApplyCoordWriteback) vs lines 314-326 (ApplyResult desync path): Both restore unconsumed server updates and pending full state with nearly identical logic. Extract a shared helper function `RestoreUnconsumedServerData(CoordReconcileWork& rWork, engine::CoordFrames& rSub)` that handles the `serverUpdates` merge (insert_or_assign for ticks > confirmed) and `pendingFullState` preservation (keep newer). Call it from both ApplyCoordWriteback and the desync path in ApplyResult [~15m]

## Verification Notes
- Scope is narrower than the full blocks: the helper covers ~13 lines of shared logic (server updates loop + pending full state check). The desync path also restores `iConfirmedOffset`, `iSnapshotHead`, snapshots swap, and `iSnapshotCount` which are NOT shared with ApplyCoordWriteback.
