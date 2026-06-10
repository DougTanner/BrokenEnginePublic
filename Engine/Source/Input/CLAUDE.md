# `/Engine/Source/Input/`

Hardware input polling. The manager is client-only, but the `RawInput` struct and button enums sit outside the `BT_CLIENT` guard so shared game code can hold snapshot members in both builds — game `Input.h` includes `RawInputManager.h` directly because `Engine.h` pulls it in only inside the client span.

## Architecture

`RawInputManager` aggregates a frame-coherent, state-only `RawInput` snapshot — no key bindings, no edge detection. The game layer (`game::Input`) drives `Update()` once per display frame (the engine main loop never calls it) and diffs consecutive snapshots for press/release; deterministic `FrameInput` construction happens there.

Three device paths converge on the snapshot:
- **Keyboard** — Win32 Raw Input (`RIDEV_NOLEGACY` suppresses WM_KEY*); `WM_INPUT` routes via WndProc to `HandleRawInput`, which writes a scratch array only.
- **Mouse** — DirectXTK `Mouse` (absolute mode), fed by Main.cpp's WndProc routing legacy mouse messages to `Mouse::ProcessMessage` — not by this manager; mouse raw-input packets reaching `HandleRawInput` are discarded.
- **Gamepad** — DirectXTK `GamePad` (XInput), pure polling of pad index 0; `SetVibration` passes rumble through (used by game camera shake).

**Two-phase update**: WndProc events mutate scratch state mid-pump; `Update` publishes everything into the snapshot once per frame — decouples event timing from frame timing, so consumers never see a half-frame key state.

## Non-obvious Behaviors

- `ImGui_ImplWin32_WndProcHandler` runs first in WndProc and can consume mouse messages, starving both DirectXTK `Mouse` and `HandleRawInput` — check this when clicks vanish over UI.
- Focus gain registers the raw-input devices and clears the keyboard scratch (stuck-key guard); focus loss unregisters them and freezes the snapshot rather than clearing — consumers keep last-known state. Gamepad suspend/resume mirrors focus.
- Mouse position is normalized against `gpGraphics->mFramebufferExtent2D`, not the client rect — can transiently exceed 0..1 during resize. Scroll wheel is DirectXTK's lifetime accumulator, not a per-frame delta.
- Gamepad construction is try/catch — DirectXTK may throw; all code paths must guard on a null pad pointer. On disconnect, thumbsticks/buttons clear but triggers/dpad retain stale values.
- Cursor trap is the engine's own `ClipCursor` (DirectXTK relative mode is unused), re-applied from `game::gpGame->ShouldTrapCursor()` every frame and forced off on focus loss regardless of game setting.

## See Also
- Game-level input: [Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md](../../../Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md)
