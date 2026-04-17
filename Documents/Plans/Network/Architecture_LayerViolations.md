# Architecture: Engine → Game Layer Violations

Source: /external-architecture-review on Engine/Source/Network + Projects/BrokenEngineSandbox/Source/Network (recursive)

Engine-layer Network code reaches into specific game subsystems (`mGameSaveLoad`, `mGameFlags`, `mTimeStep`, `mCoordFrames` surgery). `gpGame` indirection is compliant by letter but breaks the "Engine generic, Game specific" intent. Converting game-authoritative packet kinds to `kGamePacketStart`-range and pushing CoordFrames mutation back into the game session tightens the boundary.

## Changes

### Engine/Source/Network/Server/ServerReceive.cpp
- Move `kClientSaveRequest` handler (~line 480) body `game::gpGame->mGameSaveLoad.ServerSave()` to game-side `ServerSession::HandleSaveRequest`; change the packet kind to a `kGamePacketStart+` value so engine just forwards raw bytes. [~45m]
- Move `kClientLoadRequest` handler (~line 497) body `mGameSaveLoad.ServerLoad()` + `ResetClientsForLoad` call to game-side equivalent. [~30m]
- Move `kClientResetRequest` handler (~line 548) body `mGameSaveLoad.ServerReset()` to game-side. [~20m]
- Move `kClientReplayRecordRequest`/`kClientReplayPlaybackRequest` (~lines 514, 531) bodies that set `GameFlags::kSaveReplay`/`kLoadReplay` to game-side handlers. [~30m]
- Move `kClientPauseRequest`/`kClientTimespeedRequest` (~lines 432-462) bodies that mutate `mGameFlags::kPaused` / `mTimeStep.DecreaseTimeScale()/IncreaseTimeScale()` to game-side. [~30m]
- After moving: verify engine `ServerReceive.cpp` no longer references `game::gpGame->mGameSaveLoad` or `mGameFlags` or `mTimeStep` members. [~10m]

### Engine/Source/Network/Client/ClientSessionBase.cpp
- `DisconnectFromServerBase` (lines 30-34) iterates `game::gpGame->mCoordFrames` calling `ResetClientState()`. Introduce virtual `OnDisconnect()` on `ClientSessionBase` and move the coord-frames reset into `game::ClientSession::OnDisconnect` override. [~30m]
- `ApplyReceivedUpdatesBase` (lines ~226, 237, 245) calls `game::gpGame->mCoordFrames.at(coord).serverUpdates.try_emplace(...)`. Return the update batches from the base method; let game-side `ClientSession` apply them to `CoordFrames`. [~1h]
- `SendNewSubscriptionFullStates`-analog (lines 370-398) dereferences `mCoordFrames.iConfirmedTick`/`serverUpdates`. Same refactor — return, don't mutate. [~1h]

### Engine/Source/Network/Server/ServerSessionBase.cpp
- `SendNewSubscriptionFullStates` (lines 64-68) reads `game::gpGame->mCoordFrames.find()` + `frameIt->second.staticData` + `frameIt->second.pCurrent.get()`. Introduce a pure-virtual `GetCoordStaticData(GridCoord)` / `GetCoordCurrentFrame(GridCoord)` on `ServerSessionBase` and override in `game::ServerSession`. [~45m]

### Engine/Source/Network/Client/ClientReceive.cpp
- Line 40: `std::make_unique<game::Frame>()` in `DecompressAndReadFrame` hardcodes the concrete game Frame type in the engine. Add a virtual `CreateFrame()` on `ClientSessionBase` (or pass the Frame factory at construction). Sandbox overrides to produce `game::Frame`. [~30m]
- Line 496: direct write `game::gpGame->mTimeStep.SetTimeScale(iMultiply, iDivide)`. Promote to a virtual `OnServerTickRateChanged(int multiply, int divide)` on the engine Client so the game layer owns the time-scale application. [~45m]
- Line 377 (in `ClientSend.cpp`, not `ClientReceive.cpp`): `game::Frame::kiVersion` used by engine client when sending hello — move the version-number read to the game session (coupled with the packet-reclass above). [~15m]

### Engine/Source/Network/Server/Server.cpp
- Lines 81, 111 (simulation branch): reads `game::gpGame->mTimeStep.miTimeMultiply`. If `BufferFullFrame` stops living here (see below), this read can move with it. Otherwise replace with a callback injected at init. [~20m]
- Line 288 `BufferFullFrame(int64_t, const std::vector<std::pair<GridCoord, const game::Frame*>>&)` with `ostringstream` at 299-302: engine serializes the concrete game Frame type. Change signature to accept pre-serialized bytes (`std::span<const uint8_t>`) and have the game-side session call `rFrame.ServerWrite(stream)` before handing off. [~1h]

### Engine/Source/Network/Client/Client.cpp
- Lines 119, 149: `mTimeStep.miTimeMultiply` reads. Fix with the same virtual/callback change. [~20m]

### Engine/Source/Network/NetworkSerialization.h
- Declares `namespace game { struct StatusChange; }` inside `engine::` (line 3) and takes `game::StatusChange*`. Options: (a) parameterize as template on payload type, (b) have the game-side header declare the free functions, (c) keep as-is and document the declare-in-engine / implement-in-game intent at the top of the header. Pick (c) as minimal; add 4-line header comment explaining the pattern. [~15m]

## Verification Notes
Corrected line references vs original plan:
- `ClientReceive.cpp:119` and `:149` were incorrect (those are `Client.cpp` — now consolidated under the Client.cpp bullet).
- `ClientReceive.cpp:377` was incorrect; `kiVersion` is sent in `ClientSend.cpp:377` (`SendHello`) — corrected.
- `ClientReceive.cpp:40`, `:496` confirmed accurate (Frame factory, `SetTimeScale`).
- `ServerReceive.cpp` handler line refs (480, 497, 514, 531, 548, 432, 451-462) all confirmed at commit d08678d3.
- `Server.cpp:81, 111, 288` confirmed (`miTimeMultiply` reads and `BufferFullFrame`).
- `Client.cpp:119, 149` confirmed (`miTimeMultiply` reads).
Save/load packet-reclass is sound: the handler bodies are trivial forwards to `game::gpGame->mGameSaveLoad.ServerSave/Load/Reset()`, and the state machine consequences (`ResetClientsForLoad` in `ServerSession.cpp`) already live at the game layer.
