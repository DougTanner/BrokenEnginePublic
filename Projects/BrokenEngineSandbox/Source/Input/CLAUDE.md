# Input - Game-Specific Input Processing

Client-only (`BT_CLIENT`) conversion of raw hardware input into game and menu commands. Wraps the engine's `RawInputManager`. Server builds use AI-driven behavior and do not compile this code.

**Global**: `gpInput`

## Architecture Notes

- **Three input paths**: Menu actions and continuous camera input both poll every display frame for responsive UI; per-tick `FrameInput` (a list of `StatusChange`s) is built elsewhere (game/frame layer) and consumed during the physics tick pipeline. Camera input carries a continuous move axis and a per-frame scroll-wheel delta (the engine snapshot exposes only an accumulator, so the delta is derived here against a previous value seeded on first poll).
- **`FrameInput` serialization**: Its layout is versioned — bump `kiVersion` on any on-disk change (replays/saves depend on it). `Crc()` folds each `StatusChange`'s type tag and payload into the per-tick checksum used for reconciliation.
- **Debug-gated bindings**: Debug/profile/screenshot/debug-render keys compile in via `if constexpr` on `kbDebugInput` / `kbProfiling` / `kbScreenshots` / `kbDebugRender`; free-camera WASD movement gates on `kbFreeCamera`. New debug-only keys belong inside those blocks.
- **Mode auto-switch**: gamepad engages when either thumbstick's `|x| + |y|` exceeds the threshold; mouse movement or a key in the KBM whitelist (WASD, arrows, numpad 1/2/3/5, LMB/RMB) flips back. New movement keys must extend the whitelist or mode detection misses them.
- **Toggle-detect ordering**: previous-frame menu input is captured after all `*Pressed()` edge-detection calls for the frame; moving that assignment earlier silently breaks edge detection for the rest of the function.
- **ImGui gamepad feed**: while a menu is up (`gpGame->meUiState != UiState::kNone`), gamepad buttons / d-pad / left stick are pushed into ImGui's IO so menus are pad-navigable.
- **Variant read**: Reading a `StatusChange` must seat the correct `std::variant` alternative (via `DefaultDataForType`) before `common::Read` runs — `std::visit` assumes the active alternative already matches the type tag.

## See Also

- Engine raw input: [../../../../Engine/Source/Input/CLAUDE.md](../../../../Engine/Source/Input/CLAUDE.md)
- [Documents/Architecture/GameReconciliation.md](../../../../Documents/Architecture/GameReconciliation.md)
