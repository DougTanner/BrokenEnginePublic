# Refactor: Engine Root Files Quick Wins (Main/GameBase/CrashReport/Input)

## Context
Source: /external-refactor-clean on Engine/Source (recursive). Mechanical and small-behavior items in the engine root files plus RawInput. Two need verification/decision before edit (the GameBase defensive loops, the crash-path allocation).

## Design

### Engine/Source/Main.cpp
- Drop the dead `WM_QUIT` case from `WndProc` (:587) — WM_QUIT is a thread message never routed to a window proc; quit flows through `WM_CLOSE`/`WM_DESTROY` + `sbQuit` [~5m]
- Hoist the fullscreen-toggle reconciliation (:416-426) out of `ProcessMessages()` into the main loop (~:263) — every caller silently performs window restyling, including the teardown lambda (:158-168) where a mid-shutdown `gFullscreen` mismatch would `SetWindowPos` a dying window (harmless today only by accident) [~15m]
- Remove `WM_INPUT` from the DirectXTK `Mouse::ProcessMessage` forwarding (:459, :471) — only the keyboard is raw-input-registered; every packet handed to Mouse is keyboard data it ignores; implies a raw-mouse path that doesn't exist [~5m]
- `MainThread` (~290 lines, :39-329): **accept** — strictly linear boot orchestration with load-bearing, centrally documented ordering; recorded so future sweeps don't re-file

### Engine/Source/GameBase.{h,cpp}
- Extract `AdvanceRenderClock(...)` from `Render()` (:266-420) — the render-clock advance (:297-347: seed/pause/cold-start/rebase/integrate-clamp) is a self-contained state machine over `mfRenderTime`/`mbRenderClockSeeded` returning one value; `Render` drops to ~100 lines and the client-render-clock invariants get a named home. Comments move with it [~30m]
- Delete (after verification) the two defensive log loops: `BuildAndDispatchFrameTicks` (:172-177) skip-null-with-kWarning — `PrepareActiveSet`→`EnsureNextFrames` runs immediately before every call; **verify first-tick `pCurrent` seeding for a brand-new coord before deleting**; and the `FinalizeFrameTick` post-swap verification loop (:221-231) — re-checks the function called four lines earlier, no producer of the failure state found; runs per server tick [~15m + verification]
- Drive the visual-error decay from `mfLastRenderFrameSeconds` instead of fixed `1/miMonitorRefreshRate` (:399-408) — `GameBase.h:206-212` documents exactly why display-rate consumers must use the wall delta (vsync-miss relative stutter), and the game camera already does (`Camera.cpp:64`). One line; visual-only [~5m]

### Engine/Source/CrashReport.cpp
- Delete `ReadDxDiag`'s internal `IsDebuggerPresent()` re-check (:83-86) — the single call site (`Main.cpp:94-97`) already gates on it [~5m]
- Fix the crash-path allocation drift in `HandleException`: the :16 comment claims fixed-buffer/no-heap, but `std::filesystem::create_directories` (:44) heap-allocates before the report is written — replace with allocator-free `CreateDirectoryW(spcPath, nullptr)` (the ofstream past that point only risks losing the report body, not re-faulting) [~15m]
- Make the per-prop COM result discipline in `ReadDxDiag` consistent (:120 literal 256 vs :130 `std::size(...) - 1`; bare `GetNumberOfProps`/`EnumPropNames`/`GetProp` at :125,:130,:133 in a CHECK_HRESULT file) — CHECK them (they're in the try) or add a one-line "best-effort per-prop reads" comment [~5m]

### Engine/Source/Input/RawInputManager.cpp
- Dedupe the register/unregister `RAWINPUTDEVICE` setup in `UpdateFocus` (:43-83) — identical struct differing only in `dwFlags` (RIDEV_NOLEGACY vs RIDEV_REMOVE); hoist the struct or a small lambda [~5m]

## Critical files
- `Engine/Source/Main.cpp`, `GameBase.{h,cpp}`, `CrashReport.cpp`, `Input/RawInputManager.cpp`

## Out of scope
- `GameBase` dead virtuals / server MenuInput plumbing (`Engine/Architecture_GameBaseDeadVirtuals.md` — same files, co-schedule)
- `CoordFrames::uiGeneration` (live `Network/DeadMachinerySweep.md`)
- Audio/Profile findings (`Audio/Architecture_AudioVoiceSeams.md`, `Profile/Architecture_ProfileVirtualRouting.md`)

## Notes
- Invariant exposure: LOW — no CRC/wire/save change. The `AdvanceRenderClock` extraction is client-render-clock code motion (must preserve exact float ops — render-only, but the CoordFrames-adjacent invariants are documented in Engine/Source/CLAUDE.md); the decay fix is deliberately behavior-changing (visual-only, matches documented intent). The GameBase loop deletions are gated on the first-tick verification — if `pCurrent` can be null on a coord's first tick, keep that guard and delete only the post-swap loop
- Grill decisions: (a) GameBase loops — confirm first-tick seeding; (b) `CreateDirectoryW` swap vs comment-scope-down in `HandleException` (recommend the swap)
