# LogTypes Single-Source-of-Truth Category Count Guard

## Context

`Common/Log/LogTypes.h` defines the `LogLevel` and `LogCategory` enums plus the global call-site aliases used by the `LOG` macro. The category count is a hand-written literal that no compiler check ties to the enum it counts:

```cpp
// Common/Log/LogTypes.h:17-29
enum class LogCategory : int8_t
{
	kDefault  = 0,
	kTemp     = 1,

	kAudio    = 2,
	kGraphics = 3,
	kLoading  = 4,
	kNavData  = 5,
	kNetwork  = 6,
	kInput    = 7,
};
inline constexpr int64_t kiLogCategoryCount = 8;
```

`kiLogCategoryCount` is the load-bearing size for the per-category ring-buffer array (`gLogRingBuffers[kiLogCategoryCount]` in `Common/Log/Log.h`) and the bound for the positional name/level mirror tables (`kpcLogCategoryNames[]`, `keLogLevels[]` in `Log.h`, fed by each project's `keLogLevel*` block in its `Pch.h`). Adding a category requires editing the enum, the literal `8`, the matching alias, and the downstream tables in lockstep. If the count is not bumped when a category is appended, `LogCategory::kInput`-style indexing and the ring-buffer write path silently run out of bounds (UB) with no diagnostic. This is a maintainability/correctness guard gap — make the lists unable to silently drift.

The global-namespace visibility of the level/category aliases (`kError`, `kInfo`, `kAudio`, ...) and the two `using common::Log*;` declarations are intentional: the `LOG` macro is designed to take unqualified enumerators, and unqualified resolution is by design per `Common/CLAUDE.md`. The line-40 `kChunkAudio` comment documents one already-resolved rename, not a live collision. No ODR hazard — leave the alias design as-is.

## Design

- **Derive the count from the enum** — `Common/Log/LogTypes.h:17-29` — effort 1
  - Append a non-counted sentinel `kCount` as the last `LogCategory` enumerator (immediately after `kInput = 7`), then replace the literal with `inline constexpr int64_t kiLogCategoryCount = static_cast<int64_t>(LogCategory::kCount);`. The value stays `8` but is now enum-derived, so appending a category before `kCount` bumps it automatically. No `kCount` alias is added (the sentinel is index-only). Matches the index/count enum idiom in the style guide.
- **Document + assert the contiguity contract** — `Common/Log/LogTypes.h:17` — effort 1
  - Add a one-line comment on `LogCategory` stating values are dense `0..kCount-1`, used directly as array indices, append-only immediately before `kCount`. (The blank line between `kTemp` and `kAudio` is purely visual grouping; the comment removes any "gap is allowed" misread.)
- **Static-assert each downstream table length** — `Common/Log/Log.h` (at the `kpcLogCategoryNames[]`, `keLogLevels[]`, and `gLogRingBuffers[...]` definitions) — effort 2
  - Co-locate `static_assert(std::size(<table>) == common::kiLogCategoryCount, "...desync with LogCategory");` with each positional table so any future enum/table drift is a build break, not a silent OOB. Verify exact identifiers and definition sites in `Log.h` before editing (the analyzed report's `Log.h` line numbers were not re-confirmed against source in this pass; treat them as approximate and re-locate by symbol).

## Critical files

- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Log\LogTypes.h` — enum, sentinel, derived count, contract comment.
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Log\Log.h` — positional tables (`kpcLogCategoryNames[]`, `keLogLevels[]`) and `gLogRingBuffers[]`; add `static_assert`s. Confirm symbol names/sites against source.
- `C:\Users\dougt\Documents\BrokenEnginePublic\Projects\BrokenEngineSandbox\Source\Pch.h` and `C:\Users\dougt\Documents\BrokenEnginePublic\DataPacker\Source\Pch.h` — each defines the per-project `keLogLevel*` block feeding `keLogLevels[]`; no edit needed unless adding a category, but they are part of the synchronized set the `static_assert` now guards.

## Out of scope

- H1 — moving the level/category aliases out of the global namespace or wrapping them behind the `LOG` macro. The unqualified-enumerator design is intentional (`Common/CLAUDE.md`); no real ODR collision exists. DROPPED.
- M3 — adding `#include <cstdint>` for `int8_t`. The project centralizes standard headers in `Common/ExternalHeaders.h`; a per-file include violates that convention and the header already compiles via the aggregation path. DROPPED.
- L1 — relocating `LogColor` into `LogTypes.h` (cosmetic cohesion). DROPPED.
- L2 — documenting the per-project `keLogLevel*` obligation (documentation-only). DROPPED.
- L3 — enum blank-line group comments beyond the single contiguity comment above (cosmetic). DROPPED.
- L4 — replacing the alias blocks with `using enum` (style-only; re-introduces the intentional global visibility). DROPPED.
- Renaming or reordering any existing `LogCategory`/`LogLevel` enumerator — values are stable by contract.

## Acceptance criteria

- `kiLogCategoryCount` is derived from `LogCategory::kCount`, not a hand-written literal.
- Appending a category before `kCount` updates the count and every guarded table size automatically; forgetting to update a positional table (`kpcLogCategoryNames[]`, `keLogLevels[]`) or the ring-buffer array is a compile error via `static_assert`, not a silent OOB.
- A contract comment on `LogCategory` states the dense-index / append-before-`kCount` rule.
- DataPacker and BrokenEngineSandbox (client + server) still build with no new warnings; no behavioral change (count remains 8, enum ordinals unchanged).

## Notes

- The sentinel `kCount` is index-only — do not add a `kCount` call-site alias and do not give it an entry in `kpcLogCategoryNames[]` / `keLogLevels[]`; the tables stay length `kiLogCategoryCount`.
- `kTemp` (category 1) is reserved for transient agent diagnostics (`Common/CLAUDE.md`) — keep it in the dense run; do not special-case it.
- Re-confirm the exact `Log.h` definition sites/identifiers against source before adding the `static_assert`s; the source could not be re-read during analysis (tool-output outage), and the original report's `Log.h:69/133-137` line numbers are unverified.
