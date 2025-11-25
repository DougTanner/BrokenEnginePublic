# `/Engine/Source/Input/`

Unified input handling for keyboard, mouse, and gamepad.

**Global**: `gpRawInputManager`

## RawInputManager

Central input system that polls all devices each frame and populates a `RawInput` struct with current state. Keyboard uses Win32 Raw Input API for event-driven capture; mouse and gamepad use DirectXTK for polled state queries.

**Key Operations**:
- `HandleRawInput()` - Process WM_INPUT messages from Windows message loop
- `UpdateFocus()` - Register/unregister devices on window focus changes
- `Update()` - Poll all devices and populate `RawInput` struct
- `TrapCursor()` - Constrain cursor to window bounds during gameplay
- `SetVibration()` - Control gamepad rumble motors

## Design Patterns

**State-Only Tracking**: Tracks current button state only, not transitions. Game-specific input classes compare frames to detect press/release events.

**Focus-Aware**: Clears keyboard state on focus gain to prevent stuck keys. Suspends gamepad polling when unfocused.

**Single Gamepad**: Only gamepad index 0 is supported.
