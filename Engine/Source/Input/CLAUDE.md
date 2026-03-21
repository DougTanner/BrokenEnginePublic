# `/Engine/Source/Input/`

Engine-level hardware input polling for keyboard, mouse, and gamepad. The `RawInput` struct and button enums compile in both builds; the `RawInputManager` class and `gpRawInputManager` global exist only in `BT_CLIENT` builds.

## Architecture

**RawInputManager** polls all input devices each frame and populates a `RawInput` struct with current-frame state. Keyboard capture uses Win32 Raw Input API (event-driven via `HandleRawInput`); mouse and gamepad use DirectXTK (polled). Mouse position is normalized against framebuffer dimensions.

**RawInput** is a flat struct aggregating keyboard, mouse, and gamepad state for a single frame. Consumed by game-level input processing which compares consecutive frames to detect press/release transitions.

## Key Behaviors

- **State-only tracking**: Records current button state, not transitions. Game-layer input classes diff consecutive frames to detect press/release — this ensures deterministic frame input since transition detection is deferred to the game layer
- **Focus-aware**: Registers/unregisters raw input devices on window focus changes; clears keyboard state on focus gain to prevent stuck keys; suspends gamepad polling when unfocused
- **Cursor trapping**: Constrains cursor to window bounds during gameplay, driven by `game::gpGame->ShouldTrapCursor()`

## See Also
- Game-level input: [Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md](../../../Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md)
