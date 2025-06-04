# `/Engine/Source/Input/`

Unified input handling for keyboard, mouse, and gamepad via Raw Input API and DirectXTK.

**Global**: `gpRawInputManager`

## InputToggle.h

Template utility for tracking button state transitions.

- `UpdateToggle()` - Detect press/release transitions
- `IsDown()` - Currently held
- `WasPressed()` - Pressed this frame
- `WasReleased()` - Released this frame

## RawInputManager

### Data Structures
- **RawInput** - Complete input state snapshot
  - Keyboard: 255 key states array
  - Mouse: Position, buttons (5), scroll
  - Gamepad: Buttons, triggers, sticks
- **MouseButtons** - Left, Middle, Right, Extra1, Extra2
- **GamepadButtons** - A, B, X, Y, LShoulder, RShoulder, Start, Menu

### Core Methods
- `Update()` - Poll all devices, return RawInput struct
  - Process keyboard raw input buffer
  - Apply 200ms scroll wheel delay
- `UpdateFocus()` - Register/unregister based on window focus
- `SetVibration(left, right)` - Gamepad rumble
- `TrapCursor()` - Constrain to window
- `HandleRawInput()` - Process WM_INPUT messages

### Implementation Notes
- Keyboard: Win32 Raw Input API
- Mouse/Gamepad: DirectXTK classes
- No input when window unfocused
- Limited to gamepad index 0
- Polled input (except keyboard events)
