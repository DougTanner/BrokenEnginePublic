# `/Engine/Source/Input/`

Unified input handling for keyboard, mouse, and gamepad via Raw Input API and DirectXTK.

**Global**: `gpRawInputManager`

## RawInputManager

### Data Structures
- **RawInput** - Complete input state snapshot with current button states
  - Keyboard: 255-element bool array for key states
  - Mouse: Position, 5 mouse buttons, scroll wheel
  - Gamepad: 8 buttons, triggers, thumbsticks, D-pad
- **MouseButtons** - Enum for mouse button indices
- **GamepadButtons** - Enum for gamepad button indices

### Core Methods
- `UpdateHeld()` - Polls all input devices and updates current state each frame
  - Reads keyboard state from raw input buffer
  - Updates mouse position and button states from DirectXTK
  - Reads gamepad thumbsticks, triggers, and D-pad from DirectXTK
  - Updates gamepad button states
  - Manages cursor trapping based on focus and menu state
  - Handles gamepad connection/disconnection logging
  - Applies 200ms delay to scroll wheel input
- `UpdateFocus()` - Register/unregister devices on window focus changes
  - Registers Raw Input for keyboard and mouse when focused
  - Removes device registration when unfocused
  - Manages gamepad suspend/resume
  - Clears keyboard state on focus gain
- `HandleRawInput()` - Process WM_INPUT messages for keyboard
  - Extracts key codes from raw input buffer
  - Updates internal keyboard state array
- `SetVibration()` - Set gamepad rumble motors
- `TrapCursor()` - Constrain cursor to window bounds

### Design Notes
- **State-Only Tracking**: RawInputManager only tracks current input state (which buttons are down), not state transitions
- **No Toggle Detection**: Button press/release detection is handled by game-specific input classes
- **Polled Input**: Mouse and gamepad polled every frame; keyboard uses event-driven Raw Input API
- **Single Gamepad**: Only gamepad index 0 is supported
- **Focus-Aware**: No input processing when window unfocused

### Implementation Notes
- Keyboard: Win32 Raw Input API for low-latency event capture
- Mouse/Gamepad: DirectXTK classes for state polling
- Scroll wheel has cooldown to prevent rapid scrolling
