# BrokenEngineSandbox - Sample Game Implementation

## Overview

Space combat game demonstrating the Broken Engine's client/server architecture. All game code lives in the `game` namespace.

## Frame Purity Constraint

Frame update code is purely functional: it relies only on explicit function parameters and never queries Game (`gpGame`). Client-only render code under `Frame/` is exempt. The Frame does not privilege any player index — all players are AI-driven; one is the **flagship** that others follow. Flagship-tracking responsibilities (camera shake, death transitions, respawn orchestration) are Game-level, not Frame.

## Key Classes/Systems

- **Game** (`Game.h`/`Game.cpp`) - Central coordinator inheriting from `engine::GameBase`, accessed via `gpGame`. Owns lifecycle, multi-frame grid orchestration, cross-grid entity transfers, the `ClientSession`/`ServerSession`, and (server-only) the `GameSaveLoad` save/load/replay subsystem; holds a `FleetSelection` member and forwards its fleet-navigation API.
- **FleetSelection** (`FleetSelection.h`/`.cpp`, client-only) - Owns the client fleet list and focus state; drives which grid cell the camera follows.
- **Fleet** (`Fleet.h`) - Client-side grouping of player entities for focus/selection and shared navigation intent. Each fleet carries a server-minted random 128-bit identifier that is stable across disconnect/reconnect/save-load and used by clients to refer to fleets persistently.
- **Settings/transfer free functions** - Client-only persisted-settings save/load/reset lives in `ClientSettings.{h,cpp}` (`game::` free functions); the shared spawn-side of cross-grid transfers is the `game::SpawnTransfer` free function in `SpawnTransfer.{h,cpp}`.
- **ClientSession** / **ServerSession** - Per-side networking orchestration owned by `Game`. See [Network/CLAUDE.md](Network/CLAUDE.md).

## Architecture Notes

- **Deterministic floating-point**: `/fp:strict` in vcxproj ensures cross-hardware determinism for CRC-based reconciliation. See [Documents/FloatingPointDeterminism.txt](../../../Documents/FloatingPointDeterminism.txt).
- **Client-driven subscriptions**: Client computes its own active set (current cell + any cells in the immediate 3x3 ring whose footprint overlaps the camera's zoom-dependent `f4LargeVisibleArea`); server only sees the resulting subscription list. Local-only frames outside the active set are evicted; confirmed frames are kept. The 3x3 clamp is explicit — at extreme zoom-out the VisibleArea can extend beyond the ring and would otherwise add cells the server doesn't authorize. Write the client grid coord only through `Game::SetClientGridCoord()` — the visible-neighbor cache stores absolute coords and the setter invalidates it.
- **Cross-grid transfers**: Entity hand-off between grid cells is expressed as transfer `StatusChange`s carrying fully-serialized spawn state, dispatched per collection type and stripped from `FrameInput` before the normal Spawn phase.
- **Alignments**: `playerAlignment` / `enemyAlignment` are owned by `Game` and copied onto each new `Frame::postRender`. Frame code reads them from `postRender`, never from `gpGame`.
- **Versioned persisted settings**: Sound/graphics/tweaks settings plus per-client UI/session state (focused fleet identifier, focused ship global ID, camera zoom target) are client-only POD structs with an embedded `kiVersion`, round-tripped through `engine::{Write,Read}VersionedFile` into `kAppDataDirectory`. Bump the struct version on layout change. Session state is captured each frame and re-persisted only when the diff against the last-written copy changes.
- **Save/load/replay** (`Save/GameSaveLoad`, server-only, driven by the engine server loop): grid saves are written atomically with coords sorted by key for deterministic output. Replay records per-coord `engine::DifferenceStream`s of FrameInput+Frame diffs and validates a checksum each tick against the resimulated frame — replay doubles as a determinism test harness. Nav data and elevation grids are never persisted (saves exclude them); elevation grids are derived data rebuilt lazily per cell on first tick on both sides, while clients receive the server-built nav data over the wire. Fresh-game paths must reset the server fleet manager explicitly — only the load path repopulates it.
- **Versioning**: the save/replay format is gated by `game::Frame::kiVersion` (composition documented in [Frame/CLAUDE.md](Frame/CLAUDE.md)) — collection version bumps propagate automatically. `Version.h`'s `kiGameVersion` is informational only (startup log, crash report, Vulkan application version); it gates nothing.
- **Compile-time toggles**: `Pch.h` holds `inline constexpr` flags for frame dispatch parallelism, desync recovery, render thread, network simulation, and per-configuration debug/profile knobs. It also defines per-category log-level overrides before including `Common.h`.
- Detailed networking flow: [Game Reconciliation](../../../Documents/Architecture/GameReconciliation.md), [Network Architecture](../../../Documents/Architecture/Network.md).

## Subdirectories

- [Frame/](Frame/CLAUDE.md) - Game state, physics tick pipeline, SOA collections
- [Graphics/](Graphics/CLAUDE.md) - Camera controller
- [Input/](Input/CLAUDE.md) - Keyboard/mouse and gamepad input pipeline
- [Network/](Network/CLAUDE.md) - Sessions, packet types, serialization
- [Profile/](Profile/CLAUDE.md) - Game CPU profiling counters
- [Save/](Save/) - Save/load/replay (`game::GameSaveLoad`, server-only)
- [Server/](Server/CLAUDE.md) - GDI monitoring window (server-only)
- [Ui/](Ui/CLAUDE.md) - ImGui HUD, menus, settings

## See Also

- [Engine Source](../../../Engine/Source/CLAUDE.md)
- [Common Utilities](../../../Common/CLAUDE.md)
