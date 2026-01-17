# `/Engine/Source/Input/`

Unified input handling for keyboard, mouse, and gamepad.

**Global**: `gpRawInputManager`

## RawInputManager

Central input system that polls all devices each frame and populates a `RawInput` struct with current state. Keyboard uses Win32 Raw Input API for event-driven capture; mouse and gamepad use DirectXTK for polled state queries.

## Design Patterns

**State-Only Tracking**: Tracks current button state only, not transitions. Game-specific input classes compare frames to detect press/release events.

**Focus-Aware**: Registers/unregisters raw input devices on window focus changes. Clears keyboard state on focus gain to prevent stuck keys. Suspends gamepad polling when unfocused.

**Cursor Trapping**: Constrains cursor to window bounds during active gameplay based on game state.

**Single Gamepad**: Only gamepad index 0 is supported.
