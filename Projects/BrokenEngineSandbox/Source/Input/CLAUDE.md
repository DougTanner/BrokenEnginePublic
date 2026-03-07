# /Projects/BrokenEngineSandbox/Source/Input/

Game-specific input processing that converts raw hardware input into game and menu commands. Client-only (`#ifdef BT_CLIENT`); wraps the engine's RawInputManager. Server builds run AI-driven player behavior without external input. Supports simultaneous keyboard/mouse and gamepad control with automatic mode detection.

**Global**: `gpInput`

## Architecture Overview

**Two-Tier System**: Raw hardware state from engine's RawInputManager -> Input class for toggle detection -> Game-specific MenuInput structure. FrameInput is a minimal struct containing only status changes (spawn, respawn, transfer, destroy events); player input for gameplay is handled separately via the network protocol.

**Automatic Mode Switching**: System monitors all input devices each frame and automatically switches between keyboard/mouse and gamepad modes. Mode affects cursor visibility and aim mechanics.

## Core Components

### Input Class

Manages state tracking for button press/release detection with a previous-state buffer for menu input. Menu input runs every frame for responsive UI (even when paused).

### MenuInput

UI navigation and system commands represented as a flags struct. Debug commands (quicksave/load, replay, time scaling, debug menus) are compiled in via `if constexpr` when `kbEnableDebugInput` is true. When menus are visible, populates ImGui's gamepad input state for D-pad navigation, A/B buttons, and left thumbstick analog navigation.

### FrameInput

Minimal per-coordinate input structure containing only status changes, serializable for deterministic replay. PlayerInput struct and flag enums remain in Input.h for the network protocol (used by server input mapping and reconciliation). Provides two CRC methods: `Crc()` for the full input state (used by replay), and `ServerInputCrc()` for the server-comparable subset (excluding client-only fields like `smokeTrailId`) used for input desync validation during reconciliation.

**Status Changes**: One-shot game state events (spawn, respawn, cross-cell transfer, player destruction) carried as a vector within FrameInput. Each `StatusChange` contains a `StatusChangeType` and a `TransferData` struct with full entity state needed for multi-frame migration (including smoke trail ID preservation for missiles). The `IsTransferType()` inline helper identifies transfer types via a contiguous range check on `StatusChangeType`. StatusChange CRC uses the full struct rather than just the type. Transfer status changes flow through the human's grid coordinate for replay determinism. During replay, `ApplyTransferStatusChanges()` processes transfer entries before checksum validation, then removes them to prevent double-processing in the Spawn phase.

### PlayerInput

Struct retained for the network protocol -- carries per-player held flags (movement, aim, fire), move vector, and direction. Broadcast from server to client in delta update and resend packets for client reconciliation extrapolation, but no longer part of FrameInput itself.

## Input Flow

```
RawInputManager (Engine) - Polls hardware state
    |
Input::UpdateMenuInput() - Every frame
    |-> Produces MenuInput, updates gamepad mode
    |
Game::BuildFrameInputs() - Per grid coordinate during physics
    |-> Applies server-confirmed player inputs via extrapolation
    |-> Injects status changes into per-coord FrameInput
    |
Frame update loop processes status changes per coordinate
```

## See Also
- Engine raw input: [../../../../Engine/Source/Input/CLAUDE.md](../../../../Engine/Source/Input/CLAUDE.md)
- Game class: [../Game.h](../Game.h) - PreUpdate() processes MenuInput
- Frame state: [../Frame/Frame.h](../Frame/Frame.h) - Consumes FrameInput during updates
