# Profile Name-Table Length Guards (vs enum kCount)

## Context

Surfaced by the `/next-plan` Step-6 sibling sweep while landing `Common/LogTypes.md` (the LogTypes single-source-of-truth category-count guard, which added `static_assert(std::size(table) == kiLogCategoryCount)` to the two brace-deduced logging tables). The Profile subsystem has the same *intent* — positional `{.name=...}` tables indexed by a dense `: int64_t` enum that already carries a `kXxxCount` sentinel — but is **not** guarded, and the LogTypes fix does **not** transplant directly.

Six positional brace-init tables (member arrays of `CpuCounter`/`CpuTimer`/`GpuTimer`/`BootTimer`), each indexed by its enum's value:

- `Engine/Source/Profile/ProfileManagerBase.h:255` — `mEngineCpuCounters[kEngineCpuCounterCount]` vs enum `EngineCpuCounters` (sentinel `kEngineCpuCounterCount`)
- `Engine/Source/Profile/ProfileManagerBase.h:275` — `mEngineCpuTimers[kEngineCpuTimerCount]` vs `EngineCpuTimers`
- `Engine/Source/Profile/ProfileManagerBase.h:296` — `mGpuTimers[kGpuTimerCount]` vs `GpuTimers` (`#if defined(BT_CLIENT)`)
- `Engine/Source/Profile/ProfileManagerBase.h:335` — `mBootTimers[kBootTimerCount]` vs `BootTimers`
- `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.h:70` — `mGameCpuCounters[kGameCpuCounterCount - kEngineCpuCounterCount]` vs `GameCpuCounters`
- `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.h:82` — `mGameCpuTimers[kGameCpuTimerCount - kEngineCpuTimerCount]` vs `GameCpuTimers`

**Why the LogTypes transform does not apply as-is (the crux of the decision):** the logging tables (`kpcLogCategoryNames[]`, `keLogLevels[]`) have a *deduced* extent (`[]`), so dropping an initializer changes `std::size` and the assert fires. The Profile tables have a *fixed* extent `[kXxxCount]`, so `std::size(table)` is identically `kXxxCount` by construction — a `static_assert(std::size(table) == kXxxCount)` is **tautological/useless** (the same reason the `gLogRingBuffers[kiLogCategoryCount]` assert was deliberately omitted in the LogTypes landing). And C++ non-static data members may **not** deduce an array bound from a default member initializer (`T arr[] {...}` is ill-formed as a member), so the LogTypes "drop the extent, then assert" trick is unavailable here.

The actual drift the Profile tables are exposed to: a **dropped** `{.name}` entry silently zero-fills the tail (empty `string_view` name + every later row's name shifts) with no compile error; an **extra** entry beyond the fixed extent is *already* a compile error. The failure is **display-only** (a blank/misaligned overlay row) — no determinism/CRC/correctness exposure. The existing `ProfileManagerBase.cpp:12-13` static_asserts pin only the engine→game **boundary** (first game enumerator == engine count), not any table's length.

## Design

**Decision plan (present options)** — resolve via `/external-grill-plan`, then implement the chosen option across all six sites consistently.

- **Option A — Accept + document (recommended, KISS/YAGNI).** The over-population case already fails to compile; under-population is a display-only blank row. Add a one-line append-discipline comment at each table (mirroring the LogTypes contiguity comment: "one entry per enumerator in order; a dropped entry zero-fills silently"), and note the limitation in `Engine/Source/Profile/CLAUDE.md`. No structural change, no false-safety assert.
- **Option B — True compile-time guard via name-table extraction.** Split the `.name` strings out of the member `CpuCounter`/`CpuTimer` arrays into separate namespace-scope `inline constexpr const char* kEngineCpuCounterNames[]` (deduced extent) + `static_assert(std::size(...) == kXxxCount)`, and have the structs/formatters read names by index. This *does* catch a dropped entry at compile time, but is a real refactor touching every name consumer (`FormatCpuTimersText`/`FormatCpuCountersText`/`FormatGpuScreen`/boot-log) and changes the storage layout of the timer/counter structs. Higher effort, higher churn.

Recommendation: **Option A** unless the grill surfaces a concrete history of these tables silently drifting — the guard's value is low (display-only) and Option B's churn touches the same formatters several queued Profile plans already moved.

## Critical files

- `Engine/Source/Profile/ProfileManagerBase.h` — the four engine tables + their enums (`EngineCpuCounters`/`EngineCpuTimers`/`GpuTimers`/`BootTimers`).
- `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.h` — the two game tables + `GameCpuCounters`/`GameCpuTimers`.
- `Engine/Source/Profile/CLAUDE.md` — record the chosen contract (append-discipline note for A, or the new name-table seam for B).
- (Option B only) `ProfileManagerBase.cpp` / `ProfileManager.cpp` formatters that read `.name`.

## Out of scope

- The LogTypes guard itself (landed) and the `gLogRingBuffers` no-assert decision (already settled).
- The engine→game boundary `static_asserts` at `ProfileManagerBase.cpp:12-13` — correct as-is, not touched.
- Any change to enum values, ordering, the contiguous engine/game index space, or the overlay's indented display-name hierarchy.
- Renaming the three engine-referenced game timer enumerators (`kCpuTimerFrameUpdate`/`kCpuTimerFrameInterpolate`/`kCpuTimerFramePostRender`) — `Profile/CLAUDE.md` invariant.

## Notes

- No determinism/CRC/`kiVersion`/`.pack`/replay/network exposure; client/server guard scope is only the existing `#if defined(BT_CLIENT)` around `mGpuTimers`. Not allocation-tracked (compile-time / static init).
- One open decision for the grill: Option A vs B (and, if B, whether to extract names for all six tables or only the CPU-counter/timer ones the formatters share).
- Sibling lineage: the LogTypes landing is the precedent; this plan exists because its mechanical transform is **not** portable to fixed-extent member arrays.
