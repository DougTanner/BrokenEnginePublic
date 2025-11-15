# `/Engine/Source/Input/`

Unified input handling for keyboard, mouse, and gamepad via Raw Input API and DirectXTK.

**Global**: `gpRawInputManager`

## RawInputManager

Central input system that polls all input devices and populates a `RawInput` struct containing current frame state.

### Core Functionality

**Device Registration**: `UpdateFocus()` registers/unregisters Raw Input devices when window gains/loses focus. Keyboard and mouse use Win32 Raw Input API for low-latency capture. Gamepad uses DirectXTK GamePad class for state polling.

**Input Polling**: `Update()` reads current state from all devices each frame and populates the `RawInput` struct. Keyboard state maintained in internal array and copied to output. Mouse position normalized to framebuffer coordinates. Gamepad polls thumbsticks, triggers, D-pad, and 8 buttons. Only gamepad index 0 supported.

**Event Processing**: `HandleRawInput()` processes WM_INPUT messages from Windows message loop, extracting keyboard key codes and updating internal state array.

**Cursor Management**: `TrapCursor()` constrains cursor to window bounds during gameplay (disabled in main menu).

**Vibration**: `SetVibration()` controls gamepad rumble motors via DirectXTK.

### RawInput Struct

Complete snapshot of all input device states for a single frame. Contains bool arrays for keyboard keys (255 elements), mouse buttons (5 elements), and gamepad buttons (8 elements). Also contains mouse position, scroll wheel value, gamepad thumbsticks, D-pad, and triggers.

### Design Patterns

**State-Only Tracking**: RawInputManager only tracks current input state (which buttons are down), not state transitions. Button press/release detection is handled by game-specific input classes that compare current and previous frame states.

**Focus-Aware**: No input processing when window unfocused. Keyboard state cleared on focus gain to prevent stuck keys.

**Hybrid Polling Model**: Keyboard uses event-driven Raw Input API for precise capture timing. Mouse and gamepad use polled state queries for simplicity.

### Threading Model

All input operations occur on main thread. No background threads or async operations.
