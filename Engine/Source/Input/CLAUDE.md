# `/Engine/Source/Input/`

Hardware input polling. The manager is client-only, but the `RawInput` struct and button enums sit outside the `BT_CLIENT` guard so shared game code can hold snapshot members in both builds — game `Input.h` includes `RawInputManager.h` directly because `Engine.h` pulls it in only inside the client span.

## Architecture

`RawInputManager` aggregates a frame-coherent, state-only `RawInput` snapshot — no key bindings, no edge detection. The game layer (`game::Input`) drives `Update()` once per display frame (the engine main loop never calls it) and diffs consecutive snapshots for press/release; the deterministic `FrameInput` machinery is entirely game-side.

Three device paths converge on the snapshot:
- **Keyboard** — Win32 Raw Input (`RIDEV_NOLEGACY` suppresses WM_KEY*); `WM_INPUT` routes via WndProc to `HandleRawInput`, which writes a scratch array only. Kept in-house deliberately — DirectXTK `Keyboard` evaluated and rejected: adopting it would lose `RIDEV_NOLEGACY` (Alt+F4 reroutes to `WM_CLOSE`, bypassing the game `kQuit` binding), let ImGui consume WM_KEY*/WM_CHAR via `WantCaptureKeyboard`, and break generic `VK_MENU`/`VK_SHIFT`/`VK_CONTROL` bindings (DirectXTK sets only L/R-specific VKs).
- **Mouse** — DirectXTK `Mouse` (absolute mode), fed by Main.cpp's WndProc routing legacy mouse messages to `Mouse::ProcessMessage` — not by this manager; mouse raw-input packets reaching `HandleRawInput` are discarded.
- **Gamepad** — DirectXTK `GamePad` (XInput), pure polling of pad index 0; `SetVibration` passes rumble through (used by game camera shake).

**Two-phase update**: WndProc events mutate scratch state mid-pump; `Update` publishes everything into the snapshot once per frame — decouples event timing from frame timing, so consumers never see a half-frame key state.

## Non-obvious Behaviors

- `ImGui_ImplWin32_WndProcHandler` runs first in WndProc and can consume mouse messages, starving both DirectXTK `Mouse` and `HandleRawInput` — check this when clicks vanish over UI.
- The reverse starvation applies to keyboard: `RIDEV_NOLEGACY` suppresses WM_KEY*/WM_CHAR while focused, so ImGui never sees keyboard input — text fields and keyboard nav silently won't work (only gamepad is manually fed to ImGui, game `Input.cpp`). Accepted today; no ImGui text fields exist.
- Focus gain registers the raw-input devices and clears the keyboard scratch (stuck-key guard); focus loss unregisters them and freezes the snapshot rather than clearing — consumers keep last-known state. Gamepad suspend/resume mirrors focus.
- Mouse position is normalized against `gpGraphics->mFramebufferExtent2D`, not the client rect — can transiently exceed 0..1 during resize. Scroll wheel is DirectXTK's lifetime accumulator, not a per-frame delta.
- Gamepad construction is try/catch — DirectXTK may throw; all code paths must guard on a null pad pointer. On disconnect, the gamepad snapshot fully clears (thumbsticks, dpad, buttons).
- Cursor trap is the engine's own `ClipCursor` (DirectXTK relative mode is unused), re-applied from `game::gpGame->ShouldTrapCursor()` every frame and forced off on focus loss regardless of game setting.
- Construction order is load-bearing: `RawInputManager` is constructed in `Main.cpp` *before* `CreateWindow` because its by-value `Mouse` member backs DirectXTK's internal singleton used by WndProc's static `Mouse::ProcessMessage` calls, and window creation dispatches messages synchronously — an invisible dependency with no code reference connecting the two files.

## See Also
- Game-level input: [Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md](../../../Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md)
