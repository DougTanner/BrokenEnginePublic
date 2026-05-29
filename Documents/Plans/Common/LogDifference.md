# LogDifference Float-Format and RAII-Guard Hardening

## Context

`Common/Log/LogDifference.h` / `.cpp` is the field-by-field client/server desync
diagnosis helper. It compares paired frame fields and, on mismatch, emits a
`LOG(kNetwork, kError, ...)` naming the field (via `FixedString` NTTP label) and
the two values, prefixed by a thread-local scoped section tag
(`gpLogDifferenceContext`).

A code-analysis report on these files raised 12 findings. Validated against the
real source (every cited symbol/line exists — no hallucinations), most dissolved
under the project's own rules:

- The report's top finding (H1: "memcmp bypasses the canonical deterministic
  `operator==`") is **not a bug**. Per `Common/CLAUDE.md` line 26 the canonical
  `operator==` for `XMFLOAT2/3/4` / `XMVECTOR` is **bitwise, not epsilon**. A
  `memcmp` of the float byte pattern is *also* bitwise-equal. The `XMFLOAT*`
  types are tightly packed (8 / 12 / 16 bytes, no trailing padding;
  `XMFLOAT4A` is `alignas(16)` size 16), so `memcmp` reads exactly the value
  lanes — no padding/garbage. For desync *diagnosis* the two comparisons are
  equivalent (both flag any bit difference, including -0.0 vs +0.0 and divergent
  NaN payloads — which is what the canonical bitwise operator does too). The
  "could drift if `operator==` is ever changed" argument is speculative. Dropped
  as a bug; the only residue is a minor consistency/DRY preference handled
  incidentally below.

Two genuine items survive and are in scope here:

1. The LOG sites pass float-bearing values (`XMFLOAT*`, and bare scalar
   `float`/`double` via the generic `else` branch) directly as `{}` args. The
   root `CLAUDE.md` rule requires each scalar float to be wrapped in
   `common::Wb(value, prec)` and each vector in `common::WbV2(vec, prec)`,
   because the default `std::format` float path heap-allocates and trips the
   main-loop allocation tracker. This code only runs during desync diagnosis
   (logging branch is `[[unlikely]]`), so the practical risk is bounded — but a
   scalar-float field diff still routes through the allocating default formatter
   and would `DEBUG_BREAK()` the first time it fires while the tracker is armed.

2. `ScopedLogDifferenceContext` is an RAII restore-on-destruction guard but is
   implicitly copyable/movable. A copy yields two dtors that each write
   `gpLogDifferenceContext = pcPrevious`, the second restoring a stale value.
   Every other RAII scope guard in the codebase (`ScopedLambda`, Workbuffer
   arenas) is non-copyable. This is a small hardening matching an established
   invariant.

## Design

### 1. Route float-bearing LOG args through the sanctioned wrappers
**Files:** `Common/Log/LogDifference.h:31`, `Common/Log/LogDifference.h:53`;
`Common/Log/LogDifference.cpp:14`, `Common/Log/LogDifference.cpp:27`
**Effort: 2**

- Header templates (`LogDifference<NAME, T>`): the value args are generic `T`.
  Gate the wrapping with `if constexpr`:
  - `float` / `double` → emit `common::Wb(rOne)` / `common::Wb(rTwo)`.
  - `XMFLOAT2/3/4/4A` → load to `XMVECTOR` (`XMLoadFloat*`) and emit
    `common::WbV2(vecOne)` / `common::WbV2(vecTwo)`.
  - integral / enum / `crc_t` / other non-float `T` → pass through unchanged
    (`{}`), no wrapper.
  Keep the placeholder as `{}` in every case (the wrappers carry their own
  `std::formatter`). The clean shape is a tiny local helper or an
  `if constexpr` fork of the `LOG(...)` call so the non-indexed and indexed
  templates share one formatting decision (see item 3).
- `.cpp` `_Vec` overloads: the operands are already `XMVECTOR`; the body stores
  to `f4One` / `f4Two` only to feed the LOG. Replace the LOG value args
  `f4One`/`f4Two` with `common::WbV2(rOne)` / `common::WbV2(rTwo)` (wrap the
  incoming vectors directly) so no float reaches the default formatter.

### 2. Make `ScopedLogDifferenceContext` non-copyable / non-movable
**File:** `Common/Log/LogDifference.h:8-13`
**Effort: 1**

Delete the copy and move constructors/assignments, matching the `ScopedLambda`
pattern:
```cpp
ScopedLogDifferenceContext(const ScopedLogDifferenceContext&) = delete;
ScopedLogDifferenceContext& operator=(const ScopedLogDifferenceContext&) = delete;
```
(Deleting copy implicitly suppresses move — no separate move declarations
needed.) Leave `pcPrevious` and the borrowed-`const char*` design as-is; the
struct member naming (no `m` prefix) is correct for a `struct`.

### 3. (Incidental, only if item 1 forces a shared edit site) collapse the
comparison/format into one place
**Files:** `Common/Log/LogDifference.h:16-57`
**Effort: 1**

The non-indexed and indexed header templates are byte-identical except for the
format string (`{}` vs `{}[{}]`) and the `iIndex` arg. If implementing item 1
cleanly requires touching both, factor the value-formatting decision into a
single helper used by both so the `Wb`/`WbV2` logic lives in one spot. Do **not**
undertake a broader refactor of these templates beyond what item 1 requires —
keep the change minimal. The `memcmp` equality branch may be left as-is (it is
correct; see Context) or, if it costs nothing, replaced with `rOne == rTwo` for
the `XMFLOAT*` case to drop the `if constexpr` branch — optional, not required.

## Critical files

- `Common/Log/LogDifference.h` — both template LOG sites, the `ScopedLogDifferenceContext` guard.
- `Common/Log/LogDifference.cpp` — both `_Vec` overload LOG sites.

## Out of scope

- **Changing the `memcmp` equality to `operator==` as a "correctness fix"** —
  it is not a bug (bitwise == bitwise for diagnosis; no padding read). Optional
  cosmetic only.
- **Null-guarding / lifetime-validating `pcContext`** — violates the project
  "assume parameters valid / DO NOT add validation" directives.
- **Renaming `rOne`/`rTwo` to `rClient`/`rServer`, the "LogDifferences" plural
  wording, `_Vec` suffix vs `FixedString` NTTP unification, `gp` prefix
  comment, `[[nodiscard]]`, explicit includes, store-on-mismatch micro-opt** —
  cosmetic / style / speculative; do not touch (don't-touch-unrelated-code).
- **Reworking the `Client:`/`Server:` directionality contract** — speculative
  caller-misuse; assume params valid.

## Acceptance criteria

- No `float`, `double`, `XMFLOAT2/3/4/4A`, or `XMVECTOR` value reaches a `LOG`
  call in `LogDifference.{h,cpp}` without a `common::Wb` / `common::WbV2`
  wrapper; non-float `T` still passes through unwrapped.
- Format placeholders remain `{}` (no `{:.Nf}`-style specs introduced).
- `ScopedLogDifferenceContext` is non-copyable and non-movable.
- Comparison behavior (which fields are flagged equal/different) is unchanged.
- Common builds clean (client and server).

## Notes

- Validation against source: all 12 reported symbols/lines exist verbatim — no
  phantom findings. Dropped as non-bugs/cosmetic: H1 (memcmp ≡ canonical bitwise
  for diagnosis), H3 (lifetime/null = assume-valid), M2/M3/M4 and L1–L5 (DRY /
  naming / wording / micro-opt — don't-touch-unrelated-code). Surviving: the
  LOG float-wrapper rule (H2, downgraded to Medium — diagnosis-only path) and
  the RAII copy/move hardening (M1, minor).
- `Wb` / `WbV2` and the engine-type `std::formatter` specializations live in
  `Common/Log/LogFormatters.h`; both are visible to `LogDifference` through the
  `Common.h` aggregation header (no new include needed).
