# ThreadLocal Lifetime & Per-Thread Exception-Handler Hardening

## Context

`Common/Threading/ThreadLocal.h` / `.cpp` is the per-thread storage object (log buffer,
Workbuffer, tick/indent context). Its ctor publishes `gpThreadLocal = this` and installs
per-thread determinism state; the dtor clears the global. A deep analysis
(`Common Analysis/ThreadLocal.md`) plus the companion `Common Analysis/Determinism.md`
flagged a small set of genuine lifetime / value-semantics / process-global-handler issues.
Findings were re-validated against the actual source this session; pure
input-validation, style-only, and "flag-only" items were dropped per the no-defensive-validation
and KISS/YAGNI directives.

Verified ground truth (read from source):
- `gpThreadLocal` is an `inline thread_local ThreadLocal*` (ThreadLocal.h:27).
- Ctor: `gpThreadLocal = this;` (ThreadLocal.cpp:13). Dtor: `gpThreadLocal = nullptr;`
  unconditionally (ThreadLocal.cpp:25).
- `Workbuffer` holds a **reference member** `std::vector<std::byte>& mBuffer` (Workbuffer.h:111),
  bound at construction to the passed vector. So `mWorkbuffer` (ThreadLocal.h:53) references the
  storage of `mWorkbufferMemory` (ThreadLocal.h:48), and `mpLogBuffer` (ThreadLocal.h:52) caches
  `mLogBufferMemory.data()` (ThreadLocal.h:47). Both are self-referential, member-order-dependent
  aliases. The reference member already makes `Workbuffer` (and thus `ThreadLocal`) implicitly
  non-copy-assignable / non-move-assignable; the user-declared `ThreadLocal` dtor additionally
  suppresses the implicit move *constructor*. But the implicit copy *constructor* is still
  available, and copying a `ThreadLocal` would bind the copy's `mWorkbuffer.mBuffer` to the
  **source's** `mWorkbufferMemory` (and copy `mpLogBuffer` to point into the source's
  `mLogBufferMemory`) — an aliasing hazard that outlives the source.
- 14 construction sites, all one-per-thread stack locals at thread entry (Engine `Main.cpp:41`,
  `Screenshot.cpp:32`, `FileManager.cpp:318,463`, `TextureUploadManager.cpp:151`,
  `CrashReport.cpp:80`, `PersistentWorker.cpp:10`, DataPacker `Main.cpp` ×4, `ExportJob.cpp:133`).
  Nesting/copy/move never happens today — the bugs below are latent, not currently triggered.
- `SetupExceptionHandling()` (Determinism.cpp) installs the SEH translator (`_set_se_translator`,
  per-thread, legitimate) plus three process-global handlers: `_set_invalid_parameter_handler`,
  `AddVectoredExceptionHandler` (Determinism.cpp:75), `std::set_terminate` (Determinism.cpp:152).
  The VEH is guarded by a **non-atomic `static bool sbDone`** check-then-set (Determinism.cpp:70-73)
  — racy across the first two concurrent thread spin-ups, and the other process-global handlers are
  re-installed on every thread ctor. (See `Common Analysis/Determinism.md` H2/M5.)

## Design

### 1. Lock the one-`ThreadLocal`-per-thread invariant (was H1) — effort 1
The ctor/dtor implement a "set on enter / clear on exit" model that is only correct with exactly
one live `ThreadLocal` per thread. A second instance on the same thread silently clobbers the
global; destroying the inner one nulls `gpThreadLocal` instead of restoring the outer, leaving
every later `gpThreadLocal->...` deref a null deref. All current sites are one-per-thread, so this
is latent. Match current intent and fail loud on the first violation:
- In the `ThreadLocal` ctor body (ThreadLocal.cpp:12, before `gpThreadLocal = this;`) add
  `ASSERT(gpThreadLocal == nullptr);`.
- Document the contract in the header above the class (ThreadLocal.h:33): "Exactly one
  `ThreadLocal` per thread; it owns `gpThreadLocal` for its lifetime."

Rationale: ASSERT is the KISS choice and matches the codebase "fail loud in debug" idiom; a
save/restore `mpPrior` member would add machinery for a nesting use case that does not exist
(YAGNI). Do **not** add save/restore unless a real nested-construction need appears.

### 2. Delete copy/move on `ThreadLocal` (was H3) — effort 1, risk 1
`ThreadLocal` is non-relocatable: `mWorkbuffer`/`mpLogBuffer` alias the object's own
`mWorkbufferMemory`/`mLogBufferMemory`. `Workbuffer`'s reference member already blocks the implicit
*assignment* operators and the dtor blocks the implicit *move ctor*, but the implicit **copy ctor**
remains — copying a `ThreadLocal` would bind the copy's `mWorkbuffer.mBuffer` reference (and
`mpLogBuffer` pointer) into the **source's** vectors, aliasing storage that outlives the source.
Make the type say what it is (explicit deletes also document the intent and forbid the copy ctor):
- Add to the `ThreadLocal` public section (ThreadLocal.h:37-39, next to the existing
  `ThreadLocal() = delete;`):
  ```cpp
  ThreadLocal(const ThreadLocal&) = delete;
  ThreadLocal& operator=(const ThreadLocal&) = delete;
  ThreadLocal(ThreadLocal&&) = delete;
  ThreadLocal& operator=(ThreadLocal&&) = delete;
  ```
- Add a one-line comment on the backing-vector members (ThreadLocal.h:47-48) noting they must
  precede `mpLogBuffer`/`mWorkbuffer` because the latter alias them (the member-init order at
  ThreadLocal.cpp:8-11 depends on it).

Risk note: these are deletions only; verify no construction site relies on aggregate/return-by-value
moves (the 14 sites are all in-place stack locals, so none do).

### 3. Guard `LogTickScope` against a missing `ThreadLocal` (was M4) — effort 1
`LogTickScope` ctor/dtor deref `gpThreadLocal->miLogTickCounter` (ThreadLocal.h:62-63, 68) with no
guarantee one exists on the thread; its safety is coupled to the item-1 invariant. Add
`ASSERT(gpThreadLocal != nullptr);` at the top of the `LogTickScope` ctor (ThreadLocal.h:61). This
is a debug-only fail-loud guard, not release-path validation, so it is consistent with the
no-defensive-validation directive. (Optional: same ASSERT in the dtor; ctor alone is sufficient
since the dtor cannot run without the ctor having passed.)

### 4. Process-global exception-handler install is per-thread / racy (was H2) — effort 2-3, risk 2-3 — DEFER to the Determinism plan
`SetupExceptionHandling()` is called from every `ThreadLocal` ctor (ThreadLocal.cpp:17-20). Only
`_set_se_translator` is genuinely per-thread. `_set_invalid_parameter_handler` /
`std::set_terminate` are process-global (idempotent, wasteful), and `AddVectoredExceptionHandler`
*accumulates* — its one-shot guard is a non-atomic `static bool sbDone` (Determinism.cpp:70-73),
racy on the first two concurrent spin-ups, which can double-register the VEH (double stack-walk +
double `DEBUG_BREAK()` per fault). `ThreadLocal` is the per-thread *trigger* and where the cost is
paid, but the **fix belongs in `Determinism.h`/`.cpp`**, not here: split `SetupExceptionHandling()`
into a per-thread part (SEH translator) called from the ctor and a one-time process-global part
(`std::call_once` / magic-static) called once. This is the same root cause as `Common Analysis/
Determinism.md` H2 (racy VEH guard) and M5 (per-thread re-install). It should be tracked and fixed
in a single Determinism plan; do not duplicate the implementation here. No code change to
`ThreadLocal.cpp` beyond possibly calling the new split entry points once that plan lands.

## Critical files

- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Threading\ThreadLocal.h` — class contract
  comment, deleted copy/move ctors/assign, backing-vector ordering comment, `LogTickScope` ASSERT.
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Threading\ThreadLocal.cpp` — ctor
  `ASSERT(gpThreadLocal == nullptr);`.
- `C:\Users\dougt\Documents\BrokenEnginePublic\Common\Determinism.cpp` / `Determinism.h` —
  **referenced only**; item 4 is owned by the Determinism plan (see Notes).

## Out of scope

- Item 4 implementation (split / one-time install of process-global handlers): owned by the
  Determinism plan — see Notes.
- M1 (`iWorkbufferSize` default `0`): DROPPED. `0` is a deliberate, in-use pattern for log-only
  threads (Screenshot, eager/lazy load all pass `0`); removing the default is churn for no
  correctness gain and the Workbuffer growth path already `DEBUG_BREAK()`s loudly. Not a bug.
- M2 (cache `mpLogBuffer` vs accessor): DROPPED. Cosmetic; `mLogBufferMemory` is never resized so
  the cache cannot diverge. Replacing it is unrelated refactor.
- M3 (32 KB fixed log buffer per thread): DROPPED. One-time ctor allocation off the main loop;
  "flag only / acceptable as-is" by the report's own admission. KISS.
- L1 (dtor nulls global on non-current instance): DROPPED — subsumed by item 1's invariant.
- L2 (public data members), L3 (mixed access specifiers), L4 (`Threads` enum colocated/`miThreadId`
  typing), L5 (`OutputDebugString` truncation doc nit): DROPPED — style/encapsulation/documentation
  only; data-oriented public bag is intentional, no correctness impact.
- Do not add save/restore (`mpPrior`) nesting support, error handling, validation of ctor
  parameters, or unit tests.

## Acceptance criteria

- A second `ThreadLocal` constructed on a thread that already has one trips the ctor ASSERT in
  debug.
- `ThreadLocal` is non-copyable and non-movable (compile error on any attempt); the 14 existing
  in-place stack-local sites still compile.
- Member-init order in the ctor remains `mLogBufferMemory`, `mWorkbufferMemory` before
  `mpLogBuffer`, `mWorkbuffer`, with a comment marking the dependency.
- Constructing a `LogTickScope` on a thread without a `ThreadLocal` trips an ASSERT rather than null
  deref.

## Notes

- **Determinism overlap (item 4):** the per-thread re-install of process-global exception handlers
  and the racy non-atomic `static bool sbDone` VEH guard are one root cause shared with
  `Common Analysis/Determinism.md` (H2 racy guard, M5 per-thread re-install). The one-time-install
  fix (`std::call_once` / magic-static split of `SetupExceptionHandling`) lives in
  `Determinism.h`/`.cpp` and should be authored/tracked as part of a Determinism plan
  (`Documents/Plans/Common/Determinism.md` if/when created), not implemented inside `ThreadLocal`.
  This plan only notes `ThreadLocal` as the trigger and the place the duplicated-handler cost is
  paid.
- `ConfigureThreadFloatingPoint()` (MXCSR) is correctly per-thread and must stay in the ctor — not
  a finding here.
