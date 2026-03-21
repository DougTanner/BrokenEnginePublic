# Engine/Source - Core Runtime Engine

## Overview

Manager-based game engine runtime producing two executables (client and server) from the same source via `BT_CLIENT`/`BT_SERVER` defines. All major systems are singletons accessed via global pointers (e.g., `gpGraphics`, `gpAudioManager`). Managers are created in strict dependency order at startup and destroyed in reverse order via RAII.

See also: [Frame Update Pipeline](../../Documents/Architecture/FrameUpdatePipeline.md) — update this diagram if main loop or RunFrameTick phase ordering changes

## Key Classes

- **Main.cpp** - Entry point: initialization, main loop, shutdown. Client loop runs input/physics/render/audio then kicks `PostRender()` reconciliation; server loop runs physics with network broadcasts and GDI stats display
- **GameBase** - Abstract base orchestrating fixed-rate physics over a sparse grid of dual-buffered frames. Client adds snapshot ring buffers for extrapolation/reconciliation. Owns a `GameFlags` bitmask for game-wide state transitions (e.g., `kPaused` freezes the timestep on both client and server). Game code accesses this via the derived `game::gpGame` pointer
- **GameSaveLoad** - Save/load/replay responsibility extracted from GameBase (server-only, `BT_SERVER`). Delta-compressed deterministic recording/playback with CRC validation, plus multi-frame grid quicksave/quickload. Exposes `ServerSave()`/`ServerLoad()` for network-triggered save/load; quickload calls `ResetClientsForLoad` after a successful load to re-link clients by GUID and clear buffered frames. Replay uses per-coord maps (`mReplayWriters`/`mReplayReaders`) keyed by `GridCoord`: `SaveLoadReplay()` handles start/stop/load transitions driven by `GameFlags::kSaveReplay`/`kLoadReplay`; `SyncReplayTick()` advances all writers or readers each server tick. Recording saves one file per coord plus a manifest and grid state; playback loads the manifest, restores grid state, creates per-coord readers, and calls `ResetClientsForLoad`. `IsRecording()`/`IsReplaying()` expose writer/reader presence; `GetReplayReaders()` exposes the reader map to `PrepareActiveSet`
- **CrashReport** - Crash report generation with callstack (StackWalker) and DxDiag output
- **Engine.h** - Single aggregation header with `#ifdef` guards for client/server-conditional includes

## Architecture Notes

- Fixed-rate physics (e.g., 64Hz) with variable-rate rendering and interpolated frames
- Network orchestration lives in `ClientSession`/`ServerSession` (game layer), called from `UpdateClient()`/`UpdateServer()`
- Frame ticks are dispatched in parallel across grid coordinates via `common::gpMultithreading->Dispatch()`
- `thread_local` globals enable safe parallel physics execution

## Subsystems

- [Audio/CLAUDE.md](Audio/CLAUDE.md) - XAudio2 3D spatial audio (client-only)
- [Debug/CLAUDE.md](Debug/CLAUDE.md) - Vulkan debug utilities (client-only)
- [File/CLAUDE.md](File/CLAUDE.md) - Asset loading, save files, DifferenceStream replay
- [Frame/CLAUDE.md](Frame/CLAUDE.md) - Game state, collections, IslandTerrain
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md) - Vulkan multi-pass renderer (client-only)
- [Input/CLAUDE.md](Input/CLAUDE.md) - Raw Input keyboard, DirectXTK mouse/gamepad (client-only)
- [Memory/CLAUDE.md](Memory/CLAUDE.md) - Global allocator, allocation tracking
- [Network/CLAUDE.md](Network/CLAUDE.md) - ENet UDP networking, discovery
- [Profile/CLAUDE.md](Profile/CLAUDE.md) - CPU/GPU performance profiling
- [Server/CLAUDE.md](Server/CLAUDE.md) - GDI monitoring window (server-only)
- [Ui/CLAUDE.md](Ui/CLAUDE.md) - Runtime-adjustable settings
