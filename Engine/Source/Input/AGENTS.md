# Input - Hardware Snapshot Sharing

`RawInputManager` is client-only, while the state-only `RawInput` types remain shared so game code can embed snapshots in both builds. Key bindings, edge detection, and deterministic `FrameInput` construction are game-owned.

## Device and Frame Boundaries

- Win32 Raw Input owns keyboard state with `RIDEV_NOLEGACY`; window events write scratch state and `Update()` publishes one coherent snapshot per display frame. Mouse state comes from DirectXTK's Win32 message path, and gamepad state is polled from XInput.
- Because legacy keyboard messages are suppressed, hardware keyboard input does not feed ImGui. Gamepad navigation and agent-injected ImGui events use separate explicit paths.
- Mouse wheel state is a lifetime accumulator that consumers diff. Mouse normalization uses the Vulkan framebuffer extent and may briefly leave the 0..1 range during resize.
- The DirectXTK mouse object must exist before window creation because synchronous window messages call its static processing path.
- Keyboard handling is hand-rolled on purpose rather than routed through DirectXTK's `Keyboard`. Adopting it would give up `RIDEV_NOLEGACY`, so Alt+F4 would arrive as an ordinary window-close message and bypass the game quit binding; it would let ImGui swallow keyboard messages; and it would break bindings that test the generic `VK_MENU`/`VK_SHIFT`/`VK_CONTROL` keys, because DirectXTK reports only the left/right-specific ones. Do not unify the keyboard onto DirectXTK to match the mouse and gamepad paths, and do not add `Keyboard.cpp` to the compiled DirectXTK units.
- Game `Input.h` includes `Input/RawInputManager.h` directly because `Engine.h` pulls that header in only inside its client-only span, while the shared `RawInput` struct sits outside the header's own client guard. That direct include is what lets the server build see `RawInput`; it is not redundant, and moving the whole header inside the client guard breaks shared `FrameInput` code.

## Focus and Agent Input

- Focus gain registers the keyboard and clears scratch state; focus loss unregisters it and freezes the published snapshot. Gamepad suspend/resume follows focus.
- An active agent script may publish synthetic input while unfocused. Agent-port clients suppress physical keyboard, mouse, gamepad, and cursor trapping for the process lifetime while preserving window close handling.
- Synthetic game input overlays the published snapshot; synthetic ImGui mouse position is re-applied after the Win32 backend. Keep these sinks separate because their lifetimes differ.
- Cursor trapping uses Win32 `ClipCursor`, follows game policy while focused, and is forced off on focus loss or physical-input suppression.
- Gamepad construction may fail; all polling and vibration paths support a null gamepad object and clear the published pad state on disconnect.

## See Also

- Engine Agent (`../Agent/AGENTS.md`) - Synthetic input ownership
- Game input (`../../../Projects/BrokenEngineSandbox/Source/Input/AGENTS.md`) - Bindings, edges, and deterministic serialization
