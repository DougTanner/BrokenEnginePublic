# Camera Zoom ImGui-Capture Gate

## Context

Camera mouse-wheel zoom consumes the wheel with no ImGui-capture or menu gate, so scrolling an ImGui window (menu scroll region, settings list) ALSO zooms the game camera.

Verified against current source (2026-07-10):

- `Input::UpdateCameraInput` (`Projects/BrokenEngineSandbox/Source/Input/Input.cpp:129-136`) diffs `RawInput.iScrollWheelValue` unconditionally every display frame, producing `mCameraInput.iScrollDelta`. It is called from `engine::GameBase::ProcessInput` (`Engine/Source/GameBase.cpp:30`) each frame with no capture check. The one `gpGame->meUiState != UiState::kNone` test in this file (`Input.cpp:88`) gates only the *gamepad* ImGui feed, not scroll.
- `game::Camera::Update` (`Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp:225-240`) reads `gpInput->mCameraInput.iScrollDelta` and applies it to `mfCameraEyeHeightTarget` (eye-height zoom) with no `ImGui::GetIO().WantCaptureMouse` or `meUiState` check. Its only `meUiState` reference (`Camera.cpp:321`) is the graphics-settings slider override — unrelated.
- No `WantCaptureMouse` reference exists anywhere in the game source; there is no existing gate. The coupling is real, not benign.

Consequence: real hardware wheel and the agent-harness synthetic wheel alike zoom the camera while the cursor is over an open menu. Surfaced during `AgentMouseWheelCursorCoupling` execution (targeted-wheel UI scroll); the harness `mouse action:"wheel"` unconditionally bumps `AgentInput::miSyntheticScrollAccumulator` (`AgentInput.cpp:365`) by design, so a wheel meant to scroll a menu also zooms. User-directed follow-up: camera zoom should respect ImGui capture / menu state.

## Design

Gate the scroll-delta consumption on `ImGui::GetIO().WantCaptureMouse` so the camera ignores wheel input that ImGui is claiming for a hovered/scrollable window. `WantCaptureMouse` is the correct, self-updating signal (true whenever ImGui wants the wheel — any hovered window, not only the modal menu states), and it already reflects the frame's hover state.

Open decision for the grill — **gate site**:

- **Option A — gate in `Input::UpdateCameraInput`** (recommended default): when `ImGui::GetIO().WantCaptureMouse` is set, still advance `miPreviousScrollWheelValue` to the current accumulator (so the swallowed ticks are absorbed, not deferred) but set `mCameraInput.iScrollDelta = 0`. Keeps the "wheel routing" decision in the input layer alongside the existing gamepad/menu gating; `Camera` stays a pure consumer. Must preserve the `kScrollWheelInitialized` seeding so first-poll still seeds without emitting a spurious delta.
- **Option B — gate in `game::Camera::Update`**: guard the `if (iScrollDelta != 0)` block on `!ImGui::GetIO().WantCaptureMouse`. Simpler diff, but leaves `mCameraInput.iScrollDelta` non-zero (any other future consumer would still see it) and splits input-routing policy across two files.

Prefer A unless the grill surfaces a reason the delta must remain visible to a non-camera consumer. Whichever site is chosen, absorb the swallowed ticks (advance the previous-value baseline) so releasing capture does not dump an accumulated multi-tick zoom.

## Critical files

- `Input::UpdateCameraInput` — `Projects/BrokenEngineSandbox/Source/Input/Input.cpp:113-138` (scroll-delta derivation; `kScrollWheelInitialized` seeding at `:130-134`). Option A edit site.
- `game::Camera::Update` (`const FrameInterpolate&` overload) — `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp:225-240` (scroll-delta consumption / eye-height retarget). Option B edit site.
- `engine::GameBase::ProcessInput` — `Engine/Source/GameBase.cpp:26-33` (per-frame caller of `UpdateCameraInput`; no change, context only).
- `ImGui::GetIO().WantCaptureMouse` — the gate signal; ImGui is already initialized and its IO already populated by the time input polls run.

## Out of scope

- The harness dual-sink design: `AgentInput::miSyntheticScrollAccumulator` bump stays **unconditional** (`AgentInput.cpp:365`). The synthetic wheel still feeds ImGui IO; only the *camera's* consumption of the resulting delta is gated. `AgentMouseWheelCursorCoupling` (targeted-wheel synthetic-pos routing) is a separate, complementary fix.
- Keyboard / gamepad capture (`WantCaptureKeyboard`) and free-camera WASD gating — untouched.
- Any change to how `RawInput.iScrollWheelValue` is accumulated in the engine `RawInputManager`.
- Non-wheel camera input (move axis, jump, shake).

## Acceptance criteria

- Wheel over an open menu (Pause / Graphics settings / Sound / Tweaks scroll region) scrolls the menu and does **not** change camera eye height (`mfCameraEyeHeightTarget` unchanged across the scroll).
- Wheel over the world (no ImGui window claiming the mouse) still zooms the camera exactly as before.
- Releasing capture (cursor leaves the menu) does not dump an accumulated burst of zoom — swallowed ticks were absorbed, not deferred.
- First-poll scroll seeding still produces no spurious zoom.

Runtime-observable via the agent harness: `describe_scene` reports camera state / eye height; `describe_ui` + `mouse action:"wheel"` with an over-menu coordinate can drive the check (depends on `AgentMouseWheelCursorCoupling`'s targeted-wheel routing to place the synthetic wheel over the menu).

## Notes

- **Invariant exposure**: client-only input/visual path. Camera eye-height state is **not** CRC'd, not on the wire, and carries no `kiVersion` / `.pack` layout. No determinism, replay, or save-format exposure.
- This changes **user-visible input behavior** (wheel over UI no longer zooms), so it needs a playtest / user judgement pass — hence the moderate risk despite the tiny diff.
- Single open decision pre-staged for `/external-grill-plan`: gate site (Option A in the input layer vs Option B in `Camera`). Recommend A.
