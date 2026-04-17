# Refactor: Decompose Oversized Functions

Source: /external-refactor-clean on Engine/Source/Network + Projects/BrokenEngineSandbox/Source/Network (recursive)

Several functions exceed the project's ~100-line guideline. Each has natural sub-phase boundaries (often annotated as existing comments). Split into named helpers that take the per-call context as a struct reference.

## Changes

### Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp — `ReconcileCoord` (lines 458-664, 207 lines)
- Existing comment boundaries: "Aggressive CRC walk" (477-506), "Determine rollback base" (522-554), "Primary replay" (556-577), "Two-tier fallback" (581-623), "Output layout" (632-663). Extract each into a static helper taking `CoordWork&` and the shared scratch. [~2h]
- Target: 207 → ~80 line orchestrator body + 5 helpers averaging ~25 lines. [~included]

### Projects/BrokenEngineSandbox/Source/Network/Client/ReconcileReplay.cpp — `ReconcileRunTickCoord` (lines 79-182, 104 lines)
- Extract transfer-logging block at 146-165 into `LogTransferSummary(const CoordWork& rWork, int64_t iTick, ...)`. [~30m]

### Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp — `PollNetwork` (lines 54-152, 99 lines)
- Extract per-event switch at 86-132 into `ApplyPlayerEvent(const ReceivedPlayerEvent& rEvent, engine::GridCoord preEventClientCoord, auto updatePlayerCoord)`. [~45m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp — Save/Load symmetry (lines 682-773)
- `WriteFleetData` and `ReadFleetData` are symmetric ~45-line giants with manual field-by-field I/O. Extract `WriteFleet(Fleet&)` / `ReadFleet(Fleet&)` helpers that each handle a single fleet's fields; outer functions become thin loops over `mFleets`. [~1h]

### Engine/Source/Network/Server/ServerSend.cpp — `SendResends` (lines 217-317, 101 lines)
- Extract resend-log hysteresis at 299-316 into `UpdateResendLogState(ClientConnection& rClient, int64_t iSlot, int iSlotResendCount, GridCoord coord)`. [~30m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp — `ResetClientsForLoad` (lines 389-459, 71 lines, 4-level nesting)
- Extract inner 2 loops (420-434) into `bool TryRelinkClient(ClientConnection& rClient, ...)` — returns true if re-linked. Outer becomes a cleaner 2-level structure. [~45m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerClientManager.cpp — `NewClients` (lines 50-129, 80 lines)
- Extract relink block (82-122) into `bool TryRelinkNewClient(ClientConnection& rClient, ...)`. [~45m]

### Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp — `OnResetForLoad` (lines 550-611, 62 lines, 4-level nesting)
- Extract inner loops into `ResetFleetForLoad(Fleet& rFleet)`. [~30m]

### Engine/Source/Network/Client/Client.cpp — `Poll` (lines 76-166, 90 lines)
- `Client::Poll` contains a sim-vs-direct branch at 117-135 that is textually duplicated as the delayed-packet handler at 143-157. Extract `DispatchIncoming(ENetEvent& rEvent)` shared between the two. [~45m]

### Engine/Source/Network/Server/Server.cpp — `Poll` (lines 54-120)
- Mirrors `Client::Poll`: same sim/direct duplication. Apply the same `DispatchIncoming` extraction. [~45m]

## Verification
- Each extraction is behavior-preserving; rebuild + local session after each.
- For the RNG-sensitive path (`ReconcileCoord`): run the replay determinism check from `Architecture_FleetRngDeterminism.md` after this refactor lands too.

## Verification Notes
Verified at commit d08678d3:
- `ReconcileReplay.cpp:458-664` — `ReconcileCoord` spans exactly 207 lines as claimed.
- `ReconcileReplay.cpp:79-182` — `ReconcileRunTickCoord` confirmed.
- `ClientSession.cpp:54-152` — `PollNetwork` spans 99 lines as claimed.
- `ServerSession.cpp:389-459` — `ResetClientsForLoad` confirmed (note: file is 473 lines so function ends within file).
- `Client.cpp:76-166` — `Poll` confirmed; sim/direct branch duplication at 117-135 vs 143-157 is real.
Line-count growth during extraction may push some of these files across thresholds temporarily; re-run `Refactor_FileSizeTriage` after.
