# Architecture: Library Replacement (DirectXTK Keyboard)

**Decision plan (present options)** — resolve via `/external-grill-plan` before any edit. Recommendation: **keep the in-house path** (Option A); this plan records the evaluated alternative so it isn't re-litigated each review.

## Context
Source: /external-architecture-review on `Engine/Source/Input` (ThirdParty replacement scan). The in-house Win32 Raw Input keyboard path (~60-80 lines across `HandleRawInput`, the keyboard halves of `UpdateFocus`, the scratch array, and Main.cpp's `WM_INPUT` routing) duplicates a capability of DirectXTK — already vendored and used here for `Mouse`/`GamePad`. `Keyboard.cpp` is *not* currently compiled (the unity unit `ThirdParty/Prebuilts/Source/Engine/DirectXTK.cpp:9-12` includes only AudioEngine/SoundCommon/GamePad/Mouse), so adoption is one include line plus WndProc glue — no new library, no new license.

## Candidate
- **Module/cluster**: `RawInputManager.cpp` keyboard path — `HandleRawInput` (lines 217-240), keyboard register/unregister + scratch-clear in `UpdateFocus` (lines 37-54, 61, 67-81), snapshot copy loop in `Update` (lines 142-146); `RawInputManager.h` `mpbKeyboardKeysDown` (line 67); `Main.cpp` `WM_INPUT` case (lines 572-578).
- **Lines removable**: ~70-80 gross, ~40-50 net after glue (Keyboard member, `Keyboard::ProcessMessage` WndProc cases, focus `Reset()`, GetState→snapshot copy, one unity-build include).
- **Proposed library**: DirectXTK `Keyboard` — **MIT** (on the `ThirdParty/CLAUDE.md` allow list; library already imported — this is coverage extension, not a new import). Microsoft-maintained, same toolkit whose Mouse/GamePad/Audio paths this engine ships.

## Options

### Option A — Keep in-house (recommended)
The raw-input path exists specifically for `RIDEV_NOLEGACY` semantics, documented as deliberate (`Input/CLAUDE.md`): legacy `WM_KEY*` suppression keeps Alt+F4 away from `DefWindowProc` (quit is game-implemented: `Input.cpp:37` reads generic `VK_MENU` + F4 edge) and keeps `WM_KEY*`/`WM_CHAR` away from `ImGui_ImplWin32_WndProcHandler` (which runs first in WndProc and could start consuming keys via `WantCaptureKeyboard`). No code change; close this plan as evaluated-and-rejected.

### Option B — Adopt DirectXTK Keyboard
Add `Src/Keyboard.cpp` to the unity unit, hold a `Keyboard` member, route `WM_KEYDOWN/UP`/`WM_SYSKEYDOWN/UP` to `Keyboard::ProcessMessage`, copy `GetState()` into the snapshot, `Reset()` on focus gain (replaces the stuck-key scratch clear — equivalent coverage). Accepted behavior changes (each needs an explicit yes):
1. **`RIDEV_NOLEGACY` is lost** — legacy keyboard messages re-enabled. Alt+F4 reaches `DefWindowProc` → window close, bypassing the game's quit flow. (`SC_KEYMENU` is already suppressed separately, `Main.cpp:520`, so the Alt-menu modal loop stays covered.)
2. **ImGui starts receiving `WM_KEY*`/`WM_CHAR`** — `WantCaptureKeyboard` may consume keys that today always reach the game.
3. **VK remapping** — DirectXTK maps shift/ctrl/alt to left/right-specific VKs and does not set the generic `VK_SHIFT`/`VK_CONTROL`/`VK_MENU` bits; the `Input.cpp:37` `VK_MENU` binding (and any future generic-VK binding) must be remapped.

## Risks
- Option B: product-behavior changes above; bindings audit; no perf concern (256-bit bitfield read vs 255-bool copy); zero new transitive deps.
- Rejected alternatives (for the record): SDL2/SDL3 (zlib — owns the window/event pump, massive integration cost), GLFW (zlib — window-ownership conflict), gainput (Apache-2.0 — unmaintained, redundant with DirectXTK). None rejected for license.

## Critical files
- `ThirdParty/Prebuilts/Source/Engine/DirectXTK.cpp` (Option B only)
- `Engine/Source/Input/RawInputManager.h` / `.cpp`
- `Engine/Source/Main.cpp`
- `Projects/BrokenEngineSandbox/Source/Input/Input.cpp` (binding remap, Option B only)

## Out of scope
- Mouse/GamePad paths (already DirectXTK), cursor-trap policy, `RawInput` struct shape.

## Notes
- No determinism/CRC exposure — keyboard state is display-rate, never feeds `FrameInput` directly.
- If Option B is ever chosen, `Input/Refactor_RawInputHandling.md`'s `HandleRawInput` item and parts of `Engine/DeadCodeAndUnusedIncludesSweep.md`'s mouse-registration item evaporate (the whole raw-input block deletes) — re-check both before executing.
- Grill decision: Option A vs B (product behavior, user call).

## Verification Notes
- License/import claims re-verified: MIT is on the `ThirdParty/CLAUDE.md` allow list; DirectXTK is already vendored (inventory entry, `USING_XINPUT` build); `ThirdParty/Prebuilts/Source/Engine/DirectXTK.cpp:9-12` compiles only AudioEngine/SoundCommon/GamePad/Mouse; `ThirdParty/DirectXTK/Inc/Keyboard.h` exists. Option B is coverage extension, not a new-library import — the License Policy's new-library approval gate is not triggered; the grill decision rests on behavior alone.
- VK-remap claim confirmed in vendored `Src/Keyboard.cpp` (Win32 path ~`:601-617`): `VK_SHIFT`/`VK_CONTROL`/`VK_MENU` are remapped to side-specific VKs via `MapVirtualKeyW(MAPVK_VSC_TO_VK_EX)`; the generic bits are never set — the `Input.cpp:37` `VK_MENU` binding would break under Option B as stated.
- Option B item 1 nuance: WndProc handles `WM_CLOSE` by setting `sbQuit` (`Main.cpp:589-595`), so Alt+F4 under Option B still exits via the normal main-loop break — what it bypasses is the game-side `kQuit` binding (and any future confirmation gate), not clean shutdown.
