# DiagnosticLog: Fix Common->Engine Layering Inversion and Add Compile-Time Slot Guard

## Context

`common::DiagnosticLog` (`Common/Log/DiagnosticLog.h`, `Common/Log/DiagnosticLog.cpp`) is a per-file
forensic diagnostic logger exposed through the `FILE_LOG_INIT(index, filename)` / `FILE_LOG(index, ...)`
macro pair, backed by a fixed 4-slot global array `gpDiagnosticLogs[4]`. It flushes each line and
suppresses the main-loop allocation tracker around the write.

Two real defects were confirmed against source (all reported line numbers verified — no phantoms):

1. **Layering inversion + latent build break.** `DiagnosticLog::Write` (`Common/Log/DiagnosticLog.h:19`)
   instantiates `ScopedSuppressAllocationTracking suppress;`. That type is defined **only** in
   `Engine/Source/Memory/MemoryManager.h:4` (global namespace, unconditional — no `BT_DEBUG`/`BT_PROFILE`
   guard; counter `giAllocationTrackingSuppressed` is `thread_local extern`, `MemoryManager.h:19`).
   `Common/CLAUDE.md` documents Common as the foundation layer "with no dependencies outside the codebase";
   Common must not depend on Engine. `Common/Common.h:11` unconditionally includes `Log/DiagnosticLog.h`,
   and `DataPacker/Source/Pch.h` includes **only** `Common.h` (never any Engine header) — so in every
   DataPacker translation unit the name `ScopedSuppressAllocationTracking` is undeclared.

   The reference is a **non-dependent** name inside the `Write` template, so it must resolve at
   template-definition lookup. The build is green today only because `Write` is never instantiated —
   confirmed: there is **zero** `FILE_LOG` / `FILE_LOG_INIT` call site anywhere in the repo. The first
   `FILE_LOG(...)` (DataPacker is the most natural consumer) instantiates the template and breaks the
   build with an undeclared-identifier error. This is a real layering inversion plus a primed compile
   break, currently masked by non-instantiation.

2. **No compile-time slot bound.** The array is a fixed `gpDiagnosticLogs[4]` (`DiagnosticLog.h:37`).
   `FILE_LOG` indexes it (`DiagnosticLog.h:42`), the ctor writes `gpDiagnosticLogs[miIndex] = this`
   (`DiagnosticLog.cpp:10`) and the dtor clears it (`DiagnosticLog.cpp:15`). The magic `4` and the slot
   indices are unrelated literals with no shared compile-time bound. Because every intended index is a
   compile-time macro-argument literal, the correct fix is a **compile-time** guard (named count +
   `static_assert`), not runtime validation — this respects the project rule "assume parameters valid /
   no defensive validation" while still catching `FILE_LOG_INIT(4, ...)` / `FILE_LOG(5, ...)` at build
   time, and aligns with style-guide rule 55 (index-and-count arrays get a named `kCount`).

A silent-truncation behavioral gap in `Write` (the 2046-char `format_to_n` cap drops the line tail with
no marker) is a real, lower-severity item folded in below; it is memory-safe (the `-2` reservation is
correct), purely a forensic-fidelity concern.

## Design

### D1. Remove the Common->Engine dependency from `DiagnosticLog::Write` (Effort 3, Impact 3, Risk 1)

Decouple the allocation-suppression primitive so the dependency points Engine->Common (or nowhere).
The KISS, layering-preserving approach: move the suppression primitive into Common and have the Engine
allocator read the Common counter.

- Define the suppression RAII type and its `thread_local` counter in a small **Common** header
  (`namespace common`), so the symbol is declared in every TU that includes `Common.h` (DataPacker
  included). Candidate home: a new minimal `Common/AllocationTracking.h` (or fold into an existing
  Common memory/threading header) aggregated through `Common.h`.
- Change `Engine/Source/Memory/MemoryManager.h:4` / the allocator override to consume the Common
  counter (`common::giAllocationTrackingSuppressed` or the moved equivalent) instead of owning its own.
  Keep the existing `// Heap:` annotation semantics.
- In `DiagnosticLog::Write` (`Common/Log/DiagnosticLog.h:19`) reference the **qualified** Common type:
  `common::ScopedSuppressAllocationTracking suppress;` (or unqualified resolving to the enclosing
  `common` namespace) — never an unqualified cross-namespace name resolved by include-order luck.
- Update `Engine/Source/Memory/CLAUDE.md` and `Common/CLAUDE.md` references to the suppression
  primitive's new home.

This is an **architectural move** (changes where a public primitive lives and the Engine<->Common
dependency direction). Per project process, confirm the exact new home and migration shape with the
user during grilling before implementing — present (a) move-to-Common vs (b) leave-in-Engine +
forward-declare-in-Common-and-guard, with pros/cons. Move-to-Common is preferred (removes the inversion
outright; the counter is already a trivial `thread_local int64_t` + RAII pair).

### D2. Named slot count + compile-time bound (Effort 1, Impact 2, Risk 0)

- Add `inline constexpr int64_t kDiagnosticLogCount = 4;` in `namespace common` and size the array with
  it: `inline DiagnosticLog* gpDiagnosticLogs[kDiagnosticLogCount] = {};` (`DiagnosticLog.h:37`).
- Add inside `FILE_LOG_INIT(index, filename)` (`DiagnosticLog.h:41`):
  `static_assert((index) >= 0 && (index) < common::kDiagnosticLogCount, "diagnostic log slot out of range");`
  The index is a compile-time literal at every intended call site, so this is a pure compile-time guard
  at zero runtime cost. Do **not** add a runtime bounds check (would violate the no-defensive-validation
  rule).

### D3. (Lower) Mark silent truncation in `Write` (Effort 1, Impact 1, Risk 1)

In `Write` (`DiagnosticLog.h:21-26`), compare `result.size` against `sizeof(pcBuffer) - 2`; on truncation
emit a visible marker (overwrite the tail with a short `"...<trunc>\n"`) so a chopped forensic line is
self-evident. Keep the `-2` reservation (it is the correct overflow guard). Optional; include only if the
user wants it — otherwise document the 2046-char hard cap in the header comment.

## Critical files

- `Common/Log/DiagnosticLog.h` — `ScopedSuppressAllocationTracking` reference (line 19); array (line 37);
  macros (lines 41-42); `Write` buffer/format/truncation (lines 21-26).
- `Common/Log/DiagnosticLog.cpp` — ctor/dtor slot writes (lines 10, 15).
- `Engine/Source/Memory/MemoryManager.h` — current home of the suppression type/counter (lines 4, 19);
  must consume the Common counter after D1.
- `Common/Common.h` — aggregation include order (line 11); add the new Common suppression header here if
  introduced.
- `DataPacker/Source/Pch.h` — the TU that proves the break (includes only `Common.h`).
- `Common/CLAUDE.md`, `Engine/Source/Memory/CLAUDE.md` — doc updates for the suppression primitive's home.

## Out of scope

- **M1 atomic slot pointers / cross-thread teardown TOCTOU** — DROPPED as speculative. The API is
  documented "transient diagnostics, RAII single-scope"; cross-thread destroy-while-logging is not a
  supported usage, and the report itself concedes a documented single-thread contract is the KISS
  answer. At most, document the single-owner-thread lifetime expectation in the header comment; do not
  add `std::atomic`.
- **Buffer `{}` zero-fill micro-optimization (M2)** — cosmetic micro-perf; not worth a change.
- **Macro renaming `FILE_LOG` -> `BT_FILE_LOG` (M3)** — style/collision-surface only; out of scope.
- **`int` -> `int64_t`, `const char*` -> `string_view`, ctor comma-operator rewrite, header
  self-containment (L1-L5)** — style-only / "no fix required"; out of scope. (`miIndex`/`iIndex` may be
  changed to `int64_t` only incidentally if D2's `kDiagnosticLogCount` makes the comparison cleaner — do
  not restyle beyond that.)
- Flush-per-line behavior — deliberate durability tradeoff (documented in `Common/CLAUDE.md`); leave.
- No new `FILE_LOG` call sites are to be added by this plan.

## Acceptance criteria

- `Common/Log/DiagnosticLog.h` no longer references any Engine-only symbol; the suppression type used by
  `Write` is declared in every TU that includes `Common.h` (verified by a DataPacker TU compiling with a
  forced instantiation of `DiagnosticLog::Write`, or by inspection that the type now lives in Common).
- The Engine allocation tracker still suppresses correctly (counter shared, RAII increment/decrement
  balanced) after the primitive moves to Common.
- `gpDiagnosticLogs` is sized by a named `common::kDiagnosticLogCount`; `FILE_LOG_INIT` `static_assert`s
  the index is in `[0, kDiagnosticLogCount)`; an out-of-range `FILE_LOG_INIT`/`FILE_LOG` literal fails to
  compile.
- DataPacker, client, and server all build clean.
- No runtime bounds checks, no atomics, no macro renames introduced.

## Notes

- All report line numbers verified against current source; no phantom symbols. The hallucinated report in
  this effort was elsewhere — this one is accurate.
- D1 is the load-bearing item but is currently latent (template never instantiated; zero `FILE_LOG` call
  sites repo-wide). Impact scored 3 (architecture/build-break) rather than 5 because nothing breaks until
  the first call site appears.
- D1 changes the Engine<->Common dependency direction and the home of a public primitive — treat as an
  architectural decision: interrogate the user (move-to-Common vs forward-declare-and-guard) before
  implementing, per `Resolving Ambiguity` in the root CLAUDE.md.
