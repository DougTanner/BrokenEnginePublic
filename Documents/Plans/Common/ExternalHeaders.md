# ExternalHeaders.h — Determinism Guard, NaN/Inf Macros, SDK Gate, Vulkan Handle Hardening

## Context

`Common/ExternalHeaders.h` is the PCH-backing aggregation header pulled into every translation unit via `Common.h` → `Pch.h`. It owns the determinism build knobs (SSE4-only DirectXMath, AVX `#error` guard), the PI subdivisions, `kfEpsilon`, `XMISNAN`/`XMISINF`, and the `Determinism.h` include ordering — all of which are documented as living here by design (`Common/CLAUDE.md`).

A code-analysis report was validated against the real source (source = ground truth). Four genuine Critical/High/Medium findings survived validation; the rest were dropped as documented-design, speculative/YAGNI, cosmetic, or include-sort (owned by code-style-review). This plan covers only the survivors plus two cheap diagnostic-hardening bundles that share the determinism/ordering theme.

The owned-by-design items (`kfEpsilon` global, PI subdivisions in `namespace DirectX`, `Determinism.h` include position, the SSE4-only knob, the `DT:`-annotated unscoped warning disables) are explicitly NOT changed.

## Design

### H1 — AVX/AVX2 `#error` guard runs after the DirectXMath includes (determinism build-correctness) — Effort 2, Impact 4, Risk 1
`Common/ExternalHeaders.h:124-130`. The guard `#if defined(_XM_AVX_INTRINSICS_) || defined(_XM_AVX2_INTRINSICS_)` (lines 128-130) executes *after* `#include <DirectXMath.h>` (line 125), and `_XM_SSE4_INTRINSICS_` is force-defined at line 124, so DirectXMath generally will not auto-promote to AVX — the guard is effectively dead in the common case. The real cross-CPU determinism hazard is `/arch:AVX` / `/arch:AVX2` / `-mavx` on the *compiler command line*, which changes scalar/SSE FP and `std::` math codegen across the whole TU and is invisible to the DirectXMath-internal macros.
- Add an up-front guard, BEFORE any include, on the compiler's own architecture macros: `#if defined(__AVX__) || defined(__AVX2__) || defined(__AVX512F__)` → `#error "AVX/AVX2/AVX512 detected — banned for cross-CPU determinism; remove /arch:AVX*"`.
- Keep the existing `_XM_AVX_INTRINSICS_` / `_XM_AVX2_INTRINSICS_` check (128-130) as a secondary belt-and-suspenders and give it a message (folds in L1 for line 129).

### H2 — `XMISNAN` / `XMISINF` are strict-aliasing UB and double-evaluate their argument — Effort 2, Impact 4, Risk 1
`Common/ExternalHeaders.h:146-147`.
```
#define XMISNAN(x)  ((*reinterpret_cast<const uint32_t*>(&(x)) & 0x7F800000) == 0x7F800000 && (*reinterpret_cast<const uint32_t*>(&(x)) & 0x7FFFFF) != 0)
#define XMISINF(x)  ((*reinterpret_cast<const uint32_t*>(&(x)) & 0x7FFFFFFF) == 0x7F800000)
```
- Type-punning a `float` lvalue through `uint32_t*` violates strict aliasing (UB; can be miscompiled under optimized/SSE4 builds). `<bit>` is already included (line 55) — use `std::bit_cast<uint32_t>(x)`.
- `XMISNAN` evaluates/addresses `x` twice; an argument with side effects misbehaves and an rvalue argument (`XMISNAN(a + b)`) fails to compile.
- ExternalHeaders.h still OWNS these per `Common/CLAUDE.md` — keep them here. Replace the macros with `inline constexpr` free functions (single-eval, no UB, `constexpr`-usable), e.g.:
```
inline constexpr bool XmIsNan(float f) { const uint32_t u = std::bit_cast<uint32_t>(f); return (u & 0x7F800000u) == 0x7F800000u && (u & 0x007FFFFFu) != 0u; }
inline constexpr bool XmIsInf(float f) { return (std::bit_cast<uint32_t>(f) & 0x7FFFFFFFu) == 0x7F800000u; }
```
Confirm whether DirectXMath defines its own `XMISNAN`/`XMISINF` macros (they would currently be silently redefined under the suppressed-warning region) — if so, `#undef` them before defining the free functions, or pick distinct names and update call sites. Grill/grep call sites before renaming.

### M3 — `#define VK_NULL_HANDLE nullptr` is a non-dispatchable-handle landmine — Effort 2, Impact 3, Risk 1
`Common/ExternalHeaders.h:212-213` (`#undef VK_NULL_HANDLE` / `#define VK_NULL_HANDLE nullptr`). On 64-bit, non-dispatchable Vulkan handles are `uint64_t` typedefs; `nullptr` (`std::nullptr_t`) does not implicitly convert, so `handle == VK_NULL_HANDLE` against a non-dispatchable handle will not compile. It only works today because comparisons are (apparently) restricted to dispatchable (pointer) handles.
- Preferred: stop mutating the SDK macro; introduce a project constant instead (e.g. `inline constexpr std::nullptr_t kNullHandle = nullptr;`) and use it where the `nullptr` spelling is wanted. Audit existing `VK_NULL_HANDLE` usage to scope the change.
- If the redefinition must stay (decide during grill), at minimum add a comment documenting it is valid only for dispatchable handles and is intentional, so the landmine is visible.

### M4 — Windows-SDK `static_assert` ANDs two independent version fields — Effort 1, Impact 3, Risk 1
`Common/ExternalHeaders.h:117`: `static_assert(VER_PRODUCTBUILD >= 10011 && VER_PRODUCTBUILD_QFE >= 16384, "Update the Windows SDK");`. `VER_PRODUCTBUILD_QFE` (the servicing/QFE number) resets per build number, so an independent `QFE >= 16384` predicate is not a "minimum version" gate: a newer `VER_PRODUCTBUILD` shipping with a smaller QFE would wrongly fail, while an older build with a high QFE passes. The current form encodes "exactly this servicing level," not "this version or newer."
- Replace with a build-number-primary comparison, QFE only as tiebreak when build numbers are equal:
```
static_assert(VER_PRODUCTBUILD > 10011 || (VER_PRODUCTBUILD == 10011 && VER_PRODUCTBUILD_QFE >= 16384), "Update the Windows SDK");
```

### Bundled minor hardening (cheap, on-theme — confirm during grill)
- **L1 — bare `#error` messages**: `Common/ExternalHeaders.h:121, 129, 242, 244` are bare `#error` directives. Add diagnostic text so a tripped guard is self-explanatory: line 121 = "DirectXMath included before ExternalHeaders.h"; line 129 = AVX message (folded into H1); lines 242/244 = "BT_DEBUG vs DEBUG/_DEBUG/NDEBUG/_NDEBUG inconsistent". Effort 1, Impact 2, Risk 0.
- **L7 — enforce the Determinism.h ordering invariant**: `Common/ExternalHeaders.h:149-152` documents in prose only that `Determinism.h` must follow the DirectXMath includes. Convert to a compile-time guard inside `Determinism.h`: `#if !defined(DIRECTX_MATH_VERSION)` → `#error "Include DirectXMath before Determinism.h"`. This touches `Common/Determinism.h` (one extra file); confirm scope during grill. Effort 1, Impact 2, Risk 1.

## Critical files
- `Common/ExternalHeaders.h` — all primary edits (H1, H2, M3, M4, L1).
- `Common/Determinism.h` — only for the optional L7 ordering guard.
- Vulkan call sites referencing `VK_NULL_HANDLE` — audit before M3 (search Engine + Projects).

## Out of scope
- **`using namespace DirectX;` (line 142) and `using namespace std::chrono_literals;` (line 58)** — the global `DirectX` using-directive is the documented mechanism for the engine's unqualified `XMVector*` math spellings; do NOT scope or remove (report H3, dropped — report itself calls it intentional debt).
- **`kfEpsilon` global (line 144), PI subdivisions in `namespace DirectX` (lines 132-141)** — owned by ExternalHeaders BY DESIGN per `Common/CLAUDE.md` (report L3, L4).
- **`Determinism.h` include position (line 152), SSE4-only knob (line 124)** — owned BY DESIGN; only adding the L7 guard, not moving anything (report M1).
- **Heavyweight std/Windows/Vulkan include surface (report M6)** — PCH aggregation is the documented single home for `#include`s; compile-time trimming is speculative and out of scope.
- **Duplicate `VK_NO_PROTOTYPES` / `VK_USE_PLATFORM_WIN32_KHR` defines (lines 165-166, 208-209) (report M2)** — identical-token redefinition is well-defined and harmless; not changing.
- **Debug-macro normalization block (lines 11-25) (report M5)** — `BT_DEBUG` and the CRT runtime (`/MDd`) are configured together in the vcxproj by design; no evidence of an actual mismatch; reworking working build config is YAGNI.
- **Include-sort order of `<variant>`/`<sstream>`/`<span>` (lines 84-86) (report L2)** — owned by code-style-review / `.editorconfig`.
- **`#pragma warning(disable:...)` block (lines 252-279) (report L5), `#pragma comment(lib,...)` (lines 103/109/112) (report L6)** — intentional, `DT:`-annotated, currently links fine.
- No input validation / error handling added (assume valid params per project directives).

## Acceptance criteria
- An AVX/AVX2/AVX512 build (e.g. `/arch:AVX2`) fails to compile with a clear determinism `#error` raised before DirectXMath is included.
- `XMISNAN`/`XMISINF` (or their `XmIsNan`/`XmIsInf` replacements) are single-evaluation, free of strict-aliasing UB, and produce identical results to the prior bit patterns; existing call sites compile and behave unchanged.
- SDK `static_assert` passes for the current SDK and would correctly accept a newer build number with a lower QFE.
- Client, server, and DataPacker all build (see `/compile`).

## Notes
- Validation summary: report H1, H2, M3, M4 confirmed against source line-for-line; symbols/lines exist (H1 guard is at 128-130, not the report's 123-130, but the claim holds). No phantom findings detected. No confident added-from-source bug (DirectXMath's own `XMISNAN`/`XMISINF` macro definitions could not be cross-checked because the shell was unavailable this session — see H2 grill note).
- H1 and H2 are the highest-value items (build-time determinism correctness and removal of optimizer-visible UB). M3/M4 are cheap correctness hardening. L1/L7 are trivial diagnostic improvements that ride along with the same lines.
