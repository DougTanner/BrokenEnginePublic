# MenuUtils AppendUtf8 Returns A Dangling Workbuffer Pointer

## Context

The `game::AppendUtf8` helper (`Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.cpp:6-37`, declared at `MenuUtils.h:28`) converts a UTF-32 string to a null-terminated UTF-8 string and hands the caller a `const char*` for ImGui. It opens its **own** local `common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();` (`MenuUtils.cpp:8`), `PushBack<char>`s the encoded bytes plus a trailing `'\0'`, then returns `rWorkbuffer.View().data()` (`MenuUtils.cpp:35-36`).

The bug: the local arena's destructor runs `Pop()` at function return, so the returned pointer points into a popped (free-to-reuse) workbuffer region — a use-after-pop / dangling pointer. The frame that produced the bytes is closed before the caller ever dereferences the pointer.

It works today only by luck. Every caller consumes the pointer inline in the same full-expression — e.g. `ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringQuit)))` and `ImGui::CalcTextSize(AppendUtf8(...)).x` — before any other workbuffer `Push`/`Append`/`PushBack` on the thread overwrites the bytes. There are ~12 call sites, all client-only UI (`#ifdef BT_CLIENT`):
- `PauseMenuScreen::Render` — `PauseMenuScreen.cpp:34-38` (the five-call max-button-width loop), `:41`, `:46`, `:51`, `:56`, `:62`.
- `MainMenuScreen::Render` — `MainMenuScreen.cpp:93`, `:107`, `:111`, `:118`, `:124`.
- `SoundMenuScreen::Render` — `SoundMenuScreen.cpp:38`.

The header comment at `MenuUtils.h:27` ("Push first, returns const char*") is ambiguous/misleading about the lifetime contract — it reads as if the caller is responsible for the Push, but the function Pushes internally and the result must not outlive the caller's full-expression.

This is **not** introduced by the recent Workbuffer hardening change (which added `ASSERT(miDepth > 0)` to `View`/`Span`, `Workbuffer.h:62,69`). That assert passes here because the frame is still open at the moment of the `View()` call inside `AppendUtf8`. The dangling-pointer hazard is pre-existing and independent.

The `PauseMenuScreen` max-button-width loop (`PauseMenuScreen.cpp:34-38`) is the constraint that shapes the fix: it calls `AppendUtf8` five times and reads each result independently to compute a per-string width. Each call must yield its **own** isolated string — they must not concatenate into one growing frame.

## Design

Recommended: change `AppendUtf8` so the workbuffer frame it opens stays alive in the caller through the inline ImGui consume, instead of being popped at function return. The clean way is to keep the per-call string isolation the multi-button screens rely on while moving frame ownership out to the caller's full-expression (a temporary lives to the end of the full-expression that contains the call — exactly covering the inline ImGui read).

Two shapes to weigh during the grill; both are mechanical, compile-checked, and client-only:

- **Option A — return the arena handle.** Return `common::ScopedWorkbufferArena` by value; the caller reads `.View().data()` for the `const char*`. Blocker to resolve first: `common::ScopedWorkbufferArena`'s copy **and** move are currently `= delete`d (`Workbuffer.h:147-148`), so it cannot be returned by value as-is. This option therefore requires either (a) giving the arena a move constructor that transfers ownership and makes the source inert (mirroring `ScopedWorkbufferAllocation`'s move at `Workbuffer.h:182-187`), which is a `Common/` core change and is **out of scope** per the boundary below, or (b) a different handle type. Prefer Option B unless the grill decides the move-ctor addition is wanted as a separate, sequenced Common change.

- **Option B — return a tiny game-side RAII wrapper struct (recommended).** A small `struct` local to the Ui/Screens layer that holds the `ScopedWorkbufferArena` by value as a member (member construction is direct, not a copy/move, so the deleted copy/move on the arena is not an obstacle), exposes the bytes via an implicit `operator const char*` (or an explicit `.Cstr()` / `.View().data()`), and is itself returned by value from `AppendUtf8`. Its returned temporary keeps the frame open until the end of the caller's full-expression. Decide in the grill whether an implicit `operator const char*` reads better at the ~12 ImGui call sites (callers stay byte-identical: `ImGui::Button(AppendUtf8(...))`) versus an explicit accessor (more visible lifetime, costs a `.Cstr()` at each site). The wrapper must be movable (or guaranteed-elided via the return) so the by-value return compiles; if it holds the arena by value and relies on mandatory copy-elision for the `return`, no user-declared move is needed.

Either way the change is a single signature/return-type edit on `AppendUtf8` plus the matching call-site read style, all under `#ifdef BT_CLIENT`. The five `PauseMenuScreen` width-loop calls keep producing five independent strings because each `AppendUtf8` call still opens its own frame — the only change is that the frame now closes in the caller rather than inside the helper.

Reject the "don't Push internally; require the caller to own one frame and just `Append` into it" variant: multiple `AppendUtf8` calls within a single caller frame would concatenate (each `View()` returns the whole open frame), which breaks the `PauseMenuScreen` per-string width loop. Per-call frame isolation must be preserved.

Also update the misleading `MenuUtils.h:27` header comment to state the real lifetime contract: the returned handle owns a workbuffer frame and the `const char*` it yields is valid only for the lifetime of that handle (i.e. only within the caller's full-expression when used inline).

The grill must settle: (1) Option A vs Option B (and, if A, whether to add the arena move-ctor as a sequenced Common change); (2) implicit `operator const char*` vs explicit accessor; (3) exact wording of the corrected header comment.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.cpp` — the `AppendUtf8` definition (`:6-37`); the local `Push()` at `:8` and the `return ...View().data()` at `:35-36` are the bug. The sibling helpers `WrapperToggle`/`WrapperSlider`/`WrapperPlusMinus` (`:39-82`) are untouched.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.h` — the `AppendUtf8` declaration (`:28`) and the misleading lifetime comment (`:27`).
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/PauseMenuScreen.cpp` — `PauseMenuScreen::Render`, call sites `:34-38,41,46,51,56,62`; the `:34-38` max-width loop is the per-call-isolation constraint.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MainMenuScreen.cpp` — `MainMenuScreen::Render`, call sites `:93,107,111,118,124`.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/SoundMenuScreen.cpp` — `SoundMenuScreen::Render`, call site `:38`.
- `Common/Workbuffer.h` — reference only. `Workbuffer::Push`/`View`/`Pop` semantics, the `ScopedWorkbufferArena` deleted copy/move (`:147-148`), and the `ScopedWorkbufferAllocation` move-ctor precedent (`:182-187`). Do not edit unless the grill explicitly chooses Option A's move-ctor route (then sequence it as a Common change).

## Out of scope

- The sibling `MenuUtils` helpers `WrapperToggle`, `WrapperSlider`, `WrapperPlusMinus`, `ScopedMenuScale`, and `kfMenuUiScale` — none have the dangling-pointer problem and none are touched.
- The Localization 256-char-per-string cap (`Ui/CLAUDE.md`, `Localization.h`) and `TranslatedString()` fallback behavior — unrelated.
- Any change to the `Common/Workbuffer` core (`Workbuffer.h`/`.cpp`), including adding a `ScopedWorkbufferArena` move constructor, **unless** the grill explicitly selects Option A's move-ctor route; if so, that is a separately-sequenced Common change, not folded into this game-layer fix.
- Adding validation or error handling (null checks, length guards, defensive clamps) — repo policy forbids it; assume inputs are valid.
- Server builds — `AppendUtf8` and all call sites are client-only (`#ifdef BT_CLIENT`); no server vcxproj/file-inclusion change.
- Any non-UTF-8 / non-menu workbuffer consumer elsewhere in the codebase.

## Acceptance criteria

- `AppendUtf8` no longer returns a pointer into a popped workbuffer frame; the frame backing the returned bytes stays open until the caller's full-expression completes.
- The ~12 call sites still compile and produce identical on-screen text; the `PauseMenuScreen` five-call max-width loop still computes five independent per-string widths (no concatenation).
- The `MenuUtils.h:28` declaration comment accurately states the handle/lifetime contract.
- Client builds clean; no new allocation-tracker break (the fix stays within the existing workbuffer, no heap allocation added).

## Notes

- The fix is latent-correctness, not a live-visible bug: it works today purely because every caller consumes the result inline before the thread touches the workbuffer again. Any future refactor that inserts a workbuffer `Push`/`Append` between an `AppendUtf8` call and its ImGui consume would silently corrupt menu text — that is the failure mode this closes.
- `Common/Workbuffer.h` already demonstrates the by-value-return RAII pattern with `ScopedWorkbufferAllocation<T>` (`PushBuffer` returns it by value with a move ctor at `:182-187` and `operator T()` at `:200`). Option B mirrors that ergonomics at the game layer without changing the Common core.
- Compile-checked: a return-type/signature change forces every call site to be visited by the compiler, so a missed site cannot silently survive.
