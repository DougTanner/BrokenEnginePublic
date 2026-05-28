# `/Engine/Source/Input/`

Hardware input polling (client-only manager; `RawInput` struct compiles in both builds so shared code can reference the snapshot shape).

## Architecture

`RawInputManager` aggregates a frame-coherent `RawInput` snapshot. On focus gain it registers keyboard (`RIDEV_NOLEGACY` suppresses WM_KEY*) and mouse as Win32 Raw Input devices; keyboard events route to the manager via WndProc, mouse events feed DirectXTK's `Mouse`. Gamepad is pure DirectXTK polling. On focus loss both devices are unregistered.

**Two-phase update**: `HandleRawInput` fires from WndProc per event and writes a scratch keyboard array only. `Update` runs once per frame, polls mouse/gamepad via DirectXTK, then copies the scratch into the snapshot — decouples event timing from frame timing. Snapshot is state-only; game layer diffs consecutive frames for press/release, keeping input deterministic.

## Non-obvious Behaviors

- Focus GAIN clears the keyboard scratch (stuck-key guard); focus LOSS freezes the snapshot rather than clearing — consumers keep last-known state. Gamepad suspend/resume mirrors focus.
- Mouse position is normalized against `gpGraphics->mFramebufferExtent2D`, not the client rect. Scroll wheel is DirectXTK's accumulator, not a per-frame delta.
- Gamepad construction is try/catch — DirectXTK may throw; all code paths must guard on a null pointer. On disconnect, thumbsticks/buttons clear but triggers/dpad retain stale values.
- Cursor trap is driven by `game::gpGame->ShouldTrapCursor()` each frame and forced off on focus loss regardless of game setting.

## See Also
- Game-level input: [Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md](../../../Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md)
