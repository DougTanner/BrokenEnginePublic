# StringUtils: Make `ToLower` ASCII-deterministic for the DataPacker chunk-ordering key

## Context

`common::ToLower` (`Common/StringUtils.cpp:17-22`) lowercases via `std::tolower`, which consults the
global C locale. It is used as the comparison key for the deterministic DataPacker export-job sort:

- `DataPacker/Source/Main.cpp:383` — `std::sort(... return common::ToLower(rpA->mRelativeFile) < common::ToLower(rpB->mRelativeFile); )`.
  The comment states the intent: "Sort by relative path to ensure chunks are in same order inside the
  file (for more efficient Steam patching)." `DataPacker/Source/CLAUDE.md` reinforces this:
  "Deterministic output: Assets sorted by relative path before processing for consistent chunk ordering."

This sort determines `.pack` chunk layout and the emission order of the generated CRC constants
(`Main.cpp:333`). A non-deterministic key therefore changes baked output ordering between machines /
runs — a reproducibility hazard for the offline bake.

`std::tolower(int)` is locale-dependent. First-party code never calls `setlocale`/`std::locale`/`imbue`
(verified across `Common/`, `DataPacker/`, `Engine/`), so today the process runs under the default "C"
locale where `std::tolower` is pure ASCII — meaning the current output is deterministic in practice.
The risk is latent: any future global-locale change, or a build/runtime environment that perturbs the
C locale before the sort, would silently reorder chunks. Replacing the locale-sensitive `std::tolower`
with a self-contained ASCII fold removes the global-state dependency from a determinism-critical key at
zero behavioral cost (asset filenames are ASCII).

The other two `ToLower` callers (`DataPacker/Source/Attribution.cpp:58,82`) compare lowercased
filenames against ASCII literals (`"license"`, `"copying"`, `"readme"`, etc.); ASCII-only folding is
already the correct and intended behavior there.

## Design

- **`common::ToLower`** — `Common/StringUtils.cpp:17-22` — [effort:1]
  Replace the `std::tolower`-based lambda with a self-contained ASCII lowercasing predicate (fold only
  `'A'..'Z'`, leave all other bytes unchanged). This makes the function locale-independent and removes
  the dependency on global C-locale state. Keep the existing signature, in-place `std::transform`, and
  by-value `std::string` return unchanged. No call sites change.

  This also incidentally removes the file's reliance on `std::tolower` from `<cctype>` (which is not
  aggregated in `Common/ExternalHeaders.h` and arrives only transitively today).

## Critical files

- `Common/StringUtils.cpp` — the `ToLower` implementation (the only file edited).
- `DataPacker/Source/Main.cpp:383` — consumer whose determinism this protects (read-only; no edit).
- `DataPacker/Source/Attribution.cpp:58,82` — other consumers (read-only; ASCII compares, behavior preserved).

## Out of scope

- **`ToHex` "silent truncation" (report H1)** — DROPPED as non-bug. `ToHex` deduces `T` from the
  argument and sizes its buffer via `static_assert(N >= 2 + sizeof(T)*2 + 1)`; the loop emits exactly
  `sizeof(T)*2` nibbles for that same `T`, so a value wider than `T` cannot reach the loop. All callers
  (`Common/ErrorUtils.cpp:21`, `Common/Determinism.cpp:121`, `Engine/Source/Audio/AudioManager.cpp:127-129`,
  `Engine/.../ServerReceive.cpp:152`, `ClientSend.cpp:78`, and the four `Projects/.../Network/...` reconcile
  sites) pass the value at its natural width. The hazard is purely hypothetical pre-narrowing at a
  call boundary; no such caller exists. Adding an `ASSERT(uiValue == 0)` is speculative robustness (YAGNI).
- **`Split` signed/unsigned `int64_t` vs `size_type` (report M1)** — type smell only; works correctly
  via two's-complement round-trip. Single first-party caller is a diagnostic Vulkan-message path
  (`Engine/.../InstanceManager.cpp:64`). Not a bug; "don't touch unrelated code."
- **`Split` `reserve` / redundant `token` copy (report M2)** — perf on offline/diagnostic code with one
  caller; not a clear, valuable win.
- **`Split` empty-input / trailing-delimiter / empty-delimiter contract (report M3)** — documentation
  only; current caller relies on existing behavior.
- **`<cctype>` not in `ExternalHeaders.h` (report M4)** — missing-include hygiene; owned by
  code-style-review. (Also rendered moot once `ToLower` no longer uses `std::tolower`.)
- **`ToLower` ASCII-only doc note (L1), `ToString` return-value check (L2), `PathToCppVariable`
  leading-digit / extra-separator handling (L3), stale comment param names (L4), Hungarian-prefix /
  loop-sign nits (L5), up-front-copy perf note (L6), "offline only — allocates" header notes** — all
  documentation / style / speculative-validation items; pruned per project conventions
  (assume valid params; KISS/YAGNI; cosmetic owned by style review).

## Acceptance criteria

- `common::ToLower` produces identical output to the prior ASCII behavior for all ASCII inputs and is
  independent of the global C locale.
- DataPacker still builds; export-job sort order and generated chunk / CRC-constant ordering are
  unchanged for the (ASCII) shipping asset paths.
- No call-site changes required.

## Notes

- Scope: SMALL (single-function, single-file, no API change).
- Behavior is byte-identical for ASCII inputs (all current callers); the change only removes the
  locale-global dependency, hardening determinism of the bake-ordering key.
- Effort 1, Impact 2, Risks 1 (latent reproducibility, no live failure today; inputs ASCII and no
  `setlocale` present). Score = 1 - 2 + 1 = 0.
