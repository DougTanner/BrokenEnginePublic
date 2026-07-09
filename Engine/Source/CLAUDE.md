# Engine/Source - Core Runtime Engine

## Overview

Manager-based runtime producing client and server executables from shared source. Managers are constructed in dependency order in `MainThread` and destroyed in reverse via RAII. Fixed-rate physics with variable-rate rendering interpolating between committed ticks.

See also: [Frame Update Pipeline](../../Documents/Architecture/FrameUpdatePipeline.md) — update this diagram if main loop or `RunFrameTick` phase ordering changes.

## Hub Conventions (children do not re-document these)

- **Engine→game access**: The engine may assume **any** type or symbol is declared by Game, and **Game is required to implement anything the engine assumes** — that is the contract, not a leak to be plugged. The sanctioned engine→game direction (root CLAUDE.md) therefore includes virtual hooks on `GameBase`, direct `game::gp*` reads, engine calls to `game::` static functions (e.g. the `FrameInterpolate` phase-hook dispatch family — `Register`/`GraphicsResources`/`AllocateAndCopy`/`BeginRender`/…), and engine reads of any game-layer compile-time symbol or static constant (e.g. `keNetworkSimulation` in game `Pch.h`; the game CPU-timer enum the Profile FPS overlay reads by name; `game::Camera` static constants consumed by render-target sizing — all documented at their leaves). None of these are layer violations and must not be "decoupled". Real violations are the reverse direction — engine *types* naming game concepts (e.g., an `engine::PacketType` enumerator only the game uses, an engine class `friend`-ed to a game class) — those are worth fixing. Deliberate engine ownership of game objects documented at the leaf (e.g. `ImGuiManager`'s `game::*Screen` members) is a sanctioned exception, not a violation.
- **Aggregation header**: Every subsystem exposes itself through `Engine.h`, unless a subsystem hub documents a deliberate exception (game `Input.h` includes `Input/RawInputManager.h` directly so the server build sees the RawInput struct — `RawInputManager.h` itself is partial-guard, not whole-file, for that reason; `NetworkCursor.h` is intentionally not aggregated; Ui wrapper headers are excluded so default-value edits don't recompile the world). Include order there is load-bearing and commented inline. Platform-gated includes go inside the existing single `BT_CLIENT`/`BT_SERVER` spans, which are include grouping only — every single-build header/cpp reached through them carries its own whole-file `#if defined(BT_CLIENT)`/`BT_SERVER` wrap (mechanism: root CLAUDE.md → Client/Server Targets). `Engine.h` also hosts the `std::formatter` specializations for engine ID/alignment types (`uuid_t`, `id_t<T>`, `alignment_t`, `Alignments`) so `LogDifference` can print them. There is no `Engine/Source/Pch.h` — the game's `Pch.h` includes `Engine.h` and generates the PCH per project.
- **Allocation discipline**: Tracking is enabled only around the main loop — startup/teardown allocate freely. Suppression rules: root CLAUDE.md; allocator/tracking mechanics: [Memory/CLAUDE.md](Memory/CLAUDE.md).

## Startup & Main Loop

- Single-instance mutex stays gated by `kbSingleInstance` (server-only; false for client) — a set `--agent-port` does not force it on. Where the mutex exists, `--agent-port` only swaps the conflict `MessageBox` for a `kError`+exit so a harness never blocks on a modal dialog; client duplicate protection is the agent bind fail-fast instead.
- `engine::LaunchOptions` (`LaunchOptions.h`, `gLaunchOptions`) parses `--agent-port` / `--log-file` / `--windowed WxH` at `wWinMain` and drives agent-channel activation, the optional log file sink, and an optional forced windowed client size. `ParseLaunchOptions` returns false and `wWinMain` aborts if `--agent-port` is outside `[1, 65535]` (htons would silently truncate it). `--windowed` overrides fullscreen only at the read sites (`Main.cpp`'s `WantedFullscreen()` helper) — it never mutates the persisted `gFullscreen` setting.
- Determinism: FMA3 disabled, SSE4.1 required — both load-bearing for cross-CPU CRC matching.
- Process priority `HIGH_PRIORITY_CLASS`; main thread `TIME_CRITICAL`. Background worker count = cores − 2 (client) / − 1 (server).
- DxDiag read asynchronously at `BELOW_NORMAL` priority, cached for crash reports, skipped under debugger.
- Client requires `RO_INIT_MULTITHREADED` for XAudio2 / gamepad.
- Client boot order is load-bearing: `IslandTerrain` elevation maps complete before the `Graphics` ctor (the record-once terrain command buffer needs the CPU mesh pointers), priority textures are awaited, and every framebuffer is rendered/presented once before `ShowWindow`.
- `TextureUploadManager` and `FileManager` are created in `wWinMain` before the exception-handled `MainThread` so they remain alive during crash handling and teardown.
- Under debugger, exceptions propagate uncaught; otherwise they route to `engine::HandleException`. `DeviceLostException` recreates `Graphics` in place.
- WndProc suppresses `SC_KEYMENU` and (server-only) `SC_MOVE`/`SC_SIZE`/`SC_MAXIMIZE`/`SC_RESTORE` to prevent modal message loops from stalling the main thread.

## CoordFrames Invariants

- Client ring indexing always via `SnapshotIndex(iHead, iLogical)` — never raw `%`.
- Monotonic guards trip `DEBUG_BREAK` on regression: validated-tick high-water (reconcile) and last-rendered tick/time (renderer must never step backward). Tick counter itself is asserted non-negative on assignment — callers performing clock corrections must clamp at zero.
- `ResetClientState()` is the canonical session-reset point; any new per-coord counter must reset there or state leaks across sessions.
- Server dual-buffer: `SwapFrames()` per tick; post-swap `pNext` holds stale data reused by the next `EnsureNextFrames()`.
- ID minting is server-authoritative (frame IDs wrap uint16, global IDs monotonic int64). Clients receive both via serialization — never mint locally.
- Client render-side sim clock (`mfRenderTime`) integrates sim seconds and is clamped to the closed one-tick window starting `kiRenderBehindTicks` behind the ring tail (newer committed ticks are held as starvation cushion), so every rendered frame interpolates between two simulated ticks — never extrapolates past committed state. Seeded once at the window midpoint; rebases only on a multi-tick discontinuity (window regression or burst advance), never per commit.

## Tick Flow

- **Client**: single-pass poll / reconcile / advance. Clamps to `GetSimTickCeiling()` (clock-servo target + `kiSimCeilingSlackTicks`) via `AbsorbUnusedTicks()` — the slack keeps arrival jitter from stalling the sim while StatusChanges still normally arrive before their tick simulates. Physics advances only inside reconcile — no separate client physics loop.
- **Server**: `ServerUpdate` orchestrates network pre-tick, save/load/replay, tick wait, per-tick simulation/broadcast, resends, and autosave; quickload may early-return the whole update. Full-tick count ≠ 1 logs a warning.
- **Finalize**: cross-frame transfer harvest is skipped during replay for deterministic reproduction; per-tick status changes are cleared after broadcast.
- **Dispatch**: `ActiveFrameRef` pre-resolved into workbuffer, fanned out across grid coordinates via `common::gpMultithreading->Dispatch()` when enabled, else sequential; `thread_local` globals enable safe parallel physics.
- Network orchestration lives in `ClientSession`/`ServerSession` at the game layer, called from `ClientUpdate`/`ServerUpdate`.
- Save/load/replay also lives in the game layer (`game::GameSaveLoad`, server-only — see the game [Source/CLAUDE.md](../../Projects/BrokenEngineSandbox/Source/CLAUDE.md)); the engine main loop only invokes it through the game object during the server tick.

## Crash Reporting

`HandleException` calls `DEBUG_BREAK()` first, then bundles exception message, StackWalker callstack, cached DxDiag output, and in-memory log ring into a report written to Desktop or `%APPDATA%` (user chooses via MessageBox). Path/filename construction uses fixed wchar buffers, never `std::string` — the handler is reachable from `SIGABRT` during heap corruption, so it must not re-enter the allocator.

## Subsystems

- `Agent/` - Loopback TCP JSON command channel for automated-harness control (`AgentCommandServer`, `gpAgentCommandServer`, null when disabled). Activation compile-gated by game `kbAgent` (only the construction site is `if constexpr (kbAgent)`; the code always compiles), activated only when `--agent-port` is set. A background `jthread` accepts one in-flight request; the request is executed on the main thread via `Drain()` — top of the client frame loop and after `PreTickNetwork()` in `GameBase::ServerUpdate`. Command dispatch (`ExecuteAgentCommand`) lives at the game layer. A handler may call `DeferResponse(poll)` to complete asynchronously: `Drain()` withholds the reply and polls each later frame until the poll yields a result, publishing it under the original request id — generation-tagged so a mid-capture disconnect abandons the deferral, with a drain-count timeout so a lost result (device-loss mailbox wipe) fails rather than deadlocks. Two client-only companion subsystems back UI automation (both `gp*` singletons, whole-file `BT_CLIENT`, zero steady-state heap): `AgentUiRegistry` double-buffers a fixed-cap snapshot of every ImGui widget/window per completed frame via the imgui test-engine hooks — published by `ImGuiManager` calling `Swap()` after `ImGui::Render()`, read for label resolution and `describe_ui`; `AgentInput` runs one frame-stepped synthetic-input script at a time (advanced at the client drain point), driving both ImGui IO events and a `RawInput` snapshot overlay for game bindings (see [Input/CLAUDE.md](Input/CLAUDE.md)).
- [Audio/CLAUDE.md](Audio/CLAUDE.md) - XAudio2 3D spatial audio (client-only)
- [File/CLAUDE.md](File/CLAUDE.md) - Asset loading, save files, DifferenceStream replay
- [Frame/CLAUDE.md](Frame/CLAUDE.md) - Game state, collections, IslandTerrain, navigation
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md) - Vulkan multi-pass renderer (client-only)
- [Input/CLAUDE.md](Input/CLAUDE.md) - Raw Input keyboard, DirectXTK mouse/gamepad (client-only)
- [Memory/CLAUDE.md](Memory/CLAUDE.md) - Global allocator, allocation tracking
- [Network/CLAUDE.md](Network/CLAUDE.md) - ENet UDP networking, discovery
- [Profile/CLAUDE.md](Profile/CLAUDE.md) - CPU/GPU performance profiling
- [Ui/CLAUDE.md](Ui/CLAUDE.md) - Runtime-adjustable settings
