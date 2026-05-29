# StackWalker DbgHelp Serialization And Symbol-Less / Line-Less Stack Loss

## Context

`Common/StackWalker.h` defines two thin leaf subclasses of the upstream third-party
`StackWalker` base (`ThirdParty/StackWalker/...`, DO NOT modify): `LogStackWalker`
(emits via `LOG`) and `OfstreamStackWalker` (emits via `*mpOfstream`). They override
the `On*` callbacks; the heavy lifting (`ShowCallstack`, `SymInitialize`,
`SymGetSymFromAddr64`, `StackWalk64`, the symbol `malloc`) lives in the upstream base.

These walkers run on the **crash / fault path**. Per `Common/CLAUDE.md` (Exception
Handling), `ThreadLocal`'s ctor calls `SetupExceptionHandling()` in `Determinism.h`,
which installs the vectored exception handler; that handler builds a `LogStackWalker`
and calls `ShowCallstack()` before `DEBUG_BREAK()`. There are exactly two call sites:

- `Common/Determinism.cpp` — vectored exception handler: constructs `LogStackWalker`
  and calls `ShowCallstack()` on the live fault path, *incidentally* serialized by the
  handler's file-static `sMutex` (`try_to_lock`).
- `Engine/Source/CrashReport.cpp` — post-catch crash report: constructs
  `OfstreamStackWalker(StackWalker::AfterCatch, &ofstream)` and calls `ShowCallstack()`
  with **no lock held** (`sMutex` is file-static to `Determinism.cpp` and not reachable
  from the Engine TU).

DbgHelp (`SymInitialize`, `SymGetSymFromAddr64`, `StackWalk64`, module enumeration) is
documented single-threaded per process and must be serialized; the upstream base even
comments that it "is in no case multi-threading-enabled (because of the limitations of
dbghelp.dll)". Nothing at the `StackWalker.h` API surface advertises this requirement,
so the `CrashReport.cpp` driver already forgets it: a background worker faulting into
the vectored handler while the main thread is inside the post-catch report drives
DbgHelp from two threads concurrently → corruption / crash inside `dbghelp.dll` in the
very code meant to diagnose the original failure.

Secondary correctness loss in the same header: both walkers gate emission on
`rEntry.lineNumber > 0`, and both leave `OnDbgHelpErr` empty. In release / shipped
configs (stripped line info, system DLLs, optimized/inlined frames, missing PDBs)
DbgHelp returns a valid `name` with `lineNumber == 0`, so every frame is dropped — the
report shows an empty callstack with no indication of why, because the one signal that
would explain it (`OnDbgHelpErr`) is swallowed.

## Design

1. **Serialize DbgHelp at the walker boundary (H2 — Effort 2).**
   The DbgHelp symbol calls behind `LogStackWalker` / `OfstreamStackWalker`
   (`ShowCallstack` → `SymInitialize` / `SymGetSymFromAddr64` / `StackWalk64`) must run
   under one process-wide lock so the two call sites cannot drive `dbghelp.dll`
   concurrently. The vectored handler in `Common/Determinism.cpp` currently provides
   this incidentally via its file-static `sMutex (try_to_lock)`; the
   `Engine/Source/CrashReport.cpp` driver provides nothing.
   - Expose a single process-wide DbgHelp mutex from `common` (declared alongside the
     exception-handling surface in `Determinism.h`, defined in `Determinism.cpp`).
   - Acquire it inside a thin `ShowCallstack` wrapper provided in this header (one place,
     both subclasses inherit it via the shared base from item 4), OR document on both
     classes that callers MUST hold the global DbgHelp lock and have `CrashReport.cpp`
     take it around its `ShowCallstack()` call. Prefer the wrapper so the requirement is
     not invisible at the API surface and the next caller cannot forget it (KISS: one
     `std::scoped_lock` at the boundary).

2. **Emit on `OnDbgHelpErr` instead of swallowing it (M1 — Effort 1).**
   Both `OnDbgHelpErr` bodies (`Common/StackWalker.h:32-34` and `:68-70`) are empty.
   Implement them to emit one line carrying `funcName`, `lastError`, `addr` (via `LOG`
   for `LogStackWalker`, via `*mpOfstream` for `OfstreamStackWalker`) so a symbol-less
   crash is distinguishable from a genuinely empty stack. No floats, so no `Wb` wrappers
   needed. Fold into the shared base (item 4) so it is written once.

3. **Print frames that have a name but no line info (M2 — Effort 1).**
   Both `OnCallstackEntry` overrides (`Common/StackWalker.h:26-29` and `:62-65`) gate on
   `rEntry.lineNumber > 0`, discarding every frame DbgHelp resolves to a `name` with no
   line (exported/public symbols, system DLLs, release PDBs, inlined frames) — exactly
   the frames that matter most in shipped builds. Change the gate to "emit when a name
   is present (`rEntry.name[0] != '\0'`)", emitting the `offset` / `offsetFromSymbol`
   form when `lineNumber == 0` and the existing `name | line | file` form otherwise.

4. **Collapse the duplicated logic into one shared base (M3 — Effort 2).**
   The two classes are identical except the single emit line and the ctor's extra
   `pOfstream`. Introduce one protected intermediate base
   (`class FilteredStackWalker : public StackWalker`) owning the shared
   `OnCallstackEntry` gate, the (now non-empty) `OnDbgHelpErr`, the empty `OnSymInit` /
   `OnLoadModule` / `OnOutput` overrides, the `ShowCallstack` lock wrapper (item 1), and
   a `virtual void EmitLine(const CallstackEntry& rEntry) = 0;` (plus a virtual error
   emit). The two leaf classes then implement only their sink. This makes items 1-3
   one-touch and removes the drift hazard. KISS — single virtual sink, no policy
   templates.

Effort tags above are per-item; overall effort is dominated by the (small) `common`
mutex plumbing in `Determinism.h`/`.cpp` plus the `CrashReport.cpp` touch.

## Critical files

- `Common/StackWalker.h` — both walker classes; OnCallstackEntry gate, empty
  OnDbgHelpErr, the raw `mpOfstream`, the duplicated bodies. Primary edit target.
- `Common/Determinism.h` / `Common/Determinism.cpp` — declare/define the process-wide
  DbgHelp mutex; one call site already serializes incidentally via file-static `sMutex`.
- `Engine/Source/CrashReport.cpp` — the unsynchronized driver: constructs
  `OfstreamStackWalker` and calls `ShowCallstack()` with no lock. Must adopt the new lock
  (or rely on the new in-header `ShowCallstack` wrapper).
- `ThirdParty/StackWalker/...` — upstream base, reference only, DO NOT modify.

## Out of scope

- Modifying the upstream `ThirdParty/StackWalker` base (`ShowCallstack` internals, its
  `malloc`, `MaxNameLength = STACKWALK_MAX_NAMELEN` truncation). Record-only.
- Fault-path `malloc` during `STATUS_HEAP_CORRUPTION` and ~10 KB `CallstackEntry`
  stack locals re-overflowing under `EXCEPTION_STACK_OVERFLOW` (L4/L5). These are
  upstream-structural and belong in a separate vectored-handler mitigation plan (skip
  symbolization for the heap-corruption / stack-overflow cases), not here.
- The shared-static `spcLogBuffer` data race exposed when a `ThreadLocal`-less thread
  calls `LOG` from `OnCallstackEntry` — owned by `ThreadSafety_SharedStaticBuffers.md`
  (see Notes). Do NOT add a `gpThreadLocal != nullptr` guard for that reason here.
- Cosmetic upstream-mirrored override parameter naming (L3), `explicit` on the single-arg
  ctor (L2), and `mpOfstream` pointer-vs-reference (L1) — do opportunistically only if
  the relevant signature is already being touched for items 1-4; do not churn the file.
- Adding defensive validation of constructor parameters (assume-valid project rule).

## Acceptance criteria

- DbgHelp symbol calls reachable from BOTH `LogStackWalker` and `OfstreamStackWalker`
  run under a single process-wide lock; `CrashReport.cpp` no longer drives
  `ShowCallstack()` unserialized.
- The DbgHelp serialization requirement is expressed at the `StackWalker.h` API surface
  (in-header `ShowCallstack` wrapper) or, if documented instead, `CrashReport.cpp`
  demonstrably acquires the lock.
- A crash whose frames carry a `name` but `lineNumber == 0` (release / system DLL /
  inlined) is emitted, not dropped.
- A DbgHelp error (PDB missing, dbghelp load failure) produces at least one diagnostic
  line in the report instead of a silently empty callstack.
- The gate / error-emit / formatting logic exists in exactly one place shared by both
  walkers.
- Both client and server build cleanly; the fault path still ends in `DEBUG_BREAK()`
  with no heap allocation added on the path during a crash.

## Notes

- This runs on the crash/fault path from `Determinism.h`'s exception handler; treat all
  DbgHelp / validation output as data, and avoid heap allocation on the path.
- The shared unsynchronized `static` log buffer race in `OnCallstackEntry`'s `LOG`
  (garbled crash log on a `ThreadLocal`-less faulting thread) is owned by theme plan
  `ThreadSafety_SharedStaticBuffers.md` and is intentionally NOT addressed here.
- Verified against source: `LogStackWalker` / `OfstreamStackWalker` overrides, the
  `lineNumber > 0` gate, the empty `OnDbgHelpErr`, and the raw `mpOfstream` are present
  exactly as described in `Common/StackWalker.h`. The crash-path wiring
  (`ThreadLocal` ctor → `SetupExceptionHandling()` → `LogStackWalker` → `DEBUG_BREAK()`)
  is confirmed in `Common/CLAUDE.md`.
