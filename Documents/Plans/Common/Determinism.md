# Determinism Exception-Handler Install: One-Time Gate + Fault-Path Allocation Removal

## Context

`Common/Determinism.cpp` implements `SetupExceptionHandling()`, called from every `ThreadLocal`
constructor (one call per thread at thread creation — confirmed by `Common/CLAUDE.md`
"Every thread's `ThreadLocal` ctor calls ... `SetupExceptionHandling()`"). The function installs
four fault handlers: the SEH translator, the invalid-parameter handler, the vectored exception
handler, and the terminate handler.

Two real defects surfaced in the analysis of this file (`Common Analysis/Determinism.md`):

1. **Racy non-atomic one-shot guard.** The vectored-exception-handler install is gated by a plain
   `static bool sbDone` with a check-then-set that is neither atomic nor under `sMutex`. If two
   threads enter `SetupExceptionHandling()` concurrently (e.g. the first worker threads spinning up
   together), both can read `sbDone == false`, both set it, and both call
   `AddVectoredExceptionHandler` — registering the handler twice (double-log, double-`DEBUG_BREAK()`
   per fault) and constituting a formal data race on `sbDone` (UB).

2. **Heap allocation on the invalid-parameter fault path.** The invalid-parameter handler builds its
   diagnostic message with a `std::wstring` plus several `+=` reallocations and `std::to_wstring`,
   then `ToString(...)`s it — multiple heap allocations inside a fault handler. When the fault under
   diagnosis is heap corruption (`STATUS_HEAP_CORRUPTION`, explicitly handled by the sibling vectored
   handler), re-entering the allocator can re-fault or recurse. The sibling SEH translator already
   demonstrates the correct pattern: it formats into a fixed stack/static `char` buffer rather than
   allocating.

The other three installers besides the vectored handler are process- or thread-global:
`_set_se_translator` and `_set_invalid_parameter_handler` are *per-thread* by the CRT contract
(`_set_se_translator` legitimately so — it must run on every thread), while `_set_error_mode`,
`SetErrorMode`, the three `_CrtSetReportMode` calls, and `std::set_terminate` are *process-global*
and are redundantly re-applied on every thread spin-up. Consolidating those process-global installs
under the same one-time gate is the natural scope of fix (1) and also resolves the cross-referenced
"per-thread re-install of process-global handlers" finding noted against `ThreadLocal`.

This file is determinism infrastructure. Note explicitly out of scope below: the bitwise
(non-epsilon) `operator==` set in `Determinism.h` is correct **by design** for CRC reconciliation —
do not add epsilon comparison.

## Design

### 1. One-time process-global install gate (Effort 2, Risk 2)

Replace the `static bool sbDone` check-then-set one-shot at `Determinism.cpp:70-73` with a
thread-safe one-time mechanism. Recommended: a function-local `static std::once_flag` + `std::call_once`
(or, equivalently, a Meyers-style function-local-static initializer whose magic-static guarantee
covers the install). Both give the compiler-enforced once-only, race-free guarantee that the bare
`static bool` lacks.

Move into that one-time block **all genuinely process-global installs**, so they too run exactly once
instead of on every `ThreadLocal` ctor:

- `_set_error_mode(_OUT_TO_DEFAULT)` — `Determinism.cpp:17`
- `SetErrorMode(...)` — `Determinism.cpp:18`
- `_CrtSetReportMode(...)` x3 — `Determinism.cpp:19-21`
- `AddVectoredExceptionHandler(1, ...)` — `Determinism.cpp:75` (the install whose current gate is racy)
- `std::set_terminate(...)` — `Determinism.cpp:152-157`

Keep **outside** the gate (genuinely per-thread, must run on every `ThreadLocal` ctor):

- `_set_se_translator(...)` — `Determinism.cpp:24` — per-thread by CRT design; keep this nuance.
- `_set_invalid_parameter_handler(...)` — `Determinism.cpp:46` — CRT per-thread handler slot.

The one-time gate is the shared fix: it removes the data race, prevents the duplicate vectored-handler
registration, and stops the redundant per-thread re-application of the process-global error modes /
terminate handler.

### 2. Fixed-buffer formatting in the invalid-parameter handler (Effort 2, Risk 1)

Rewrite the message construction in the invalid-parameter handler lambda at `Determinism.cpp:56-67`
to format into a fixed stack buffer instead of a `std::wstring` + `+=` + `to_wstring` + `ToString`
chain — mirroring the fixed-buffer pattern already used by the SEH translator
(`static char spcCode[64]` at `Determinism.cpp:41-43`). This keeps the fault path allocation-free so
it cannot re-enter (and re-fault in) the heap allocator while diagnosing heap corruption.

Use a fixed `char` (narrow) buffer + the existing zero-alloc formatting machinery; the four inputs
(`pcExpression`, `pcFunction`, `pcFile` wide pointers, `uiLine`) are emitted into it, then thrown via
`std::runtime_error`. Match whatever idiom the SEH translator's fixed buffer uses for consistency
(do not introduce a new formatting style). Size the buffer generously for the four fields and
NUL-terminate.

## Critical files

- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Determinism.cpp`
  - lines 17-21 (process-global error modes — move under gate)
  - lines 56-67 (invalid-parameter handler message build — fixed buffer)
  - lines 70-73 (`static bool sbDone` racy gate — replace with `std::call_once` / Meyers init)
  - line 75 (`AddVectoredExceptionHandler` — under gate)
  - lines 152-157 (`std::set_terminate` — under gate)

## Out of scope

- **`operator==` / epsilon.** The bitwise (non-epsilon) `operator==` for `XMVECTOR` / `XMFLOAT2/3/4`
  in `Determinism.h` is correct by design for CRC reconciliation. Do **not** add epsilon comparison.
  (Optionally the header comment wording "bitwise" vs. "IEEE ordered" could be tightened later, but
  that is a documentation nicety, not part of this plan.)
- **`_controlfp_s` return-value / control-word check** in `ConfigureThreadFloatingPoint`. Adding
  defensive return-code validation conflicts with the project directive "assume parameters valid; do
  not add validation," and the requested mode is already re-asserted downstream per frame tick in
  `FrameTick.cpp`. Dropped.
- **Magic-number / named-constant cleanups** (`pcHex[20]`, raw `0xC0000005` / `0xC0000374` /
  `0xE06D7363` vs. `EXCEPTION_*` / `STATUS_*` symbols, nested-ternary `pcType`). Cosmetic; `ToHex`'s
  `static_assert` already guarantees buffer safety. KISS/YAGNI — not worth touching here.
- **`sprintf_s(..., std::size - 1, ...)` and `static char spcCode[64]` in the SEH translator.** The
  buffer is mutex-serialized and copied into the thrown `runtime_error`; no real bug. Style-only.
- **Naming / `const` nits** (`pReserved` -> `uiReserved`, `const DWORD uiExceptionCode`, type
  unification). Cosmetic; "don't touch unrelated code."
- **Header self-containment of `Determinism.h`** (relies on `ExternalHeaders.h` include order). This
  is intentional and documented in the file's own header comment.

## Acceptance criteria

- `AddVectoredExceptionHandler` is invoked exactly once per process regardless of how many threads
  construct a `ThreadLocal` concurrently; no data race on the install gate.
- The process-global error-mode / report-mode / terminate-handler installs run once per process, not
  per thread.
- `_set_se_translator` (and `_set_invalid_parameter_handler`) continue to install per thread.
- The invalid-parameter handler performs no heap allocation while building/throwing its diagnostic
  message.

## Notes

- The single one-time-install gate is the shared fix: it simultaneously closes the `sbDone` data race
  / double-registration **and** resolves the cross-referenced `ThreadLocal` finding about
  process-global handlers being re-installed on every per-thread ctor. One change, both findings.
- Keep the `_set_se_translator` per-thread nuance intact — it is correct for the CRT to require it on
  every thread; do not fold it into the one-time gate.
- The fixed-buffer rewrite in the invalid-parameter handler should follow the SEH translator's
  existing fixed-buffer idiom rather than inventing a new one, for consistency within the file.
