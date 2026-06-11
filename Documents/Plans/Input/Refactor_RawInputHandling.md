# Refactor: Raw Input Handling

## Context
Source: /external-refactor-clean on `Engine/Source/Input` (investigation items handed off from /external-architecture-review). Two mechanics items in the WM_INPUT path: the WndProc swallows `WM_INPUT` without the `DefWindowProc` pass the Win32 contract asks for, and `HandleRawInput` does a two-call size-query + workbuffer allocation for a packet that is fixed-size for the device classes this engine registers.

## Design

### Engine/Source/Main.cpp
- In WndProc's `case WM_INPUT:` (lines 572-578), replace `return 0;` with `break;` after `gpRawInputManager->HandleRawInput(lParam)` so the message falls through to the function's trailing `DefWindowProc` return — MSDN (WM_INPUT): "the application must call DefWindowProc so the system can perform cleanup". Fall-through verified: the second switch ends at `Main.cpp:609` and the function's final statement is `return DefWindowProc(...)` at `:611`. The earlier `Mouse::ProcessMessage` routing for `WM_INPUT` (case label at line 462, call at 474) is unaffected. [~5m]

### Engine/Source/Input/RawInputManager.cpp
- Simplify `RawInputManager::HandleRawInput` (lines 217-240): replace the size-query `GetRawInputData(..., nullptr, &uiRawInputBytes, ...)` (line 222) + `gpThreadLocal->mWorkbuffer.PushBuffer<RAWINPUT*>` (line 224) + second fetch with a single `GetRawInputData` into a stack `RAWINPUT` buffer. Only usage-page 0x01 usages 0x02/0x06 (mouse/keyboard) are ever registered (`UpdateFocus`, lines 37-48) — both decode into the fixed-size `RAWMOUSE`/`RAWKEYBOARD` union members, never variable-length `RAWHID` — so `sizeof(RAWINPUT)` bounds the packet. Keep the returned-size validation + `DEBUG_BREAK()` (lines 225-230): `GetRawInputData` is an OS API, a trust boundary. [~15m]

## Critical files
- `Engine/Source/Main.cpp`
- `Engine/Source/Input/RawInputManager.cpp`

## Out of scope
- Removing the vestigial mouse `RIDEV_INPUTSINK` registration — `Engine/DeadCodeAndUnusedIncludesSweep.md` owns it (its "behavior-verify" caveat applies there). The stack-buffer item here is valid whether or not that lands: mouse raw packets are also fixed-size.
- The incorrect comment at `RawInputManager.cpp:42` — `Common/StaleCodeCommentsSweep.md`.
- Left/right modifier (`VK_LSHIFT`/`VK_RSHIFT`) disambiguation in the keyboard decode (line 234) — no current binding distinguishes sides (game bindings read generic `VK_MENU`, `Input.cpp:37`); YAGNI until one does.

## Notes
- No determinism/CRC/network exposure — client-only display-rate input path.
- Co-schedule with `Engine/DeadCodeAndUnusedIncludesSweep.md` (mouse-registration item edits the same `UpdateFocus` region) and `Common/StaleCodeCommentsSweep.md` (comment in the same block) to avoid stale line numbers.

## Verification Notes
- Registration verified: only usage page 0x01, usages 0x02 (mouse, `RIDEV_INPUTSINK`) and 0x06 (keyboard, `RIDEV_NOLEGACY`) are ever registered (`UpdateFocus`, cpp:37-48); no RAWHID source, so `sizeof(RAWINPUT)` bounds every packet — claim holds.
- Execution note: with a fixed-size buffer, `GetRawInputData` returns `(UINT)-1` when the buffer is too small — the kept trust-boundary validation must check the return value, not just size equality.
- The `return 0;` at `Main.cpp:577` sits outside the `BT_CLIENT` guard, so the `break` change also affects server builds — harmless (server registers no raw-input devices; `WM_INPUT` simply reaches `DefWindowProc`).
