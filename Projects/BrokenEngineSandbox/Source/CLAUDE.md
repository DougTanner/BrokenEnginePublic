# BrokenEngineSandbox - Sample Game Implementation

## Overview

Space combat game demonstrating the Broken Engine's client/server architecture. All game code lives in the `game` namespace; the vcxproj defines `BT_CLIENT` or `BT_SERVER` to produce separate executables from the same source.

## IMPORTANT: Frame Purity Constraint

Frame code is purely functional. Frame updates must only rely on explicit function parameters — Frame code must NEVER query Game (`gpGame`) for anything. The Frame does not know which player is human vs AI. Human identity, camera shake, death transitions, and respawn orchestration are Game-level responsibilities.

## Key Classes/Systems

- **Game** (`Game.h`/`Game.cpp`) - Central coordinator inheriting from `engine::GameBase`, accessed via `gpGame`. Owns lifecycle, fleet selection, multi-frame grid orchestration, cross-grid entity transfers, persisted settings, and the `ClientSession`/`ServerSession`.
- **Fleet** (`Fleet.h`) - Client-side grouping of player entities for focus/selection and shared navigation intent. Drives which grid cell the camera follows.
- **ClientSession** / **ServerSession** - Per-side networking orchestration owned by `Game`. See [Network/CLAUDE.md](Network/CLAUDE.md).

## Architecture Notes

- **Deterministic floating-point**: `/fp:strict` in vcxproj ensures cross-hardware determinism for CRC-based reconciliation. See [Documents/FloatingPointDeterminism.txt](../../../Documents/FloatingPointDeterminism.txt).
- **Client-driven subscriptions**: Client computes its own active set (current cell + up to three quadrant neighbors) using per-axis hysteresis; server only sees the resulting subscription list. Local-only frames outside the active set are evicted; confirmed frames are kept.
- **Cross-grid transfers**: Entity hand-off between grid cells is expressed as transfer `StatusChange`s carrying fully-serialized spawn state, dispatched per collection type and stripped from `FrameInput` before the normal Spawn phase.
- **Alignments**: `playerAlignment` / `enemyAlignment` are owned by `Game` and copied onto each new `Frame::postRender`. Frame code reads them from `postRender`, never from `gpGame`.
- **Versioned persisted settings**: Sound/graphics/tweaks settings are client-only POD structs with an embedded `kiVersion`, round-tripped through `engine::{Write,Read}VersionedFile` into `kAppDataDirectory`. Bump the struct version on layout change.
- **Save-format version**: `Version.h` holds a single `kGameVersion` constant included inside the `game` namespace; bump on save/replay/protocol-incompatible changes.
- **Compile-time toggles**: `Pch.h` holds `inline constexpr` flags for frame dispatch parallelism, quadrant subscriptions, desync recovery, render thread, network simulation, and per-configuration debug/profile knobs. It also defines per-category log-level overrides before including `Common.h`.
- Detailed networking flow: [Game Reconciliation](../../../Documents/Architecture/GameReconciliation.md), [Network Architecture](../../../Documents/Architecture/Network.md).

## Subdirectories

- [Frame/](Frame/CLAUDE.md) - Game state, physics tick pipeline, SOA collections
- [Graphics/](Graphics/CLAUDE.md) - Camera controller
- [Input/](Input/CLAUDE.md) - Keyboard/mouse and gamepad input pipeline
- [Network/](Network/CLAUDE.md) - Sessions, packet types, serialization
- [Profile/](Profile/CLAUDE.md) - Game CPU profiling counters
- [Ui/](Ui/CLAUDE.md) - ImGui HUD, menus, settings

## See Also

- [Engine Source](../../../Engine/Source/CLAUDE.md)
- [Common Utilities](../../../Common/CLAUDE.md)
