# Flags<ENUM>: fix Toggle partial-mask break and operator& return-type asymmetry

## Context

`common::Flags<ENUM>` (`Common/Flags.h`) is the project-mandated bit-flag wrapper — "Flags over booleans" in both the root and `Common/CLAUDE.md`, enforced as a hard flag by the code-review skill. It is widely instantiated (~40 `using XFlags_t = common::Flags<...>` aliases across Engine / Projects / DataPacker), is `Members()`-style serializable (`Write`/`Read` at `Flags.h:111-121`), CRC-hashable, and has a `std::formatter` specialization (`Common/Log/LogFormatters.h:170`, takes `Flags` by value). Any change here is broad-blast-radius and must preserve serialized layout and the formatter's by-value access.

Two genuine correctness/footgun issues remain after validating the report against source. The report's other items are dropped as style-owned, YAGNI, input-validation, or already-implemented (the unsigned-underlying `static_assert` it lists as "missing-with-no-message" already exists at `Flags.h:10`).

## Design

### 1. `Toggle()` hits `DEBUG_BREAK()` on a partially-set multi-bit mask — `Common/Flags.h:84-107` — effort:2
`Toggle(ENUM eFlag)` reads `iFlag = std::to_underlying(eFlag)` then branches three ways:
- `(iCurrent & iFlag) == iFlag` (all mask bits set) -> clear them,
- `(iCurrent & iFlag) == 0` (no mask bits set) -> set them,
- else (partial) -> `DEBUG_BREAK()` and write back the unmodified `iCurrent` (no toggle happens), yet still returns `(iCurrent & iFlag) != 0`.

For a single-bit enumerator the mask is always fully set or fully clear, so the break is unreachable — and every current caller passes a single bit (`PlayersCombat.cpp:345` `Toggle(kBlasterSpawnLeft)`, `:410` `Toggle(kMissileSpawnLeft)`; `Game.cpp:840` `Toggle(GameFlags::kPaused)`; `gFullscreen.Toggle()`/`gDebugTexture.Toggle()` are single-bit). But passing a composite mask (e.g. a `kReadWrite = kRead | kWrite` enumerator, which the `initializer_list`/`Set` API fully supports building piecemeal) with only some bits set falls into the `else` and calls `DEBUG_BREAK()`. `DEBUG_BREAK()` is a real break whenever a debugger is attached (`Common/ErrorUtils.h:11`) and the partial case also silently no-ops the toggle while returning a "result" for an operation it didn't perform — a latent footgun on a foundational wrapper.

Resolution: replace the three-way branch with a plain XOR toggle:
```cpp
constexpr bool Toggle(ENUM_TYPE eFlag)
{
    underlying_t iFlag = std::to_underlying(eFlag);
    underlying_t iCurrent = std::to_underlying(meFlags) ^ iFlag;
    meFlags = static_cast<ENUM_TYPE>(iCurrent);
    return (iCurrent & iFlag) != 0;
}
```
XOR is the conventional, branch-free toggle, correct for single- and multi-bit masks alike, matches the no-rejection behavior of `Set`/`Clear`, stays `constexpr` (no need for the `std::is_constant_evaluated()` guard), and is KISS. Per the project "assume parameters are valid" directive, do not add an `ASSERT` single-bit precondition — just XOR.

### 2. `operator&(ENUM)` returns `bool` but `operator&(const Flags&)` returns `underlying_t` — `Common/Flags.h:73-82` — effort:2
Two overloads of the same operator return unrelated types:
- `operator&(ENUM eFlag) const` -> `bool` (a "has any of these bits" membership predicate),
- `operator&(const Flags& rOther) const` -> `underlying_t` (raw masked bits).

Same spelling at the call site, two meanings: `if (flags & kSomeEnum)` is a clean predicate, but `if (flagsA & flagsB)` silently yields an integer (implicitly converted to bool here, but `auto x = flagsA & flagsB;` captures a bare `underlying_t`), leaking the concrete integer width and defeating the type-safety the wrapper exists to provide. This is a real footgun on a determinism-sensitive type.

Caller constraint discovered in source: `PushersUpdate.cpp:146` (`if ((flags & excludeFlags) != 0u)`) and `:152` (`if ((flags & includeFlags) == 0u)`) compare the `Flags x Flags` result against `0u`. These compile only because the overload returns a raw `underlying_t`; a same-type `Flags` result would break them (`Flags` has no `== 0u` — its `operator<=>` is `Flags <=> Flags` only). So a blind "return `Flags`" is NOT safe.

Resolution (two viable shapes — pick one during grill, default A):
- A (least churn, recommended): keep both overloads' current return types but rename `operator&(const Flags&)` to an explicit, intention-revealing accessor — `constexpr underlying_t Mask(const Flags& rOther) const` (or `Intersection`) — so the raw-integer result is no longer hidden behind the same `&` spelling as the `bool` predicate. Update the two `PushersUpdate.cpp` sites to `(flags.Mask(excludeFlags) != 0u)` / `(flags.Mask(includeFlags) == 0u)`. This removes the footgun (the two operators no longer collide) while preserving the raw-int semantics the only caller actually needs.
- B (closed algebra): change `operator&(const Flags&)` to return `Flags`, and migrate the two `PushersUpdate.cpp` sites to a `Flags`-friendly test (e.g. `!(flags & excludeFlags).Empty()` / `(flags & includeFlags).Empty()`). More uniform but touches more call-site semantics and adds a query method.

Prefer A: smallest blast radius, KISS, and it directly resolves the asymmetry (same `&` no longer means two different return kinds). Do NOT add `operator|`/`^`/`~`/compound-assign — YAGNI, no callers, out of scope.

Note: neither shape alters the serialized representation of `meFlags` (the `Write`/`Read` path is untouched), so CRC/serialization layout and replay determinism are unaffected.

## Critical files
- `Common/Flags.h` — only file changed; both fixes are local to this header.
- `Common/ErrorUtils.h` — read-only reference (`DEBUG_BREAK()` fires under an attached debugger; not a no-op).
- `Common/Log/LogFormatters.h:170` — `std::formatter<Flags>` takes `Flags` by value and reads it; the fixes keep that intact. Verify it does not depend on `operator&(Flags)->underlying_t`.
- `Engine/Source/Frame/Collections/Pushers/PushersUpdate.cpp:146,152` — the only `operator&(Flags)` raw-int consumers found; must be updated alongside the rename/return-type change (option A renames to `.Mask(...)`). All other `operator&` uses across the ~40 `Flags<...>` instantiations are `Flags & ENUM` boolean predicates and are unaffected.

## Out of scope
- Adding `operator|`/`operator^`/`operator~`/`operator&=`/`operator|=`, `Any()`/`All()`/`HasAll()`/`Count()` — YAGNI, no callers (report H2 extras / H3 / L3).
- `static_assert` message text on the existing unsigned-underlying guard at `Flags.h:10` — cosmetic (report L7); the guard itself already exists and is correct.
- `Set`'s side-effect ternary `bSet ? ... : ...` (report M2), `underlying_t` alias naming (L5/L6), `iValue`/`iFlag`/`uiX` Hungarian (L4), `explicit` on the converting ctor (L1), public `meFlags` (L2) — code-style-review owns; do not touch.
- Thread-safety doc comment (report M1) — design contract is single-owner; not a code bug.
- `Read` undefined-bit masking / serialized-size version guard (report M3/M4) — these treat deserialized bytes as untrusted input; per project directive assume valid inputs, and the engine-wide `Write`/`Read` + container versioning (`DataHeader::kiVersion`) already owns format-change protection. Do not change the serialized format.
- `iValue` initialization in `Read` (report M3 sub-point) — immediately overwritten by `common::Read`; style-owned at most.

## Acceptance criteria
- `Toggle(mask)` for any prior state and any mask (single- or multi-bit) flips exactly the mask bits, returns whether those bits are now set, and never calls `DEBUG_BREAK()`.
- The same `&` spelling no longer returns two different kinds: `flags & kEnum` is `bool`; the `Flags x Flags` intersection is reached via a distinctly-named accessor (option A) or returns `Flags` (option B).
- `PushersUpdate.cpp:146,152` updated to the chosen shape and still compile; all other `Flags<...>` callers and the `std::formatter<Flags>` specialization compile unchanged.

## Notes
- Effort 2; Impact 3 (removes a reachable hard break + a same-operator/two-return-kinds footgun on a foundational, ~40-site wrapper); Risk 2 (changes a widely-used wrapper's public API surface — the `operator&(Flags)` overload — and touches the two `PushersUpdate.cpp` consumers; serialized `meFlags` layout is untouched, so CRC/replay determinism is preserved).
- The report under analysis matched real source line numbers and symbols; it was not the hallucinated report in this batch.
