# Architecture: Engine → Game Layer Violations — Plan B (CoordFrames, Frame factory, timespeed, version)

## Context

This is the remainder of the original `Network/Architecture_LayerViolations.md` plan after **Plan A landed** (Plan A reclassed the six one-way debug packets — save/load/reset/replay/pause — as game-layer `GamePacketType` values now handled in `game::ServerSession::ParseReceivedGamePackets`; the engine no longer interprets them). The original `Architecture_LayerViolations.md` file has been removed from disk and from the `Order.md` table, but it is still referenced in this file's `Order.md` `## File Groups` / `## Dependencies` entries — those references describe the *area* and remain useful context for the touched files.

Plan B addresses the deferred remainders plus sibling violations a codebase sweep surfaced. The shared shape across all of them: engine session/peer code (`engine::` namespace, in `Engine/Source/Network/`) reaches *through* the game-layer global `game::gpGame` (or directly names `game::Frame`) to read or mutate game state. The fix in each case is a seam — a virtual on the engine session base overridden by the game session, or a game-supplied accessor — so the engine code traffics in engine types and lets the game layer own the game-typed state.

**Pre-existing typed-on-game-types boundary (acknowledge, do not attempt to fully remove).** Several engine structs are *already* declared in terms of game types, so even a "return batches to the game layer" refactor still hands game types across the boundary:
- `Engine/Source/Network/Client/Client.h:28` — `ReceivedCoordUpdate::statusChanges` is `std::vector<game::StatusChange>`.
- `Engine/Source/Network/Client/Client.h:37, :68` — `ReceivedCoordFullState::pFrame` / `ReceivedDebugFrame::pFrame` are `std::unique_ptr<game::Frame>`.
- `Engine/Source/Network/Server/ServerTypes.h:35` — `GridUpdateData::statusChanges` is `std::span<const game::StatusChange>`.
- `Engine/Source/Network/Server/Server.h:143` — `Server::SendCoordFullState(..., const game::Frame* pFrame)`.

This plan tightens the **call-site** coupling (who dereferences `game::gpGame` / who mutates `mCoordFrames`) but does **not** de-game-type those engine structs. State that explicitly so the executor does not expand scope into a full struct-templatization exercise.

## Design

Each item below names the current path:symbol and the proposed seam. Verify line numbers at execution time (`/next-plan` refresh) — they were captured this session and are accurate as of writing.

### 1. CoordFrames mutation/reads pushed out of engine `ClientSessionBase`

`Engine/Source/Network/Client/ClientSessionBase.cpp` reaches `game::gpGame->mCoordFrames` in many places. The original plan **under-counted** these — enumerate all of them so the boundary is actually clean, not letter-compliant:

- `ClientSessionBase::DisconnectFromServerBase` (function at ~line 22) — the reset loop at **lines 30-33** iterates `game::gpGame->mCoordFrames` and calls `ResetClientState()`. → push behind a virtual `OnDisconnect()` overridden by `game::ClientSession`, which owns the reset loop.
- `ClientSessionBase::UnsubscribeStaleCoords` — `game::gpGame->mCoordFrames.erase(unsubCoord)` at **line 152** (kSubscribing-cancel branch) and **line 159** (kUnsubscribing branch). → the engine returns / signals the unsubscribed coords; the game session erases.
- `ClientSessionBase::ApplyReceivedUpdatesBase` — `mCoordFrames.at(coord)` at **line 225**, the buffer-full guard read at **line 236**, and the `serverUpdates.try_emplace(...)` write at **line 244**. → return per-slot update batches; let the game session apply them to its own `mCoordFrames`. (Batches still carry `game::StatusChange` — see the typed-boundary note above.)
- `ClientSessionBase::GetConfirmedTick` — read loop over `mCoordFrames` at **line 369**.
- `ClientSessionBase::GetClientConfirmedTick` — `mCoordFrames.find(game::gpGame->mClientGridCoord)` at **lines 381-382**.
- `ClientSessionBase::GetServerUpdateBufferSize` — read loop over `mCoordFrames` at **line 392**.

The three `Get*` query methods are reads, not mutations; decide during the grill whether they move to the game session wholesale or take an injected `mCoordFrames` reference. (Note: the original plan's "subscription full-state path (~371-398)" citation does not match this file — lines 366-400 are the three `Get*` query methods, *not* a full-state apply path. The full-state apply lives in `Engine/Source/Network/Client/ClientReceive.cpp::ServerCoordFullState` and pushes onto engine-owned `mReceivedFullStates` without touching `mCoordFrames`. Reconcile the original citation to the `Get*` methods enumerated here.)

### 2. ServerSessionBase full-state send

`Engine/Source/Network/Server/ServerSessionBase.cpp::SendNewSubscriptionFullStates` (function at **line 49**) reads `game::gpGame->mCoordFrames.find(rSub.coord)` at **lines 64-65**, then `frameIt->second.staticData` (**line 67**) and `frameIt->second.pCurrent.get()` (**line 68**). → add pure-virtual `GetCoordStaticData(GridCoord)` and `GetCoordCurrentFrame(GridCoord)` (or one combined accessor returning a small struct) on `ServerSessionBase`, implemented by `game::ServerSession`. The engine calls `gpServer->SendCoordStaticData(...)` / `SendCoordFullState(...)` with the accessor results.

### 3. Frame factory

`Engine/Source/Network/Client/ClientReceive.cpp:40` — `std::make_unique<game::Frame>()` inside the file-static `DecompressAndReadFrame`. → virtual `CreateFrame()` (returning `std::unique_ptr<...>`) on the session base / client seam, so the engine does not name `game::Frame` to construct one. Note the same file also names `game::Frame` in the `DecompressAndReadFrame` return type and in `ServerCoordFullState` / `ServerDebugFrame` (lines 159, 370) — those are the typed-boundary cases (`std::unique_ptr<game::Frame>` is the stored type per `Client.h`), so only the *construction* site moves behind the factory; the type names stay until the structs are de-game-typed (out of scope).

### 4. Timespeed flow (fully deferred from Plan A — move as one unit)

Move the entire timespeed flow to the game layer in a single change so the wire format never splits:
- Reclass `PacketType::kClientTimespeedRequest` (`Engine/Source/Network/NetworkProtocol.h:25`) to a game-layer `GamePacketType` and move the handler `Server::ClientTimespeedRequest` (`Engine/Source/Network/Server/ServerReceive.cpp:427`, reads `game::gpGame->mTimeStep` and calls `Increase/DecreaseTimeScale` + `BroadcastTimespeedUpdate`) into the game `ServerSession` packet dispatch. The two client send sites are already game-layer (`Projects/BrokenEngineSandbox/Source/Game.cpp:1360, :1371`).
- Client apply: `Client::ServerTimespeedUpdate` (`Engine/Source/Network/Client/ClientReceive.cpp:548`) calls `game::gpGame->mTimeStep.SetTimeScale(...)` at **line 562**. → virtual `OnServerTickRateChanged(int64_t iMultiply, int64_t iDivide)` overridden by the game client session.
- The `kServerTimespeedUpdate` broadcast path: `Server::SendTimespeedUpdate` / `Server::BroadcastTimespeedUpdate` (`Engine/Source/Network/Server/ServerSend.cpp:104, :112`) and the per-tick broadcast trigger in `GameBase::ServerUpdate` reading `mTimeStep` at `Engine/Source/GameBase.cpp:117`.
- The ClientHello timespeed resend: `Engine/Source/Network/Server/ServerReceive.cpp:309-311` reads `game::gpGame->mTimeStep.miTimeMultiply/miTimeDivide` and calls `SendTimespeedUpdate(pPeer, ...)` so a client joining a non-1× server stays in sync.

`kServerTimespeedUpdate` may stay an engine packet IF the game owns both its construction (broadcast) and consumption (apply) through seams; the grill should decide whether to also reclass it to `GamePacketType` for symmetry with the request.

### 5. Frame version (deferred from Plan A — move BOTH sites together)

`game::Frame::kiVersion` is read in two engine places that form one wire contract:
- Client send: `Engine/Source/Network/Client/ClientSend.cpp:192` — `rWorkbuffer.PushBack<int64_t>(game::Frame::kiVersion)` in `Client::SendHello`.
- Server check: `Engine/Source/Network/Server/ServerReceive.cpp:246-257` — reads the client frame version and compares against `game::Frame::kiVersion` at **lines 247 and 250** (the mismatch-rejection format string).

Move BOTH via a game-supplied frame-version seam (a member set on `Client` / `Server` at init, or a session-base accessor returning the version). Moving only one half risks a silent wire-format split where one side stamps/checks a stale version.

### 6. BufferFullFrame accepts pre-serialized bytes

`Engine/Source/Network/Server/Server.cpp::BufferFullFrame` signature is `void Server::BufferFullFrame(int64_t iTick, const std::vector<std::pair<GridCoord, const game::Frame*>>& rFrames)` at **line 269** (NOT line 289 as the original citation stated), and serializes each via `std::ostringstream frameStream; frameStream << *rFrame.second;` at **line 280** (NOT line 300). → change the signature to accept pre-serialized bytes (e.g. `std::span<const std::pair<GridCoord, std::span<const uint8_t>>>` or equivalent); the game `ServerSession` serializes via `rFrame.ServerWrite(stream)` before calling. This removes the `operator<<(game::Frame)` dependency from the engine.

## Critical files

- `Engine/Source/Network/Client/ClientSessionBase.cpp` / `.h` — `DisconnectFromServerBase`, `UnsubscribeStaleCoords`, `ApplyReceivedUpdatesBase`, `GetConfirmedTick`, `GetClientConfirmedTick`, `GetServerUpdateBufferSize`; add `OnDisconnect()` virtual + update-batch return seam.
- `Engine/Source/Network/Server/ServerSessionBase.cpp` / `.h` — `SendNewSubscriptionFullStates`; add `GetCoordStaticData` / `GetCoordCurrentFrame` pure virtuals.
- `Engine/Source/Network/Client/ClientReceive.cpp` — `DecompressAndReadFrame` (`CreateFrame()` seam), `ServerTimespeedUpdate` (`OnServerTickRateChanged` seam).
- `Engine/Source/Network/Client/ClientSend.cpp` — `Client::SendHello` frame-version stamp.
- `Engine/Source/Network/Server/ServerReceive.cpp` — `Server::ClientHello` frame-version check + ClientHello timespeed resend; `Server::ClientTimespeedRequest` handler.
- `Engine/Source/Network/Server/ServerSend.cpp` — `SendTimespeedUpdate` / `BroadcastTimespeedUpdate`.
- `Engine/Source/Network/Server/Server.cpp` / `Server.h` — `BufferFullFrame` signature.
- `Engine/Source/Network/NetworkProtocol.h` — `kClientTimespeedRequest` reclass.
- `Engine/Source/GameBase.cpp` — per-tick timespeed-changed broadcast trigger at line 117 (engine side of the timespeed flow).
- Game layer: `Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.{h,cpp}`, `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.{h,cpp}` — override the new virtuals, own the `mCoordFrames` mutation/reads, serialize frames for `BufferFullFrame`, handle the reclassed timespeed request.

## Out of scope

- **De-game-typing the engine structs.** `Client.h::ReceivedCoordUpdate/ReceivedCoordFullState/ReceivedDebugFrame`, `ServerTypes.h::GridUpdateData`, and `Server.h::SendCoordFullState` stay typed on `game::StatusChange` / `game::Frame`. Returning update batches to the game layer still hands these types across the boundary; fully removing the dependency would require templatizing those structs and is a separate, much larger effort.
- **The `miTimeMultiply` debug-branch reads** at `Engine/Source/Network/Server/Server.cpp:82, :112` and `Engine/Source/Network/Client/Client.cpp:111, :141`. These live inside `if constexpr (keNetworkSimulation != NetworkSimulationLevel::kDisabled)` compile-time-disabled debug branches; cleaning them is gold-plating with zero shipped-binary impact. Leave them.
- **The camera-driver `game::gpCamera->Update()` mutation calls** and the broader `gpCamera` read sweep — tracked by `Graphics/Architecture_EngineCameraCoupling.md`.
- **The already-landed Plan A packets** (save/load/reset/replay/pause). Do not re-touch the `GamePacketType` dispatch they introduced.

## Acceptance criteria

- No `engine::` Network TU dereferences `game::gpGame->mCoordFrames` (read or write) — verified by grep after the change; the only remaining `game::` names in engine Network TUs are the pre-existing typed-struct fields enumerated in Context.
- A client connecting to a server already running at non-1× timescale receives and applies the correct timescale (ClientHello resend path) with no behavior change.
- Frame-version mismatch still rejects the client with the same log/message, and the version stamped by the client equals the value the server checks (single source via the seam).
- Subscribe → full-state → active → unsubscribe lifecycle and reconciliation/CRC determinism are unchanged across a local two-client session (this rides the reconciliation/subscription hot path).
- Debug full-frame requests still round-trip (BufferFullFrame now fed pre-serialized bytes).
