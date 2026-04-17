# Refactor: Hot-Path Allocations → Workbuffer

Source: /external-refactor-clean on Projects/BrokenEngineSandbox/Source/Network + Engine/Source/Network (recursive)

Several per-tick code paths allocate via `std::vector` / `std::unordered_map` / `std::ostringstream` on the heap (currently covered by `ScopedSuppressAllocationTracking` but still tracked by the allocator). Migrating to `common::gpThreadLocal->mWorkbuffer` eliminates the allocations entirely.

## Changes

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerBroadcaster.cpp
- Line 102: `std::unordered_map<GridCoord, std::vector<StatusChange>> allChanges;` inside `BroadcastStatusChanges`. Replace with a flat `std::span<std::pair<GridCoord, std::span<const StatusChange>>>` built into the workbuffer. [~1h]
- Lines 122-123: `allGridUpdates.reserve(gpGame->mActiveCoords.size());` per-tick vector. Move to workbuffer. [~30m]
- Lines 146-147: `std::vector<std::pair<GridCoord, const game::Frame*>> fullFrames;` inner scope, per-tick. Workbuffer. [~20m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerTransferManager.cpp
- Line 164: `std::vector<ClientTransferInfo> clientTransfers;` per-tick in `HarvestTransfers`. Workbuffer. [~45m]
- Lines 171-177: `preCrcs` map, per-tick. Replace with a parallel array keyed by the coord's dense index. [~45m]

### Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp
- Line 70: `std::vector<ReceivedPlayerEvent> playerEvents;` per-poll. Workbuffer. [~20m]
- Line 136: `std::vector<Fleet> receivedFleets;` per-poll. Workbuffer. [~20m]

### Projects/BrokenEngineSandbox/Source/Network/Client/ClientSessionSubscriptions.cpp
- Line 132: `std::vector<engine::GridCoord> effectiveDesired = mDesiredCoords;` per-`UpdateSubscriptions`. Workbuffer. [~20m]

### Engine/Source/Network/Client/ClientReceive.cpp
- Line 30: `std::string decompressed(iUncompressedSize, '\0');` per full-state / per debug-frame. Replace with a `char*` span into the workbuffer to match `NetworkSerialization.cpp:401-412`. [~30m]

### Engine/Source/Network/Server/Server.cpp
- Lines 299-302: `std::ostringstream frameStream;` per-coord inside `BufferFullFrame` (called per-tick from `BroadcastStatusChanges`). Hot path. Migrate once `game::Frame::ServerWrite` accepts a workbuffer-backed stream sink. [~2h; requires Frame serialization API change]

### Engine/Source/Network/Server/ServerSend.cpp
- Lines 24, 61: `std::ostringstream frameStream;` per-full-state / per-static-data send. Infrequent (subscription boundaries). Low priority; note for future. [~30m, defer]

## Verification
- With `BT_ALLOCATION_TRACKING` enabled: run a local multi-client session; confirm zero `DEBUG_BREAK()` fires in main loop after each migration.
- After all changes: remove the now-unnecessary `ScopedSuppressAllocationTracking` guards in the migrated functions.

## Verification Notes
Verified — all cited line numbers accurate at commit d08678d3:
- `ServerBroadcaster.cpp:102` (`allChanges` map), `:122-123` (`allGridUpdates`), `:146-147` (`fullFrames`).
- `ServerTransferManager.cpp:164` (`clientTransfers`), `:171-177` (`preCrcs`).
- `ClientSession.cpp:70` (`playerEvents`), `:136` (`receivedFleets`).
- `ClientSessionSubscriptions.cpp:132` (`effectiveDesired`).
- `ClientReceive.cpp:30` (`std::string decompressed`).
- `Server.cpp:288-302` (`BufferFullFrame` with `ostringstream` at 299).

All paths are in-frame (per-tick Poll/Broadcast/Reconcile/UpdateSubscriptions); `gpThreadLocal->mWorkbuffer` is idiomatic and available on each. The `Server.cpp:299` `ostringstream` migration is correctly flagged as gated on a `game::Frame::ServerWrite` API change.
