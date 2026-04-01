# BrokenEngineSandbox - Sample Game Implementation

## Overview

A space combat game demonstrating the Broken Engine's client/server architecture. Features dogfighting, wave-based enemy spawning, terrain interaction, and rollback-based netcode. All game code lives in the `game` namespace; the vcxproj defines `BT_CLIENT` or `BT_SERVER` to produce separate executables from the same source.

## IMPORTANT: Frame Purity Constraint

Frame code is purely functional. Frame updates must only rely on explicit function parameters -- Frame code must NEVER query Game (`gpGame`) for anything. The Frame does not know which player is human vs AI. Human identity, camera shake, death transitions, and respawn orchestration are Game-level responsibilities.

## Key Classes/Systems

- **Game** (`Game.h`/`Game.cpp`) - Central coordinator inheriting from `engine::GameBase`, accessed via `gpGame`. Manages lifecycle (menu/gameplay/death), multi-player-per-client tracking (a list of owned `global_player_t` IDs with a focused index), `ClientPlayerIndex()` (resolves the focused player's SOA index from `PlayersPostRender`), `FocusNext()`/`FocusPrev()` navigation, multi-frame grid orchestration, cross-grid entity transfers, sound settings and tweaks screen state persistence, and music playlists. Owns `ClientSession` (client) or `ServerSession` (server) for networking. On the server, the constructor attempts `Autoload()` to restore state from the previous run before falling back to creating a fresh frame; on shutdown, `Autosave()` is called from `Main.cpp` after the main loop exits. Holds a visual error offset used by the camera to smooth abrupt reconciliation corrections; decayed each render frame by `GameBase`, cleared on reset
- **ClientSession** - Client networking orchestration: connection, rollback-and-replay reconciliation, coord subscriptions, extrapolation snapshots, and clock correction. See [Network/CLAUDE.md](Network/CLAUDE.md)
- **ServerSession** - Server networking orchestration: active set management, tick broadcasting, spawn/death detection, and transfer harvesting. See [Network/CLAUDE.md](Network/CLAUDE.md)

## Architecture Notes

- **Deterministic floating-point**: `/fp:strict` in vcxproj ensures cross-hardware determinism for CRC-based reconciliation. See [Documents/FloatingPointDeterminism.txt](../../../Documents/FloatingPointDeterminism.txt) for full details on all mitigations
- **Client-driven subscriptions**: Client subscribes to 4 grid coords (human cell + 3 quadrant neighbors) rather than the server computing active sets
- **Compile-time toggles**: `Pch.h` contains `inline constexpr` flags for reconcile threading, frame dispatch parallelism, network simulation levels, auto-launching and auto-connecting to a local server, the default log level threshold (`keLogLevelDefault`) and focused log category (`keFocusedLogCategoryDefault`), and other debug/profile features, used with `if constexpr` for zero overhead
- For detailed reconciliation and networking architecture, see [Game Reconciliation](../../../Documents/Architecture/GameReconciliation.md) and [Network Architecture](../../../Documents/Architecture/Network.md)

## Subdirectories

- [Frame/](Frame/CLAUDE.md) - Game state, physics tick pipeline, and SOA collections (players, blasters, missiles, spaceships, targets)
- [Graphics/](Graphics/CLAUDE.md) - Camera controller with menu animations and player tracking
- [Input/](Input/CLAUDE.md) - Three-tier input processing with keyboard/mouse and gamepad support
- [Network/](Network/CLAUDE.md) - ClientSession and ServerSession networking classes
- [Profile/](Profile/CLAUDE.md) - Game-specific CPU profiling counters
- [Ui/](Ui/CLAUDE.md) - ImGui-based HUD, menus, and settings screens

## See Also

- [Engine Source](../../../Engine/Source/CLAUDE.md)
- [Common Utilities](../../../Common/CLAUDE.md)
