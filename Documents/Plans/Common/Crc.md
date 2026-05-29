# Crc.h Hardening: De-duplicate Hash Core, Fix char Sign-Extension, Lock the XMVECTOR Overload

## Context

`Common/Crc.h` is the codebase-wide deterministic 64-bit `Crc()` hash family that drives
client/server desync detection and replay verification (per `Common/CLAUDE.md` — `Crc()` is
`constexpr`, `XMVECTOR` hashing round-trips via `XMFLOAT4`, and the two-target overload XORs into
both full + shared collection CRCs). Because every byte the engine hashes flows through one tiny
mixing loop, any divergence between the compile-time and run-time evaluation paths — or any
change in how a byte value is folded in — is a determinism hazard: it silently invalidates
replays and produces spurious (or masked) desyncs that still compile clean.

Three genuine issues exist in the current source (line numbers verified against
`Common/Crc.h`):

1. The seed, multiplier, and full mixing loop are **copy-pasted** between the runtime `constexpr
   Crc` and the `consteval CrcConsteval`, guarded by a `static_assert` over a **single** input.
2. The mixing loop folds in a **signed `char`**, so high bytes sign-extend — a latent
   cross-toolchain / cross-build determinism landmine.
3. The generic byte-reinterpret template is a viable overload candidate for `XMVECTOR`;
   correctness relies entirely on an unasserted overload-resolution tie-break.

## Design

### 1. Collapse the duplicated hash core behind `Crc` and `CrcConsteval` (DRY + determinism) — effort 1

The mixing logic in `Crc(std::string_view)` (`Common/Crc.h:52-60`) is byte-for-byte duplicated in
`CrcConsteval(std::string_view)` (`Common/Crc.h:67-75`), including both magic constants
(`0xabcdef123456789a` seed, `0x123456789abcdef1` multiplier). The only guard against drift is
`static_assert(Crc("test") == CrcConsteval("test"), ...)` at `Common/Crc.h:76`, which proves
agreement for exactly one literal. A future edit to one constant but not the other that still maps
`"test"` identically would pass the assert yet diverge for every real asset name — the
constexpr-vs-runtime divergence hazard.

- Hoist the two magic numbers to a single definition:
  `inline constexpr crc_t kCrcSeed = 0xabcdef123456789a;` and
  `inline constexpr crc_t kCrcMultiplier = 0x123456789abcdef1;`.
- Rewrite `CrcConsteval` to forward to the existing `constexpr Crc`:
  `consteval crc_t CrcConsteval(std::string_view pData) { return Crc(pData); }`. `Crc` is already
  `constexpr`, so the `consteval` wrapper still enforces compile-time evaluation while sharing one
  definition. This eliminates the divergence surface entirely.
- The `static_assert` (`Common/Crc.h:76`) becomes redundant; keep it as belt-and-suspenders (it is
  now trivially true) — no need to expand its input set.
- Preserve the existing `#pragma warning(suppress: 26497)` on `CrcConsteval`.

### 2. Fold bytes as `unsigned char` (determinism / non-ASCII portability) — effort 1

In `Crc(std::string_view)` (`Common/Crc.h:55-57`) the loop variable is `const char& rC`. `char`
signedness is implementation-defined (MSVC defaults to signed); in `crc ^ rC`, `rC` integer-promotes
to `int`, and any byte `>= 0x80` becomes negative and sign-extends to a 64-bit value with the high
56 bits set before the XOR. The trivially-copyable and pointer+count overloads routinely feed raw
object bytes (high bytes) through here. The hash is self-consistent on MSVC today, so this is **not**
an active desync — but it is a latent landmine: switching to unsigned `char`, or any future
non-MSVC target, would silently change every CRC that ever sees a high byte, invalidating replays
and cross-build determinism.

- Iterate as `unsigned char` so the byte value is zero-extended deterministically regardless of
  `char` signedness: `for (unsigned char uiByte : pData) { crc = (crc ^ uiByte) * kCrcMultiplier; }`.
- Because step 1 makes `CrcConsteval` forward to `Crc`, this fix lands in exactly one place.
- NOTE — DETERMINISM-SENSITIVE: this changes the hash output for any input containing bytes
  `>= 0x80` (non-ASCII names, raw object bytes). All persisted/baked CRCs and committed replay
  baselines must be regenerated as part of this change. Confirm with the user that a CRC-domain
  reset is acceptable before merging (see Acceptance criteria).

### 3. Lock the dedicated `XMVECTOR` overload against silent regression — effort 1

`XMVECTOR` is not string-like, so it satisfies `NotStringLike` and the generic byte-reinterpret
template `Crc(const T&)` (`Common/Crc.h:97-102`) is a viable candidate for it. Correct behaviour
relies solely on the non-template `Crc(FXMVECTOR)` (`Common/Crc.h:105-110`) winning overload
resolution. That dedicated overload exists specifically to normalize layout (raw `__m128`
byte-reinterpret could vary); if it were ever removed or shadowed, `XMVECTOR` would silently fall
back to a 16-byte raw hash with no compile error — a fragile, determinism-sensitive coupling that
is asserted nowhere.

- Exclude `XMVECTOR` from the generic template via the `requires` clause, e.g.
  `requires NotStringLike<T> && !std::is_same_v<std::remove_cvref_t<T>, XMVECTOR>`, converting a
  silent fallback into a compile error if the dedicated overload ever disappears.
- If the `requires` form proves awkward against `FXMVECTOR`/`__m128` aliasing, fall back to a brief
  comment at the generic template stating the dedicated `XMVECTOR` overload must always win and why.

## Critical files

- `Common/Crc.h` — all three changes (single header, header-only). Lines: `52-60` (core loop +
  constants), `67-76` (`CrcConsteval` + `static_assert`), `97-102` (generic template `requires`),
  `105-110` (dedicated `XMVECTOR` overload — referenced, not necessarily edited).

## Out of scope

- Padding-byte hashing of trivially-copyable types (indeterminate padding → nondeterministic CRC).
  Owned by `Determinism_IndeterminateBytes.md` — do NOT touch here.
- `XMStoreFloat4` -> `XMStoreFloat4A` in the `XMVECTOR` overload (`Common/Crc.h:108`): stored bytes
  are identical, so this is a style-idiom item (style rule 44) owned by code-style-review, not a
  determinism bug.
- The two-target XOR-fold cancellation property (`Common/Crc.h:113-119`): order-independent
  multiset-XOR is the intended design; only a clarifying comment was suggested — leave to
  code-style-review / by-design.
- Struct `m`-prefix on `ConstexprCrcArray` members, `XM_CALLCONV` on the `const T&` template,
  `FixedString` implicit conversion, `IntToString` negative-input / `iCount * sizeof(T)`
  signed-unsigned (all "assume valid params" or pure style).
- `IntToString` / `ConstexprCrcArray` use `std::string` but are `consteval` only — never run in the
  main loop; no allocation-tracking concern.

## Acceptance criteria

- `Crc` and `CrcConsteval` share exactly one copy of the mixing loop and one definition of each
  magic constant (`kCrcSeed`, `kCrcMultiplier`); changing a constant cannot diverge the two paths.
- The existing `static_assert(Crc("test") == CrcConsteval("test"))` still holds (now trivially).
- The mixing loop folds byte values as `unsigned char`; no `char` sign-extension path remains.
- The generic `Crc(const T&)` template cannot be selected for `XMVECTOR` (compile error if the
  dedicated overload is removed), OR a comment documents the required tie-break.
- User has confirmed the CRC-domain change from the `unsigned char` fix (regenerate baked CRCs /
  replay baselines) is acceptable; baselines regenerated.
- Common project compiles clean (header is consumed widely via `Common.h` / `Pch.h`).

## Notes

- The padding-bytes determinism finding (byte-reinterpret hashing includes uninitialized padding
  in trivially-copyable types) is intentionally excluded here — it is owned by
  `Determinism_IndeterminateBytes.md`.
- All symbols/lines in this plan were verified against `Common/Crc.h`; no phantom findings.
- The seed/multiplier are non-cryptographic by design (multiplier is odd → invertible mod 2^64);
  this plan does not change the algorithm's strength, only its definition site, byte-folding
  determinism, and overload safety.
