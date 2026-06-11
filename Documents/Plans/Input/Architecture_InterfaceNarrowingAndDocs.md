# Architecture: RawInputManager Interface Narrowing & Doc Gaps

## Context

Source: /external-deep-analysis on `Engine/Source/Input`. Repo-wide grep of `gpRawInputManager` shows the
external surface actually used: `UpdateFocus` (`Main.cpp:547/:565`), `HandleRawInput` (`Main.cpp:575`),
`Update` + `mRawInput` reads (game `Input.cpp:15-16/:116`), `SetVibration` (game `Camera.cpp:304`), and one
external `mHwnd` write (`Main.cpp:155`). The remaining public members have zero external users — the interface
is wider than the module's real seam (Ousterhout: deep module, needlessly wide door). Plus two undocumented
non-obvious behaviors worth CLAUDE.md lines and one vestigial game-side forward declaration.

Supersedes `Input/Architecture_InputInterfaceTightening.md` (parallel-authored duplicate of the same surface,
deleted at verification); its two unique items are folded in below.

## Design

### Engine/Source/Input/RawInputManager.h
- Move `mpbKeyboardKeysDown` (`:67`) and `mMouse` (`:68`) into the existing private section (`:72`) — zero
  external readers/writers (grep-verified: only `RawInputManager.cpp:61/:145/:149/:237` touches them; Main.cpp
  feeds DirectXTK via the *static* `Mouse::ProcessMessage` (`Main.cpp:474`), never the member). Sibling of the
  landed-pattern `File/Architecture_FileManagerEncapsulation.md` [~5m]
- Single-source `mHwnd` (`:65`): it is written externally at `Main.cpp:155` *and* shadowed by the `hwnd`
  parameter of `UpdateFocus` (`:59`), which registers with the param (`RawInputManager.cpp:43`) but never
  assigns the member — two sources for the same handle. Assign `mHwnd = hwnd` at `UpdateFocus` entry, drop the
  `Main.cpp:155` external write, then move `mHwnd` into the private section too (no external readers remain).
  Ordering safety is provable — see Notes [~10m]

### Projects/BrokenEngineSandbox/Source/Game.h
- Remove the vestigial `struct RawInput;` forward declaration (`:19`) — the only `RawInput` mention in
  `Game.h` (grep-verified); the real consumer (`Input.h:84` by-value member) includes `RawInputManager.h`
  directly (`Input.h:3`) [~2m]

### Engine/Source/Input/CLAUDE.md
- Add a "Non-obvious Behaviors" bullet: keyboard registration with `RIDEV_NOLEGACY`
  (`RawInputManager.cpp:47`) suppresses WM_KEY*/WM_CHAR while focused, so `ImGui_ImplWin32_WndProcHandler`
  (`Main.cpp:454`) is starved of keyboard input — ImGui text fields / keyboard nav silently won't work; only
  gamepad is manually fed to ImGui (game `Input.cpp:88-108`). Accepted today (no ImGui text fields exist);
  document so a future text field doesn't fail mysteriously [~5m]
- Document the load-bearing construction order: `RawInputManager` is constructed (`Main.cpp:102`) *before*
  `CreateWindow` (`Main.cpp:150`) because its by-value `Mouse mMouse` member backs DirectXTK's internal
  singleton used by the static `Mouse::ProcessMessage` calls in WndProc (`Main.cpp:474`), and window creation
  dispatches messages synchronously — an invisible dependency between two files with no code reference
  connecting them [~5m]

## Critical files

- `Engine/Source/Input/RawInputManager.h`
- `Engine/Source/Input/RawInputManager.cpp` (`UpdateFocus` gains the `mHwnd` assignment)
- `Engine/Source/Main.cpp` (`:155` external write removal)
- `Projects/BrokenEngineSandbox/Source/Game.h` (`:19` fwd decl)
- `Engine/Source/Input/CLAUDE.md`

## Out of scope

- `mRawInput` stays a public member — direct public-member reads via `gp*` globals are the house manager
  pattern (e.g., `gpGraphics->mFramebufferExtent2D`); a const accessor would deviate for no functional gain.
- `gpRawInputManager` ctor/dtor publish guards — owned by `Engine/SingletonPublishGlobalGuardSweep.md`.
- The `RawInputManager.h` server-vcxproj `ClInclude` listing — appended to
  `Engine/VcxprojAndBuildConfigReconciliation.md` (this run).
- `HandleRawInput` mechanics and `WM_INPUT`/`DefWindowProc` routing — `Input/Refactor_RawInputHandling.md`.
- Bool-array → `common::Flags` conversion of `RawInput` — `Input/Refactor_InputButtonFlags.md`.
- Any behavior change to input handling — this is visibility/doc only (the `mHwnd` consolidation preserves
  the effective assignment timing; see Notes).

## Notes

- No determinism/CRC exposure; the edited engine code paths are client-only.
- The `mHwnd` single-sourcing is provably safe (verified against source this run): `TrapCursor(true)`
  (`RawInputManager.cpp:100-124`, the only `mHwnd` reader) is reachable solely from `Update`'s focused branch
  (`:134`), gated on `bLostFocus == false`; `bLostFocus` is `ProcessMessages()`'s `return !sbHasFocus`
  (`Main.cpp:441`); and `sbHasFocus` becomes true only inside the `WM_SETFOCUS` handler (`Main.cpp:538-540`),
  which synchronously calls `UpdateFocus(true, sHwnd)` (`:547`). So every focused `Update` is preceded by an
  `UpdateFocus(true, hwnd)` and `mHwnd` is assigned before any read. Boot path: window created hidden
  (`:150`), shown + `SetFocus` after boot renders (`:230/:251`) with `ProcessMessages` (`:252`) before the
  main loop; `WM_SETFOCUS` cannot fire during `CreateWindow` (its handler derefs `game::gpGame` at `:543`,
  constructed only at `:193`). The drop-the-parameter alternative was considered and rejected at verification
  (it would leave the raw cross-manager member write in place for no gain).

## Verification Notes

- Verification pass (this run) checked every citation against source: external-use list confirmed complete via
  repo-wide `gpRawInputManager` grep (`UpdateFocus` `Main.cpp:547/:565`, `HandleRawInput` `:575`, game
  `Input.cpp:15-16/:116`, game `Camera.cpp:304`, plus the `Main.cpp:155` `mHwnd` write through the local
  `pRawInputManager` pointer); `mpbKeyboardKeysDown`/`mMouse` have zero external uses; `Main.cpp:474` uses the
  static `Mouse::ProcessMessage`; `UpdateFocus` registers with its `hwnd` param (`RawInputManager.cpp:43`) and
  never assigns `mHwnd`; `RIDEV_NOLEGACY` at `RawInputManager.cpp:47`; ImGui gamepad-only feed at game
  `Input.cpp:88-108`.
- The recommended `mHwnd` option's focus-before-first-read ordering was the critical check — proven from the
  `sbHasFocus`/`bLostFocus` invariant chain (full derivation in Notes), so the original "confirm against the
  actual message trace, else fall back" hedge was replaced with the proof.
- Rewritten to absorb the two unique items (Game.h `:19` fwd-decl removal — re-verified vestigial; the
  ctor-before-CreateWindow doc bullet — re-verified at `Main.cpp:102/:150/:474`) from the parallel-authored
  duplicate `Architecture_InputInterfaceTightening.md`, which was deleted; also folded in its
  `mHwnd`-goes-private completion. No items dropped as invalid.
