# /Projects/BrokenEngineSandbox/Source/Input/

Game-specific input processing that converts raw hardware input into game and menu commands. Supports simultaneous keyboard/mouse and gamepad control with automatic mode detection.

**Global**: `gpInput`

## Architecture Overview

**Three-Tier System**: Raw hardware state from engine's RawInputManager → Input class for toggle detection → Game-specific MenuInput and FrameInput structures.

**Toggle Detection**: Input class tracks previous and current frame state, detecting button press/release transitions. Menu input updated every frame, frame input updated only during physics steps.

**Automatic Mode Switching**: System detects which input device is being used and switches modes automatically, affecting cursor visibility, aim mechanics, and auto-fire behavior.

**Design Philosophy**: Single unified input processing path handles all game states (menu, paused, playing) rather than separate code paths.

## Core Files

### Input.h

Defines game input structures and the Input class that manages toggle detection.

**Input Class**: Manages state tracking for button press/release detection
- Stores previous RawInput frames for menu and frame input separately
- `UpdateMenuInput()` - Processes menu input every frame, updates gamepad mode detection, returns quit flag
- `UpdateFrameInputPressed()` - Processes frame input during physics steps, returns one-shot events
- `GetMenuInput()` / `GetGamepadMode()` - Retrieve processed input and current input device mode
- Private helper methods (`KeyboardPressed`, `MousePressed`, `GamepadPressed`) encapsulate toggle detection using stored previous state

**MenuInput**: UI navigation and system commands including pause menu, fullscreen toggle, mouse/gamepad cursor control, and debug commands (quicksave, replay, time scaling).

**FrameInputHeld**: Continuous gameplay state including movement direction, aim direction, and weapon firing flags. Persists across frames until input is released. Based directly on current RawInput state.

**FrameInputPressed**: One-shot gameplay events for toggle actions like activating dash ability. Automatically cleared after processing to ensure single-frame behavior. Requires state comparison to detect transitions.

**Design Pattern**: Separating held and pressed input allows efficient replay compression - only changes need to be recorded rather than full state every frame.

### Input.cpp

Implements Input class and processes raw input into game-specific commands with automatic device detection.

**Input Class Implementation**:
- Toggle detection encapsulated in private helper methods for cleaner call sites
- Separate previous state tracking for menu and frame input to avoid interference
- Gamepad mode tracking updated during menu input processing
- Trigger threshold tracking for analog-to-digital conversion (left trigger for dash ability)

**Input Mode Detection**:
- Monitors all input devices each frame
- Switches to keyboard/mouse mode when movement keys or mouse buttons used
- Switches to gamepad mode when thumbsticks or triggers moved beyond deadzone
- Mouse movement also triggers keyboard mode for cursor visibility
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

**Direction Persistence**: Gamepad aim uses cached direction when thumbstick released to prevent jittering, allowing players to release stick briefly without losing aim.

**Free Functions**:
- `RawInputToFrameInputHeld()` - Extracts continuous gameplay state directly from RawInput without requiring toggle detection
- `WasPressed()` - Utility for comparing boolean/keyboard state between frames, used in UpdateFrameInputPressed for toggle detection

**Frame Processing Flow**:
1. Detect input mode based on recent activity
2. Process menu commands using helper methods for toggle detection
3. Skip gameplay input if in main menu
4. Calculate aim direction (different method for mouse vs gamepad)
5. Accumulate movement from multiple key sources
6. Set weapon firing flags based on mode and input
7. Detect button press events for abilities using helper methods and state comparison

**Screen-to-World Conversion**: Mouse aiming converts 2D screen coordinates to 3D world position at player height, then calculates direction vector from player to cursor.

**Debug Integration**: Debug builds include additional menu commands for quicksave/load, replay recording, time scaling, and single-stepping.

## Input Flow

```
RawInputManager (Engine) - Polls keyboard/mouse/gamepad hardware, tracks current state
    ↓
Input::UpdateMenuInput() - Every frame in Main.cpp
    ├─ Compares current vs previous state for toggle detection
    ├─ Updates gamepad mode based on device activity
    └─ Produces MenuInput for UI and system commands
    ↓
Input::UpdateFrameInput() - Only during physics steps in GameBase
    ├─ Uses state already saved by UpdateMenuInput()
    ├─ Compares current vs previous state for toggle detection
    └─ Produces FrameInputPressed for one-shot events
    ↓
RawInputToFrameInputHeld() - Called before each physics step
    └─ Extracts continuous state directly from current RawInput
    ↓
MenuInput + FrameInputHeld + FrameInputPressed - Separated UI and gameplay commands
    ↓
Game::PreUpdate() - Processes menu commands, determines if game should update
Game::ProcessSavesAndReplays() - Handles save/load/replay, can override input
    ↓
GameBase::UpdateFramesAndRender() - Consumes FrameInputHeld/Pressed for player control
```

## Design Patterns

### Menu vs Frame Update Timing

**Menu Input**: Updated every frame in main loop for responsive UI, even when game is paused or in menu.

**Frame Input**: Updated only during physics steps at fixed timestep (250Hz), ensuring deterministic gameplay for replay system.

**State Management**: Input class updates previous/current state once in UpdateMenuInput(), reused by UpdateFrameInput() to ensure consistent toggle detection.

### Held vs Pressed Separation

**Held Input**: State persists until released, checked every frame (movement, firing). Used for analog inputs like aim direction and continuous actions. No toggle detection needed.

**Pressed Input**: Single-frame events detected via state comparison (ability activation, mode toggles). Prevents accidental double-activation. Requires Input class tracking.

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
