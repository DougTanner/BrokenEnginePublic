# ErrorUtils Macro Correctness

## Context

Authored directly from source: `Common/ErrorUtils.h` and `Common/ErrorUtils.cpp`. An earlier
automated analysis report for this file was a hallucination — it described functions such as
`FormatHresult`, `GetLastErrorString`, and `MessageBoxA` that do not exist anywhere in the source.
This plan supersedes that report and is grounded only in the actual code.

`ErrorUtils.h` defines four function-like macros — `DEBUG_BREAK`, `ASSERT`, `CHECK_HRESULT`,
`VERIFY_SUCCESS` — that wrap the out-of-line helpers `common::Assert(bool, std::string_view,
std::source_location)` and `common::CheckHresult(HRESULT, std::string_view, std::source_location)`
defined in `ErrorUtils.cpp`. `ASSERT` is the codebase-wide assertion primitive (standard `assert`
is `#define`d to a compile error per `Common/CLAUDE.md`), so any macro-correctness defect here has
broad reach across Common, Engine, and Projects.

Source as it stands (`ErrorUtils.h:11-14`):

```
#define DEBUG_BREAK() do { if constexpr (kbDebugBreak) { if (IsDebuggerPresent() == TRUE) { __debugbreak(); } } } while (false)
#define ASSERT(a) do { if (!(a)) [[unlikely]] { DEBUG_BREAK(); common::Assert(a, #a); } } while (false);
#define CHECK_HRESULT(a) do { HRESULT hresultMacro = a; if (hresultMacro < 0) [[unlikely]] { DEBUG_BREAK(); common::CheckHresult(hresultMacro, #a); } } while (false);
#define VERIFY_SUCCESS(a) do { bool bReturnMacro = a; if (!bReturnMacro) [[unlikely]] { DEBUG_BREAK(); common::Assert(bReturnMacro, #a); } } while (false);
```

And the helpers (`ErrorUtils.cpp:6-25`) each `LOG`, then `DEBUG_BREAK()`, then `throw`.

## Design

Three genuine macro-correctness defects. All fixes are mechanical and confined to the single header
`ErrorUtils.h` (with one symmetric tweak to `ErrorUtils.cpp` for fix 3).

### 1. `ASSERT` double-evaluates its argument — High

The `ASSERT` macro in `ErrorUtils.h` (`ErrorUtils.h:12`) evaluates its argument `a` twice on the
failure path: once in `if (!(a))` and again in `common::Assert(a, #a)`. Any side-effecting
expression passed to `ASSERT` runs twice when the assertion fails — e.g.
`ASSERT(ptr = Acquire())`, `ASSERT(--remaining >= 0)`, `ASSERT(Pop(queue))`. The sibling macros
`CHECK_HRESULT` (`ErrorUtils.h:13`) and `VERIFY_SUCCESS` (`ErrorUtils.h:14`) already avoid this by
capturing the expression once into a local (`HRESULT hresultMacro = a;` / `bool bReturnMacro = a;`).
`ASSERT` is the lone outlier. Fix: capture `a` into a local `bool` once, test the local, and pass
the local to `common::Assert` (mirroring `VERIFY_SUCCESS`, which is already the correct shape) while
keeping `#a` as the diagnostic text. Effort: trivial.

### 2. Trailing `;` inside `do { ... } while (false);` breaks brace-less `if`/`else` — Medium

`ASSERT`, `CHECK_HRESULT`, and `VERIFY_SUCCESS` each terminate the `do { ... } while (false)` body
with a trailing semicolon (`ErrorUtils.h:12`, `:13`, `:14` — note `} while (false);`). The canonical
swallow-the-semicolon idiom requires `while (false)` with **no** trailing semicolon so the
call-site `;` terminates the statement. As written, each macro expands to a compound statement
*plus* a stray empty statement, breaking brace-less control flow:

```
if (cond) ASSERT(x); else DoOther();   // 'else' binds to the empty statement -> compile error
```

A repository grep found no current call site of the form `ASSERT(...); else` /
`CHECK_HRESULT(...); else` / `VERIFY_SUCCESS(...); else`, so this is a latent trap rather than an
active bug — but it silently constrains how these pervasive macros may be used and is a one-character
fix per macro. (`DEBUG_BREAK` at `ErrorUtils.h:11` is already correct — no trailing `;`.) Fix:
delete the trailing `;` after `while (false)` on lines 12, 13, 14.

### 3. Double `DEBUG_BREAK()` per failure, and the macro-side break fires before the diagnostic — Medium

Every failure path breaks twice. The macro calls `DEBUG_BREAK()` (`ErrorUtils.h:12/13/14`), then
calls into `common::Assert` / `common::CheckHresult`, which **also** call `DEBUG_BREAK()`
(`ErrorUtils.cpp:11` and `:22`). Under a debugger, a single failed `ASSERT` therefore halts at two
different `__debugbreak` sites for one logical event.

Worse, the ordering is wrong: the macro-side break (`ErrorUtils.h:12`, before
`common::Assert(...)`) fires **before** the helper emits its `LOG(kError, "Assert failed: ...")`
line (`ErrorUtils.cpp:10`). So in an interactive debug session the breakpoint hits before the
diagnostic message — file/line/expression — has been printed, defeating the purpose of breaking
where the failure can be read.

Fix (pick one, all keep one well-ordered break):
- (a) Remove the `DEBUG_BREAK()` from the macros and rely solely on the helper's break, which
  already fires *after* the `LOG`. Cleanest; single break, correctly ordered, message-first. This is
  the recommended option.
- (b) Remove the `DEBUG_BREAK()` from the two helpers (`ErrorUtils.cpp:11`, `:22`) and keep it in
  the macros — but this re-introduces the break-before-LOG ordering, so (a) is preferred.

Recommend (a): drop `DEBUG_BREAK();` from the three macro bodies; the helpers retain the single,
post-LOG break. Net effect: one break per failure, after the diagnostic line is logged.

## Critical files

- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\ErrorUtils.h` — the four macros; primary edit
  site for fixes 1, 2, and 3(a).
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\ErrorUtils.cpp` — `common::Assert`
  (`:6-14`) / `common::CheckHresult` (`:16-25`); the retained, correctly-ordered `DEBUG_BREAK()`
  (`:11`, `:22`) under fix 3(a). No edit needed if 3(a) is chosen; edited only under 3(b).

## Out of scope

- **Throw-path allocations** (`std::runtime_error`, `std::format`, `HresultToString`'s `std::string`
  return at `ErrorUtils.cpp:21`): by design — exceptions are reserved for fatal paths. Not flagged.
- **`DEBUG_BREAK()` gated behind `kbDebugBreak` + `IsDebuggerPresent()`** (only fires with a
  debugger attached): by design. Not addressed.
- **Defensive validation / null checks** on the helper parameters: project directive — parameters
  are assumed valid.
- **`HresultToString` shared-static-buffer / thread-safety**: that function lives in
  `Common/WindowsUtils` (called at `ErrorUtils.cpp:21`), not ErrorUtils, and is owned by a separate
  WindowsUtils theme plan. Not included here.
- **Cosmetic / formatting** (each macro on one long line; could route through `/code-style-review`
  if desired): out of scope for this correctness plan.

## Acceptance criteria

- `ASSERT(expr)` evaluates `expr` at most once on every path (verifiable by inspecting the
  expansion).
- None of `ASSERT` / `CHECK_HRESULT` / `VERIFY_SUCCESS` leaves a stray empty statement;
  `if (c) ASSERT(x); else y();` compiles.
- A single failed assertion/HRESULT breaks at exactly one `__debugbreak` site, and that break fires
  *after* the `kError` diagnostic line is logged.
- Throw-on-failure behavior is unchanged (`ASSERT`/`VERIFY_SUCCESS` still throw
  `std::runtime_error` with the expression+location; `CHECK_HRESULT` likewise).
- Common, DataPacker, and BrokenEngineSandbox (client and server) still compile.

## Notes

- Effort 1, Impact 3 (`ASSERT` correctness touches ~all assert sites engine-wide), Risks 2
  (pervasive macro but compile-checked across every consumer). Score = 1 - 3 + 2 = 0. Tier: Quick
  Win.
- Fixes 1, 2, and 3 are independent and can all land in the same trivial edit to `ErrorUtils.h`.
- Keep the `#a` stringization in `ASSERT` so the logged diagnostic still shows the original
  expression text after capturing into a local (mirror how `VERIFY_SUCCESS` keeps `#a`).
