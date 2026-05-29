# Timer Clock-Steadiness And Conversion Type-Safety Hardening

## Context

`Common/Timer.h` is the engine's high-resolution delta timer. Its output feeds delta-time into the fixed 32 Hz simulation timestep and render interpolation (`Documents/FloatingPointDeterminism.txt` §4; `Common/CLAUDE.md` line 21 documents it as a "high-resolution **steady-clock** timer"). Confirmed live consumers on the delta-time path: `Engine/Source/Frame/TimeStep.cpp:12-17`, `Engine/Source/GameBase.cpp:271,297`, `Engine/Source/Graphics/Graphics.cpp:180`, `Engine/Source/Audio/AudioManager.cpp:275`, plus `NetworkDiscoveryScanner`, `ServerSessionBase`, `GameSaveLoad`, `WindUniforms`, and the compile-time `kfDeltaTime` in `Projects/.../Frame/Frame.h:37`.

Two compile-time correctness gaps exist, both addressable with single-file, compile-checked edits and no behavioral change on the current MSVC toolchain:

1. **Wrong steadiness guarantee.** `Timer.h:18` asserts `std::chrono::high_resolution_clock::is_steady`, and every timestamp (`Timer.h:23, 29, 34, 47`) is taken from `std::chrono::high_resolution_clock`. The class's documented contract is *steady* (monotonic), but the C++ standard does **not** require `high_resolution_clock` to be steady — it may alias `system_clock`. On MSVC today it aliases `steady_clock`, so the assert passes and the timer is monotonic. If that alias ever changes (future toolchain, non-Windows port), `now() - mLastTimePoint` can go negative when the wall clock steps backward (NTP, manual change), feeding a negative delta into the deterministic fixed-timestep / interpolation math — a silent determinism hazard. The fix names the only standard clock *guaranteed* monotonic (`steady_clock`) and asserts the property on the clock the class actually queries.

2. **Silent integral misuse of the conversion helper.** `NanosecondsToFloatSeconds<FLOAT_TYPE>` (`Timer.h:10-14`) has no constraint on `FLOAT_TYPE`. Instantiating it with an integral type compiles and silently truncates a sub-second frame delta to `0` seconds, with no diagnostic — directly on the delta-time path. A one-line `static_assert` turns this misuse into a compile error, consistent with the file's existing `static_assert` usage and the style guide's canonical `static_assert` form (rule 31).

Source validated line-by-line against `Common/Timer.h` (51 lines); all cited symbols/lines exist. No phantom findings in the report.

## Design

- **steady_clock swap** — `Common/Timer.h:18, 23, 29, 34, 47` — [effort: trivial]
  - Replace all four uses of `std::chrono::high_resolution_clock` (ctor init `:23`, `Reset` `:29`, `GetDeltaNs` local `:34`, member type `:47`) with `std::chrono::steady_clock`.
  - Change the assert `:18` to `static_assert(std::chrono::steady_clock::is_steady);` (always true; documents intent and is future-proof).
  - `GetDeltaNs` keeps `duration_cast<std::chrono::nanoseconds>(...)` (`:35`); `steady_clock` on MSVC has nanosecond period, so the cast remains identity — no resolution or behavior change. No call-site changes (signatures unchanged).
- **floating-point type guard** — `Common/Timer.h:10-14` — [effort: trivial]
  - Add `static_assert(std::is_floating_point_v<FLOAT_TYPE>, "NanosecondsToFloatSeconds requires a floating-point type");` at the top of `NanosecondsToFloatSeconds`.
  - Leave the conversion body otherwise unchanged (the bare-`float` return and `std::ratio<1,1>` are out of scope — see below). All existing instantiations are `<float>`/`<double>`, so the assert is satisfied everywhere.

## Critical files

- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Timer.h` — the only file changed (header-only).

## Out of scope

- **H2 — returning a typed `std::chrono::duration<FLOAT_TYPE>` instead of a bare float** (report H2). DROPPED as YAGNI / API churn: the unit is already carried by the function name and the strongly-typed `nanoseconds` input; switching the return type ripples a `.count()` into all 13 real call sites for no correctness gain on a working API.
- **Removing the redundant `std::ratio<1,1>`** (report H2 sub-note): cosmetic — `code-style-review` owns.
- **M1 — documenting the thread-safety / single-owner contract** (report M1): doc-only, no bug; the timer is correct for its single-owner usage. No mutex (would defeat the zero-cost design / violate KISS).
- **M2 — splitting `GetDeltaNs(bool)` into `Elapsed()`/`Lap()`** (report M2): speculative API redesign (YAGNI) that ripples into all call sites for a functioning API.
- **L1-L5** (report Low findings: redundant `inline`, member `{}` initializer, `[[nodiscard]]`, `auto`/verbosity, comment trim): all style/cosmetic — `code-style-review` owns.

## Acceptance criteria

- `Common/Timer.h` uses `std::chrono::steady_clock` for the member type, ctor, `Reset`, and `GetDeltaNs` local.
- The class `static_assert` reads `static_assert(std::chrono::steady_clock::is_steady);`.
- `NanosecondsToFloatSeconds` has a `static_assert(std::is_floating_point_v<FLOAT_TYPE>, ...)` that rejects integral instantiations at compile time.
- `GetDeltaNs` still returns `std::chrono::nanoseconds` with identical observable behavior on MSVC.
- Common builds clean; no call-site changes required (signatures unchanged).

## Notes

- No public signature changes — `Timer()`, `Reset()`, `GetDeltaNs(bool)`, and `NanosecondsToFloatSeconds<FLOAT_TYPE>` keep their shapes, so callers are untouched.
- Touches the timing path that feeds the deterministic 32 Hz timestep; the change is compile-time-only and behavior-preserving on the current toolchain, so risk is low but non-zero (timing → Risks 1).
- Both edits align with the style guide's canonical `static_assert` example (rule 31).
