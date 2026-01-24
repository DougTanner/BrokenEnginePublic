# /Projects/BrokenEngineSandbox/Source/Input/

Game-specific input processing that converts raw hardware input into game and menu commands. Supports simultaneous keyboard/mouse and gamepad control with automatic mode detection.

**Global**: `gpInput`

## Architecture Overview

**Three-Tier System**: Raw hardware state from engine's RawInputManager → Input class for toggle detection → Game-specific MenuInput and FrameInput structures.

**Automatic Mode Switching**: System monitors all input devices each frame and automatically switches between keyboard/mouse and gamepad modes. Mode affects cursor visibility, aim mechanics (screen-to-world raycast vs thumbstick direction), and auto-fire behavior (gamepad auto-fires when right stick moved).

## Core Components

### Input Class

Manages state tracking for button press/release detection with separate previous state for menu and frame input to avoid interference.

- `UpdateMenuInput()` - Processes menu input every frame, updates gamepad mode detection, returns quit flag
- `UpdateFrameInputPressed()` - Processes frame input during physics steps for one-shot events
- Private helper methods encapsulate toggle detection by comparing current vs previous state

### MenuInput

UI navigation and system commands including pause menu, fullscreen toggle, mouse/gamepad cursor control. When `kbEnableDebugInput` is true, adds quicksave/load, replay, time scaling, and debug menu commands (via `if constexpr`). When `kbEnableScreenshots` is true, adds screenshot toggle.

**ImGui Gamepad Integration**: When menus are visible (`meUiState != kNone`), `UpdateMenuInput()` populates ImGui's gamepad input state via `AddKeyEvent()` and `AddKeyAnalogEvent()`. Maps D-pad for navigation, A/B buttons for activate/cancel, and left thumbstick for analog navigation.

### FrameInput

Combined held and pressed input for gameplay. Separating held/pressed enables efficient replay compression since only changes need recording.

- **Held**: Movement direction, aim direction, weapon firing flags (persist until released)
- **Pressed**: One-shot events like skill activation (cleared after processing via `ClearPressed()`)

### RawInputToFrameInput()

Free function that extracts continuous gameplay state directly from RawInput. Handles gamepad direction persistence (caches aim when thumbstick released to prevent jitter) and additive keyboard movement (WASD + arrows + numpad accumulate, then clamp).

**ImGui Integration**: When ImGui is shown and wants to capture mouse or keyboard input for interactive widgets, frame input is automatically blocked to prevent duplicate input processing. ImGui's `WantCaptureMouse` and `WantCaptureKeyboard` flags control this behavior (available only when `kbEnableDebugInput` is true, checked via `if constexpr`).

## Input Flow

```
RawInputManager (Engine) - Polls hardware state
    ↓
Input::UpdateMenuInput() - Every frame in Main.cpp
    └─ Produces MenuInput, updates gamepad mode
    ↓
RawInputToFrameInput() - Before each physics step
    └─ Extracts held state, handles screen-to-world aim
    ↓
Input::UpdateFrameInputPressed() - During physics steps
    └─ Detects one-shot events via state comparison
    ↓
Game::PreUpdate() → GameBase::UpdateFramesAndRender()
```

## Design Patterns

### Menu vs Frame Update Timing

Menu input updates every frame for responsive UI (even when paused). Frame input updates only during physics steps at fixed timestep for deterministic replay.

### Mode-Specific Behavior

- **Keyboard/Mouse**: Cursor visible, aim at mouse cursor via screen-to-world projection, manual fire
- **Gamepad**: Cursor hidden, aim at thumbstick direction with persistence, auto-fire on stick movement

## See Also
- Engine raw input: [../../../../Engine/Source/Input/CLAUDE.md](../../../../Engine/Source/Input/CLAUDE.md)
- Game class: [../Game.h](../Game.h) - PreUpdate() processes MenuInput
- Frame state: [../Frame/Frame.h](../Frame/Frame.h) - Consumes FrameInput during updates
