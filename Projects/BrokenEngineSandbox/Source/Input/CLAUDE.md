# /Projects/BrokenEngineSandbox/Source/Input/

Game-specific input processing that converts raw hardware input into game and menu commands. Supports simultaneous keyboard/mouse and gamepad control with automatic mode detection.

## Architecture Overview

**Two-Tier System**: Raw hardware state from engine's RawInputManager is transformed into game-specific MenuInput (UI/system commands) and FrameInput (gameplay controls).

**Automatic Mode Switching**: System detects which input device is being used and switches modes automatically, affecting cursor visibility, aim mechanics, and auto-fire behavior.

**Design Philosophy**: Single unified input processing path handles all game states (menu, paused, playing) rather than separate code paths.

## Core Files

### Input.h

Defines game input structures separating menu navigation from gameplay controls.

**MenuInput**: UI navigation and system commands including pause menu, fullscreen toggle, mouse/gamepad cursor control, and debug commands (quicksave, replay, time scaling, etc.).

**FrameInput**: Gameplay state with two categories:
- **Held input**: Continuous actions (movement direction, aim direction, weapon firing)
- **Pressed input**: One-shot actions (toggle firing modes, activate abilities)

**Input Mode Tracking**: Both structures include gamepad/keyboard-mouse flag for consistent mode tracking across menu and game systems.

**Design Pattern**: Pressed flags are automatically cleared after processing to ensure single-frame behavior for toggle actions.

### Input.cpp

Processes raw input into game-specific commands with automatic device detection.

**Purpose**: Centralizes input mapping and mode detection, converting low-level button states into high-level game actions.

**Input Mode Detection**:
- Monitors all input devices each frame
- Switches to keyboard/mouse mode when WASD, arrow keys, or mouse buttons used
- Switches to gamepad mode when thumbsticks or triggers moved beyond deadzone
- Mouse movement also triggers keyboard mode (for cursor visibility)
- Mode affects UI cursor, aim behavior, and auto-fire mechanics

**Keyboard/Mouse Mapping**:
- Movement from WASD, arrow keys, and numpad (additive, clamped)
- Aim direction calculated via screen-to-world raycast from mouse cursor
- Weapons on mouse buttons with toggle/hold mode support
- Menu navigation via ESC and middle mouse

**Gamepad Mapping**:
- Movement from left thumbstick
- Aim direction from right thumbstick with persistence when released
- Primary fire auto-activated when right stick moved (auto-fire)
- Secondary fire on right trigger
- Menu navigation via left thumbstick and B/menu buttons

**Direction Persistence**: Gamepad aim uses cached direction when thumbstick released to prevent jittering. This allows players to release stick briefly without losing aim.

**Frame Processing Flow**:
1. Detect input mode based on recent activity
2. Process menu commands (always active)
3. Skip gameplay input if in main menu
4. Calculate aim direction (different method for mouse vs gamepad)
5. Accumulate movement from multiple key sources
6. Set weapon firing flags based on mode and input

**Screen-to-World Conversion**: Mouse aiming converts 2D screen coordinates to 3D world position at player height, then calculates direction vector from player to cursor.

**Debug Integration**: Debug builds include additional menu commands for quicksave/load, replay recording, time scaling, and single-stepping.

## Input Flow

```
RawInputManager (Engine) - Polls keyboard/mouse/gamepad hardware
    ↓
ProcessRawInput() - Detects mode, maps inputs to game actions
    ↓
MenuInput + FrameInput - Separated UI and gameplay commands
    ↓
Game::PreUpdate() - Processes menu commands, determines if game should update
    ↓
GameBase::UpdateFramesAndRender() - Consumes FrameInput for player control
```

## Design Patterns

### Held vs Pressed Separation

**Held Input**: State persists until released, checked every frame (movement, firing). Used for analog inputs like aim direction and continuous actions.

**Pressed Input**: Single-frame events, automatically cleared after processing (ability activation, mode toggles). Prevents accidental double-activation.

### Additive Movement

Multiple input sources (WASD + arrows + numpad) accumulate rather than override, allowing players to use any comfortable key layout. Final result clamped to valid range.

### Mode-Specific Behavior

Input mode affects more than just control mapping:
- **Keyboard/Mouse**: Cursor visible, aim at mouse cursor, manual fire
- **Gamepad**: Cursor hidden, aim at thumbstick direction, auto-fire on stick movement

### Direction Persistence (Gamepad)

Cached aim direction prevents jitter when player briefly releases thumbstick:
- Thumbstick magnitude above threshold: update and cache direction
- Thumbstick released: use last cached direction
- Prevents aim snapping during momentary thumbstick release

## Integration Points

**Screen-to-World Projection**: Uses `engine::ScreenToWorld()` to project mouse coordinates onto game plane for aim calculation.

**Game State Queries**: Checks `gpGame->InMainMenu()` to skip gameplay input and `gpGame->meUiState` to prevent firing when UI active.

**Time Control**: Debug inputs directly modify engine's TimeStep for single-stepping and time scaling.

## See Also
- Engine raw input: [../../../../Engine/Source/Input/CLAUDE.md](../../../../Engine/Source/Input/CLAUDE.md)
- Game class: [../Game.h](../Game.h) - PreUpdate() processes MenuInput
- Frame state: [../Frame/Frame.h](../Frame/Frame.h) - Consumes FrameInput during updates
