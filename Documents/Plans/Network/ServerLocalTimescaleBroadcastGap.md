# Server Debug-Key Timescale Change Never Broadcasts

## Context

Surfaced by the AgentHarness2 session that extracted `ServerSession::StepTimescale(bool)` (`Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp`) as the single step-and-broadcast path shared by the `kClientTimespeedRequest` packet handler (`ServerSession.cpp:260`) and the `timescale` agent command (`AgentCommandsServer.cpp` `CommandTimescale`). `StepTimescale` calls `IncreaseTimeScale()`/`DecreaseTimeScale()` then `BroadcastTimespeedIfChanged()` — and `BroadcastTimespeedIfChanged()` is the **only** caller of the edge-triggered broadcast (it consumes `mTimeStep.mbTimeScaleChanged` and clears it).

The server-operator debug-key path was not routed through the new method. `Game::ProcessDebugInput`'s `BT_SERVER` branch (`Game.cpp`, the `#else` arms of the `kSlowTime`/`kSpeedUpTime` handlers, ~:806 and ~:817) still calls `mTimeStep.DecreaseTimeScale()` / `mTimeStep.IncreaseTimeScale()` directly. Immediately after, the shared (client+server) block at `Game.cpp:821-823` does `if (mTimeStep.mbTimeScaleChanged) { mTimeStep.mbTimeScaleChanged = false; ... }` — swallowing the edge before `BroadcastTimespeedIfChanged` (called only from `StepTimescale`, which this path skips) ever runs. Net effect: a server-operator debug-key timescale change alters the server sim rate but is **never broadcast** to connected clients, which continue at the old rate until some other trigger. Pre-existing gap; the extraction made the correct path obvious but left this caller behind.

## Design

Route the `BT_SERVER` arms of the `kSlowTime` / `kSpeedUpTime` handlers in `Game::ProcessDebugInput` through `gpServerSession->StepTimescale(false)` / `StepTimescale(true)` instead of the direct `mTimeStep.Decrease/IncreaseTimeScale()` calls. `StepTimescale` steps and broadcasts atomically, consuming `mbTimeScaleChanged` inside the broadcast; by the time the `Game.cpp:821` block runs the flag is already false, so its server-side effect (the log + the `BT_CLIENT`-guarded text update, all no-ops on server once the flag is clear) is harmless.

- Mirror the client arms' shape: those already call `gpClient->SendSimplePacket(kClientTimespeedRequest, ...)` guarded on `gpClient != nullptr`; the server arms should call `gpServerSession->StepTimescale(...)` (server session is always present under `BT_SERVER`, but confirm at the edit — match the existing null-guard convention if the session can be null before connect).
- Minimum-viable alternative if routing is undesirable: stop the `Game.cpp:821` block from clearing `mbTimeScaleChanged` on the server so the next `StepTimescale`-external broadcast site sees it — but there is no other broadcast site, so routing through `StepTimescale` is the only complete fix. Prefer routing.

Interfaces:
- `game::ServerSession::StepTimescale(bool bFaster)` — the shared step-and-broadcast entry (`ServerSession.h:~41`, `.cpp:419`).
- `game::ServerSession::BroadcastTimespeedIfChanged()` — edge-trigger consumer of `mTimeStep.mbTimeScaleChanged` (`.cpp:396`); its only caller is `StepTimescale`.
- `game::Game::ProcessDebugInput` — the `kSlowTime`/`kSpeedUpTime` `#else` (BT_SERVER) arms (`Game.cpp` ~:798-819) and the shared post-block clearing the flag (~:821-823).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Game.cpp` — `ProcessDebugInput`, the two server timescale arms.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.{h,cpp}` — `StepTimescale` / `BroadcastTimespeedIfChanged` (read-only reference; no change expected).

## Out of scope

- No change to `StepTimescale`, `BroadcastTimespeedIfChanged`, or the wire message (`kServerTimespeedUpdate`) — reuses the existing broadcast unchanged.
- The `BT_CLIENT` arms (already correct via `kClientTimespeedRequest`) and the shared timescale-text UI block stay as-is beyond the flag no longer being set by the time they run.
- No `NetworkSimulation` / newly-handshaken-client catch-up change (that path stays `Server::ClientHello`'s direct send).
- Not touching `mTimeStep` semantics, `IncreaseTimeScale`/`DecreaseTimeScale`, or the pause interaction.

## Notes

- **Invariant exposure**: server-only (`BT_SERVER` arms). No wire-format change — reuses the existing `kServerTimespeedUpdate` broadcast. Timescale affects server tick pacing but `StepTimescale` is already the sanctioned mutation path (packet + agent both use it), so this introduces no new determinism/CRC surface; it makes the debug-key path match them.
- Verify at edit time whether `gpServerSession` can be null when `ProcessDebugInput` runs (mirror the client arms' `!= nullptr` guard if so).
- No open design decision — single mechanical reroute; not a grill candidate.
