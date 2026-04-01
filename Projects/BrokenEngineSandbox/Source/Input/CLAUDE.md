# Input - Game-Specific Input Processing

## Overview

Converts raw hardware input into game and menu commands. Client-only (`#ifdef BT_CLIENT`); wraps the engine's `RawInputManager`. Server builds run AI-driven player behavior without external input. Supports simultaneous keyboard/mouse and gamepad control with automatic mode detection.

**Global**: `gpInput`

## Key Classes/Systems

- **Input** - Polls `RawInputManager` and produces `MenuInput` flags with toggle detection via previous-state tracking. Automatically switches between keyboard/mouse and gamepad modes each frame based on which device is active
- **MenuInput** - Flags struct for UI navigation and system commands (pause, fullscreen, quit). Debug commands (quicksave/load, replay, time scaling, debug texture toggle/cycle) compile in via `if constexpr` when `kbDebugInput` is true. Populates ImGui gamepad state when menus are visible
- **FrameInput** - Per-coordinate input carrying only status changes (spawn, respawn, transfer, destroy). Serializable for deterministic replay. Two CRC methods: `Crc()` for full state (replay) and `ServerInputCrc()` for shared-field subset (reconciliation desync validation)
- **StatusChange / TransferData** - Defined in `Frame/StatusChange.h`. One-shot game state events with full entity state for cross-cell migration

## Architecture Notes

- **Two-tier input**: Menu input runs every frame for responsive UI; frame input (status changes) is consumed during the physics tick pipeline
- **Input flow**: `Main.cpp` calls `GameBase::ProcessInput()` which calls `Input::UpdateMenuInput()` then `GameBase::ProcessMenuInput()`. Frame input is built separately during `Game::BuildFrameInputs()`
- Transfer status changes flow through the human's grid coordinate for replay determinism

## See Also

- Engine raw input: [Engine/Source/Input/CLAUDE.md](../../../../Engine/Source/Input/CLAUDE.md)
- Game reconciliation architecture: [Documents/Architecture/GameReconciliation.md](../../../../Documents/Architecture/GameReconciliation.md)
