# Input - Game-Specific Input Processing

Client-only (`BT_CLIENT`) conversion of raw hardware input into game and menu commands. Wraps the engine's `RawInputManager`. Server builds use AI-driven behavior and do not compile this code.

**Global**: `gpInput`

## Architecture Notes

- **Two-tier input**: Menu input polls every display frame for responsive UI; per-tick `FrameInput` (a list of `StatusChange`s) is built separately and consumed during the physics tick pipeline. Its serialized layout is versioned — bump `kiVersion` on any on-disk change (replays/saves depend on it).
- **Debug-gated bindings**: Debug/profile/screenshot/debug-render keys compile in via `if constexpr` on `kbDebugInput` / `kbProfiling` / `kbScreenshots` / `kbDebugRender`; new debug-only keys belong inside those blocks.
- **Mode auto-switch**: gamepad engages when `|thumbstick| > 0.1f`; mouse movement or a key in the KBM whitelist (WASD, arrows, numpad 1/2/3/5, LMB/RMB) flips back. New movement keys must extend the whitelist or mode detection misses them.
- **Toggle-detect ordering**: previous-frame menu input is captured after all `*Pressed()` calls for the frame; moving that assignment earlier silently breaks edge detection for the rest of the function.
- **Transfer determinism**: Transfer status changes route through the client's grid coordinate (`mClientGridCoord`) so replay order is deterministic.
- **Variant read**: Reading a `StatusChange` must seat the correct `std::variant` alternative (via `DefaultDataForType`) before `common::Read` runs — `std::visit` assumes the active alternative already matches the type tag.

## See Also

- Engine raw input: [../../../../Engine/Source/Input/CLAUDE.md](../../../../Engine/Source/Input/CLAUDE.md)
- [Documents/Architecture/GameReconciliation.md](../../../../Documents/Architecture/GameReconciliation.md)
