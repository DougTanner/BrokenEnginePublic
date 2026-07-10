# `/Engine/Source/Input/`

Hardware input polling. The manager is client-only, but the `RawInput` struct and button enums sit outside the `BT_CLIENT` guard so shared game code can hold snapshot members in both builds — game `Input.h` includes `RawInputManager.h` directly because `Engine.h` pulls it in only inside the client span.

## Architecture

`RawInputManager` aggregates a frame-coherent, state-only `RawInput` snapshot — no key bindings, no edge detection. The game layer (`game::Input`) drives `Update()` once per display frame (the engine main loop never calls it) and diffs consecutive snapshots for press/release; the deterministic `FrameInput` machinery is entirely game-side.

Three device paths converge on the snapshot:
- **Keyboard** — Win32 Raw Input (`RIDEV_NOLEGACY` suppresses WM_KEY*); `WM_INPUT` routes via WndProc to `HandleRawInput`, which writes a scratch array only. Kept in-house deliberately — DirectXTK `Keyboard` evaluated and rejected: adopting it would lose `RIDEV_NOLEGACY` (Alt+F4 reroutes to `WM_CLOSE`, bypassing the game `kQuit` binding), let ImGui consume WM_KEY*/WM_CHAR via `WantCaptureKeyboard`, and break generic `VK_MENU`/`VK_SHIFT`/`VK_CONTROL` bindings (DirectXTK sets only L/R-specific VKs).
- **Mouse** — DirectXTK `Mouse` (absolute mode), fed by Main.cpp's WndProc routing legacy mouse messages to `Mouse::ProcessMessage` — not by this manager; the mouse is not registered for raw input (only the keyboard is), so no mouse `WM_INPUT` ever reaches `HandleRawInput`.
- **Gamepad** — DirectXTK `GamePad` (XInput), pure polling of pad index 0; `SetVibration` passes rumble through (used by game camera shake).

**Two-phase update**: WndProc events mutate scratch state mid-pump; `Update` publishes everything into the snapshot once per frame — decouples event timing from frame timing, so consumers never see a half-frame key state.

## Non-obvious Behaviors

- `ImGui_ImplWin32_WndProcHandler` runs first in WndProc and can consume mouse messages, starving DirectXTK `Mouse` — check this when clicks vanish over UI.
- The reverse starvation applies to keyboard: `RIDEV_NOLEGACY` suppresses WM_KEY*/WM_CHAR while focused, so ImGui never sees hardware keyboard input — text fields and keyboard nav silently won't work; only gamepad (game `Input.cpp`) and the agent input harness (synthetic ImGui IO key/char/mouse events while a script runs, plus the persistent mouse-pos re-pin below — engine `Agent/`) feed ImGui manually. Accepted today; no ImGui text fields exist.
- Agent synthetic mouse pos feeds two sinks with different lifetimes: the RawInput overlay (game-world) clears on script `Finish`, but the ImGui-IO pos is re-pinned after the Win32 backend in `ImGuiManager::Prepare` (last-writer-wins over the physical cursor) and persists until the next script. Accepted asymmetry between scripts: the UI mouse stays pinned while the game-world mouse reverts — on suppressed (agent-port) clients to the frozen DirectXTK snapshot (physical mouse messages are gated), on non-suppressed clients to the live physical cursor.
- Focus gain registers the keyboard raw-input device and clears the keyboard scratch (stuck-key guard); focus loss unregisters it and freezes the snapshot rather than clearing — consumers keep last-known state. Exception: while an agent input script is active the unfocused early-out is relaxed, so `Update` still publishes each frame with a zeroed keyboard plus the synthetic overlay and injected input drives an unfocused window. Gamepad suspend/resume mirrors focus.
- Mouse position is normalized against `gpGraphics->mFramebufferExtent2D`, not the client rect — can transiently exceed 0..1 during resize. Scroll wheel is DirectXTK's lifetime accumulator, not a per-frame delta; `Update` folds the agent harness's persistent synthetic scroll offset into every published value (consumers diff it, so the offset must never drop out).
- Gamepad construction is try/catch — DirectXTK may throw; all code paths must guard on a null pad pointer. On disconnect, the gamepad snapshot fully clears (thumbsticks, dpad, buttons).
- Cursor trap is the engine's own `ClipCursor` (DirectXTK relative mode is unused), re-applied from `game::gpGame->ShouldTrapCursor()` every frame and forced off on focus loss (regardless of game setting) and for the whole lifetime of an agent-port client (physical-input suppression, below).
- Agent-port clients (`--agent-port` on a `kbAgent` build) suppress all physical human input for the whole process lifetime — the keyboard scratch write, physical mouse messages, and gamepad poll are dropped, the cursor trap is forced off, and `ImGuiManager::Prepare` neutralizes ImGui's physical cursor / gamepad-nav feed. The synthetic `AgentInput` overlay and injected ImGui-IO events become the sole input source. Broader than the per-script unfocused-early-out relaxation above (that relaxes only *while a script runs*); the gate itself (`engine::PhysicalInputSuppressed()`) is documented at the [Engine/Source hub](../CLAUDE.md). Window-lifecycle / close messages (the Alt+F4 escape hatch) stay ungated.
- Construction order is load-bearing: `RawInputManager` is constructed in `Main.cpp` *before* `CreateWindow` because its by-value `Mouse` member backs DirectXTK's internal singleton used by WndProc's static `Mouse::ProcessMessage` calls, and window creation dispatches messages synchronously — an invisible dependency with no code reference connecting the two files.

## See Also
- Game-level input: [Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md](../../../Projects/BrokenEngineSandbox/Source/Input/CLAUDE.md)
