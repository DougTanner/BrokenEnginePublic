# AlignedMemory Overflow Guard and Doc Correctness

## Context

`Common/AlignedMemory.h` is the 34-line single-chokepoint factory for 64-byte SIMD-aligned, RAII-managed storage (`AlignedUniquePtr` / `MakeAligned` / `AlignedDeleter`, per `Common/CLAUDE.md`). All current callers live in `Engine/Source/Frame/Collections/CollectionMemory.h` (`AllocateCollection` / `GrowCollection`) and always instantiate `MakeAligned<std::byte>(iBufferSize)`, where `iBufferSize` is itself a sum of `RoundUp(iCapacity * sizeof(ElementType), 64)` `int64_t` accumulations.

Two genuine issues remain after pruning the report:

1. The byte-size expression at the allocation site multiplies a signed `int64_t` count by an unsigned `size_t` `sizeof(T)` with no guard. The signed operand converts to `size_t` and the product wraps modulo 2^64. A negative or very large `int64_t` (e.g. an upstream `iBufferSize` sum that overflowed in `CollectionMemory.h`) silently yields a small allocation, and `_aligned_malloc` then succeeds with a buffer far smaller than the caller believes it owns — the classic under-allocation -> out-of-bounds heap write corruption class. Because `MakeAligned` is the one allocation chokepoint, an assert here catches the whole family at the true fault point.

2. The doc-comment block (lines 23-26) is stale: it claims the function "Throws `std::bad_alloc` if allocation fails" (it does not — `_aligned_malloc` returns `nullptr`, which is wrapped and returned), and documents an `iAlignment` parameter that does not exist in the signature (`MakeAligned(int64_t uiCount)` hard-codes `64`). The comment actively misleads readers about both the failure contract and the API surface.

This plan does NOT add OOM error handling or change the null-on-failure behavior — per project policy ("assume parameters valid", "do not add error handling/validation"), the null-vs-`bad_alloc` divergence is resolved by correcting the comment to describe actual behavior, not by introducing a throw on allocation failure.

## Design

- **Overflow guard in the size computation in `MakeAligned<T>`** (`Common/AlignedMemory.h:31`) — [effort: S]
  Compute the byte count once in `size_t` and assert the count is non-negative and the multiply did not wrap before calling `_aligned_malloc`. Sketch:
  `const size_t uiBytes = static_cast<size_t>(uiCount) * sizeof(T); ASSERT(uiCount >= 0 && uiBytes / sizeof(T) == static_cast<size_t>(uiCount)); return AlignedUniquePtr<T>(static_cast<T*>(_aligned_malloc(uiBytes, 64)));`
  `sizeof(T)` is always >= 1 so the division is safe with no zero-check. `ASSERT` `DEBUG_BREAK()`s + throws (see `Common/CLAUDE.md` Exception Handling), turning silent corruption into a crash at the allocation site.

- **Rename parameter `uiCount` -> `iCount`** (`Common/AlignedMemory.h:29`) — [effort: S]
  The `ui` prefix denotes unsigned but the type is signed `int64_t`. Rename to the `i` prefix to match the type (keep `int64_t`; counts default to signed int64 per the style guide). Update the one body reference and the comment. Mechanical; no caller change (`MakeAligned<std::byte>(iBufferSize)` call sites are positional).

- **Correct the stale doc comment** (`Common/AlignedMemory.h:23-26`) — [effort: S]
  Remove the "Throws `std::bad_alloc`" line (replace with the actual contract: returns an empty/null `AlignedUniquePtr` if `_aligned_malloc` fails). Remove the non-existent `iAlignment` parameter line and the "custom alignment" framing; state the fixed 64-byte alignment. Keep the block concise (project favors fragments).

## Critical files

- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\AlignedMemory.h` — the only file edited (lines 23-31).
- `C:\Users\dougt\Documents\BrokenEnginePublic\Engine\Source\Frame\Collections\CollectionMemory.h` — read-only reference for caller behavior (no edit; all callers use `std::byte`, positional arg).

## Out of scope

- Do NOT add a `static_assert(std::is_trivially_destructible_v<T>)` / trivial-`T` constraint (report M1/M3/L6). The alias is array-of-trivial by design and every caller uses `std::byte`; this is speculative robustness (YAGNI).
- Do NOT add `std::max<size_t>(64, alignof(T))` or restore a runtime alignment parameter (report M2 fix-b). Speculative API expansion; 64 is the deliberate cache-line/SIMD constant.
- Do NOT add a throw / null-check that changes the OOM behavior beyond the assert (report H2 "throw on failure" variant). Resolve the contract by fixing the comment, not by adding error handling.
- Do NOT add `[[nodiscard]]` (L2) or `noexcept` (L5) — speculative API hardening, no bug.
- Do NOT add per-file `#include <memory>` / `<malloc.h>` (L4) — convention says standard headers go in `Common/ExternalHeaders.h`.
- Do not touch `AlignedDeleter` or the `AlignedUniquePtr` alias.

## Acceptance criteria

- `MakeAligned` computes byte size in `size_t` and `ASSERT`s `uiCount >= 0` plus the wrap check before `_aligned_malloc`.
- Parameter renamed `iCount` with the comment and body reference updated consistently.
- Doc comment no longer mentions `std::bad_alloc` or an `iAlignment` parameter and accurately states fixed 64-byte alignment + null-on-failure.
- `Common`, `DataPacker`, and `BrokenEngineSandbox` (client + server) compile clean; no caller edits required.

## Notes

- Real-world exposure today is low: every caller passes `std::byte` (`sizeof == 1`), so the multiply at this site cannot itself overflow under current usage. The guard's value is (a) catching an already-overflowed/negative `iBufferSize` accumulated upstream in `CollectionMemory.h`, and (b) protecting the public chokepoint against future non-byte `T`. Impact is rated for the corruption class the chokepoint guards, not current call frequency.
- Score: Effort 1, Impact 4 (overflow -> heap corruption), Risks 1 (mechanical edit, single file, no API/behavior change for valid inputs). Score = E - I + R = 1 - 4 + 1 = -4.
