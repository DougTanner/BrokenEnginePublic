# `/Engine/Source/Input/`

Engine-level hardware input polling for keyboard, mouse, and gamepad. Client-only instantiation (`#ifdef BT_CLIENT` in Main.cpp); the class compiles in both builds but `gpRawInputManager` is null in server builds. Code that accesses the global pointer (e.g., WndProc, GameBase) null-checks before use.

**Global**: `gpRawInputManager` (client-only, null in server builds)

## RawInputManager

Central input system that polls all devices each frame and populates a `RawInput` struct with current state. Keyboard uses Win32 Raw Input API for event-driven capture; mouse and gamepad use DirectXTK for polled state queries. Mouse position normalization uses Graphics framebuffer dimensions (client-only via `#ifdef BT_CLIENT`). Also provides gamepad vibration control and cursor trapping.

**RawInput Struct**: Flat struct aggregating current-frame state across all supported input devices. Populated by RawInputManager each frame and consumed by game-level input processing.

## Design Patterns

**State-Only Tracking**: Tracks current button state only, not transitions. Game-specific input classes compare frames to detect press/release events.

**Focus-Aware**: Registers/unregisters raw input devices on window focus changes. Clears keyboard state on focus gain to prevent stuck keys. Suspends gamepad polling when unfocused.

**Cursor Trapping**: Constrains cursor to window bounds during active gameplay, driven by `game::gpGame->ShouldTrapCursor()`.

**Workbuffer Usage**: `HandleRawInput()` uses the thread-local workbuffer (`PushBuffer`/`Pop`) for temporary allocation when parsing Win32 raw input messages.

**Single Gamepad**: Only gamepad index 0 is supported.

## See Also
- Game-level input processing: [../../../Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md](../../../Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md) - Converts RawInput into game-specific MenuInput and FrameInput
