# Engine/Source - Core Runtime Engine

## Overview

Manager-based runtime producing client and server executables from shared source via `BT_CLIENT`/`BT_SERVER` defines. Major systems are singletons accessed through `gp*` globals; managers are constructed in dependency order and destroyed in reverse via RAII.

See also: [Frame Update Pipeline](../../Documents/Architecture/FrameUpdatePipeline.md) — update this diagram if main loop or `RunFrameTick` phase ordering changes.

## Hub Conventions (children do not re-document these)

- **Client/server split**: Files fully wrapped in `#if defined(BT_CLIENT)` / `BT_SERVER` live only in the matching vcxproj. Narrow `#ifdef` at the smallest practical scope; prefer `SharedMembers()` + `ClientMembers()` over duplicated collections.
- **Base classes**: Engine code uses game-derived types via `game::gpGame` — never reference `*Base` directly outside the base file itself.
- **Aggregation header**: Every subsystem exposes itself through `Engine.h` (included by `Pch.h`); include order there is load-bearing and commented inline. Platform-gated includes go inside the existing single `BT_CLIENT`/`BT_SERVER` spans.
- **Allocation discipline**: Main-loop heap allocations trigger `DEBUG_BREAK`; unavoidable ones wrap with `ScopedSuppressAllocationTracking` + `// Heap:` comment. Startup/teardown allocate freely.
- **Workbuffer**: Use `gpThreadLocal->mWorkbuffer` for temporaries across all subsystems.

## Startup & Main Loop

- Determinism: FMA3 disabled, SSE4.1 required — both load-bearing for cross-CPU CRC matching.
- Process priority `HIGH_PRIORITY_CLASS`; main thread `TIME_CRITICAL`. Background worker count = cores − 2 (client) / − 1 (server).
- DxDiag read asynchronously at `BELOW_NORMAL` priority, cached for crash reports, skipped under debugger.
- Client requires `RO_INIT_MULTITHREADED` for XAudio2 / gamepad.
- Under debugger, exceptions propagate uncaught; release routes to `engine::HandleException`. `DeviceLostException` recreates `Graphics` in place.
- WndProc suppresses `SC_KEYMENU` and (server-only) `SC_MOVE`/`SC_SIZE`/`SC_MAXIMIZE`/`SC_RESTORE` to prevent modal message loops from stalling the main thread.

## CoordFrames Invariants

- Client ring indexing always via `SnapshotIndex(iHead, iLogical)` — never raw `%`.
- Monotonic guards trip `DEBUG_BREAK` on regression: validated-tick high-water (reconcile) and last-rendered tick/time (renderer must never step backward).
- `ResetClientState()` is the canonical session-reset point; any new per-coord counter MUST reset there or state leaks across sessions.
- Server dual-buffer: `SwapFrames()` per tick; post-swap `pNext` holds stale data reused by the next `EnsureNextFrames()`.
- ID minting is server-authoritative (frame IDs wrap uint16, global IDs monotonic int64). Clients receive both via serialization — never mint locally.

## Tick Flow

- **Client**: single-pass poll / reconcile / advance. Clamps to `GetTargetSimTick()` via `AbsorbUnusedTicks()` so StatusChanges arrive before their tick simulates. Physics advances only inside reconcile — no separate client physics loop.
- **Server**: pre-tick net → quickload (may early-return) → save/load replay → wait for tick → per tick prepare/sync/dispatch/finalize → resends → quicksave. Full-tick count ≠ 1 logs a warning.
- **Finalize**: cross-frame transfer harvest (skipped during replay for deterministic reproduction) → swap → broadcast → clear status changes.
- **Dispatch**: `ActiveFrameRef` pre-resolved into workbuffer, fanned out via `common::gpMultithreading->Dispatch()` when enabled, else sequential.

## Save / Load / Replay (server-only)

- `GameSaveLoad.cpp` fully wrapped in `#if defined(BT_SERVER)`; header safe from shared code.
- All public APIs wrap bodies in `ScopedSuppressAllocationTracking` — fstreams and SOA (de)serialization must allocate.
- Quicksave/quickload gated on `kbDebugInput`; autosave unconditional.
- Grid writes sort by `ToKey()` for deterministic output; reads validate `game::Frame::kiVersion`. End-of-replay auto-loops by re-raising the load flag.
- Fleet persistence is delegated to `ServerSession` — do not serialize fleet state here.

## Crash Reporting

`HandleException` calls `DEBUG_BREAK()` first, then bundles exception message, StackWalker callstack, cached DxDiag output, and in-memory log ring into a report written to Desktop or `%APPDATA%`.

## Architecture Notes

- Fixed-rate physics (e.g., 64Hz) with variable-rate rendering and interpolated frames.
- Network orchestration lives in `ClientSession`/`ServerSession` at the game layer, called from `UpdateClient`/`UpdateServer`.
- Frame ticks dispatch in parallel across grid coordinates; `thread_local` globals enable safe parallel physics.

## Subsystems

- [Audio/CLAUDE.md](Audio/CLAUDE.md) - XAudio2 3D spatial audio (client-only)
- [Debug/CLAUDE.md](Debug/CLAUDE.md) - Vulkan debug utilities (client-only)
- [File/CLAUDE.md](File/CLAUDE.md) - Asset loading, save files, DifferenceStream replay
- [Frame/CLAUDE.md](Frame/CLAUDE.md) - Game state, collections, IslandTerrain, navigation
- [Graphics/CLAUDE.md](Graphics/CLAUDE.md) - Vulkan multi-pass renderer (client-only)
- [Input/CLAUDE.md](Input/CLAUDE.md) - Raw Input keyboard, DirectXTK mouse/gamepad (client-only)
- [Memory/CLAUDE.md](Memory/CLAUDE.md) - Global allocator, allocation tracking
- [Network/CLAUDE.md](Network/CLAUDE.md) - ENet UDP networking, discovery
- [Profile/CLAUDE.md](Profile/CLAUDE.md) - CPU/GPU performance profiling
- [Server/CLAUDE.md](Server/CLAUDE.md) - GDI monitoring window (server-only)
- [Ui/CLAUDE.md](Ui/CLAUDE.md) - Runtime-adjustable settings
