# Architecture: ReconcileUpdateHumanState Refactoring

Source: /external-architecture-review on Projects/BrokenEngineSandbox/Source/Network

## Changes

### Projects/BrokenEngineSandbox/Source/Network/ReconcileReplay.cpp
- Lines 632-671: Extract the destination coord search and position-matching logic into a helper function `FindMatchingPlayerInCoord(ReconcileContext&, GridCoord destination, XMVECTOR position) -> std::optional<player_t>`. This reduces the nesting depth in `ReconcileUpdateHumanState()` from 6 levels to 3 levels. The helper encapsulates: finding the destination coord in coordWork, getting the dest frame pointer (from replay stack or fast-path snapshot), and iterating players for position match [~15m]

## Verification Notes
- Line numbers verified correct. Interacts with Architecture_FileSplit.md — if file split is done first, this extraction happens in `ReconcileReplayHumanState.cpp`. Either order works.
