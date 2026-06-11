# Input - Game-Specific Input Processing

Two responsibilities share this directory: the `Input` class converts the engine `RawInputManager` snapshot into menu and camera commands each display frame (client-only at runtime), and `FrameInput` — the per-tick deterministic unit (a versioned, serializable list of `StatusChange`s) — which compiles in both builds and is consumed by the physics tick pipeline, server broadcast, and replay system.

**Global**: `gpInput` — constructed in `Engine/Source/Main.cpp` under `BT_CLIENT`; nullptr on the server (all dereferences are client-gated). Polled only from `engine::GameBase::ProcessInput`; `game::Camera` reads `mCameraInput` directly.

## Architecture Notes

- **Guard scope is load-bearing**: the files appear in both client and server vcxprojs. Only the polling method *bodies* are `BT_CLIENT`-gated; the `FrameInput` machinery is shared (server replay and broadcast depend on it). Don't wrap the whole file — per the root rule, a fully-guarded file would have to leave the server project.
- **Three input cadences**: menu actions (display-rate, edge-detected against the previous snapshot) and camera input (display-rate, continuous move axis + scroll-wheel delta — the engine snapshot exposes only an accumulator, so the delta is derived here against a previous value seeded on first poll) are produced here. `FrameInput` instances are never built from hardware in this directory — the server broadcaster, client reconciler, and replay reader mint them.
- **`FrameInput` serialization**: layout is versioned — bump `kiVersion` on any change to the stream format or `StatusChange` payloads (replays validate the version; size isn't checked for non-trivially-copyable types, so the bump is the only guard). `Crc()` serves the replay `DifferenceStream` (skip writing identical consecutive differences) and diagnostics; reconciliation desync detection compares `Frame` CRCs, not `FrameInput::Crc()`.
- **Variant read**: reading a `StatusChange` must seat the correct `std::variant` alternative (via `DefaultDataForType`) before `common::Read` runs — `std::visit` assumes the active alternative already matches the type tag.
- **Debug-gated bindings**: debug/profile/screenshot/debug-render keys compile in via `if constexpr` on `kbDebugInput` / `kbProfiling` / `kbScreenshots` / `kbDebugRender`; free-camera WASD gates on `kbFreeCamera`. New debug-only keys belong inside those blocks.
- **Mode auto-switch**: gamepad engages when either thumbstick's `|x| + |y|` exceeds the threshold; mouse movement or a key in the KBM whitelist (WASD, arrows, numpad 1/2/3/5, LMB/RMB) flips back. New movement keys must extend the whitelist or mode detection misses them.
- **Toggle-detect ordering**: previous-frame menu input is captured after all `*Pressed()` edge-detection calls for the frame; moving that assignment earlier silently breaks edge detection for the rest of the function.
- **ImGui gamepad feed**: while a menu is up (`gpGame->meUiState != UiState::kNone`), gamepad buttons / d-pad / left stick are pushed into ImGui's IO so menus are pad-navigable.

## See Also

- Engine raw input: [../../../../Engine/Source/Input/CLAUDE.md](../../../../Engine/Source/Input/CLAUDE.md)
- [Documents/Architecture/GameReconciliation.md](../../../../Documents/Architecture/GameReconciliation.md)
