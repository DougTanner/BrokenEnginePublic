# /Projects/BrokenEngineSandbox/Source/Input/

Game-specific input processing that converts raw hardware input into game and menu commands. Supports simultaneous keyboard/mouse and gamepad control with automatic mode detection.

**Global**: `gpInput`

## Architecture Overview

**Three-Tier System**: Raw hardware state from engine's RawInputManager -> Input class for toggle detection -> Game-specific MenuInput and FrameInput structures.

**Automatic Mode Switching**: System monitors all input devices each frame and automatically switches between keyboard/mouse and gamepad modes. Mode affects cursor visibility, aim mechanics (screen-to-world raycast vs thumbstick direction), and auto-fire behavior (gamepad auto-fires when right stick moved).

## Core Components

### Input Class

Manages state tracking for button press/release detection with separate previous-state buffers for menu and frame input to avoid interference between the two update paths. Menu input runs every frame for responsive UI (even when paused); frame input runs only during physics steps for deterministic replay.

### MenuInput

UI navigation and system commands represented as a flags struct. Debug commands (quicksave/load, replay, time scaling, debug menus) are compiled in via `if constexpr` when `kbEnableDebugInput` is true. When menus are visible, populates ImGui's gamepad input state for D-pad navigation, A/B buttons, and left thumbstick analog navigation.

### FrameInput

Combined per-player and global input for gameplay, serializable for deterministic replay. Per-player held state (movement, aim, fire) is stored in a dynamically-sized vector resized each frame to match player count. One-shot pressed events and scroll wheel are scoped to the human player and cleared after processing.

**Status Changes**: One-shot game state events (spawn, respawn, cross-cell transfer) carried as a vector within FrameInput. Each `StatusChange` embeds a `TransferData` struct with full entity state needed for multi-frame migration (including smoke trail ID preservation for missiles). Transfer status changes flow through the human's grid coordinate for replay determinism. During replay, `ApplyTransferStatusChanges()` processes transfer entries before checksum validation, then removes them to prevent double-processing in the Spawn phase.

### RawInputToFrameInput()

Free function that maps continuous hardware state into the human player's input slot. Mouse aim uses screen-to-world projection relative to camera position. Gamepad aim persists direction when thumbstick is released to prevent jitter. Keyboard movement accumulates from WASD, arrow keys, and numpad, then clamps. Blocked when ImGui wants input capture (debug builds only).

## Input Flow

```
RawInputManager (Engine) - Polls hardware state
    |
Input::UpdateMenuInput() - Every frame
    |-> Produces MenuInput, updates gamepad mode
    |
Game::BuildFrameInputs() - Per grid coordinate during physics
    |-> RawInputToFrameInput() for human, PlayerAi for AI wingmen
    |-> Input::UpdateFrameInputPressed() for one-shot events
    |
Frame update loop processes all player inputs uniformly
```

## See Also
- Engine raw input: [../../../../Engine/Source/Input/CLAUDE.md](../../../../Engine/Source/Input/CLAUDE.md)
- Game class: [../Game.h](../Game.h) - PreUpdate() processes MenuInput
- Frame state: [../Frame/Frame.h](../Frame/Frame.h) - Consumes FrameInput during updates
