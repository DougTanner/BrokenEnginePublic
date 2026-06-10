# Determinism Handler sMutex Re-entry (formal try_lock UB)

## Context

`common::SetupExceptionHandling` (`Common/Determinism.cpp:17`) installs three fault-path handlers that each guard their body by try-acquiring the file-static `std::mutex sMutex` (`Determinism.cpp:6`):

- the SEH translator lambda passed to `_set_se_translator` — `std::unique_lock<std::mutex> uniqueLock(sMutex, std::try_to_lock)` at `:24`,
- the invalid-parameter handler lambda passed to `_set_invalid_parameter_handler` — same idiom at `:46`,
- the vectored exception handler lambda passed to `AddVectoredExceptionHandler` — same idiom at `:80`.

All three are the "skip if another thread is already mid-handler" guard (`if (!uniqueLock.owns_lock()) return;`). The latch is correct for the cross-thread concurrent-fault case. The defect is the **owned re-entry** case: `std::mutex::try_lock()` invoked by a thread that already holds the mutex is formally **undefined behavior** ([thread.mutex.requirements.mutex] — the calling thread must not own the mutex). That re-entry is reachable on the fault path: a nested fault occurring *inside* one of these handlers while it still holds `sMutex` (e.g. the handler body trips an access violation that re-enters the vectored handler, or the SEH translator's `throw`/formatting path faults) re-runs the same `try_to_lock` construction on the same thread that owns the lock.

This is the **`sMutex` sibling of the `gDbgHelpMutex` decision that landed this session**: the nested-stack-walk corner was already resolved by making `gDbgHelpMutex` a `std::recursive_mutex` (`Determinism.h:46`, "recursive so a fault inside DbgHelp during a walk re-enters instead of deadlocking"; acquired `try_to_lock` by `FilteredStackWalker::ShowCallstack`). The stack-walk path is now safe; the **handler-level `sMutex` still has the same formal hazard** at the three sites above.

Practically benign on MSVC today: `std::mutex::try_lock` is SRWLOCK-backed and an already-owned `TryAcquireSRWLockExclusive` returns `false` (so the owned-re-entry handler simply early-returns, matching the cross-thread intent) rather than corrupting state — but that is an implementation detail, not a standard guarantee. This is therefore **low-priority formal-correctness hardening**, not a live-bug fix.

**Invariant exposure: none.** Fault-path-only. No determinism/CRC/sim-math path, no `Frame::kiVersion` / `.pack` / save-layout, no replay regen, no client/server guard-scope change, no allocation-tracked path. `sMutex` only serializes diagnostic logging/throwing inside the handlers; switching its type changes neither the bytes nor the timing of any sim state.

## Design

Single file (`Common/Determinism.cpp`), a handful of lines. One open decision (pre-staged for grill below).

### Option A (recommended) — `std::recursive_mutex` for `sMutex`

Change `static std::mutex sMutex;` (`:6`) to `static std::recursive_mutex sMutex;`, mirroring the `gDbgHelpMutex` decision exactly. The three `std::unique_lock<std::mutex>(sMutex, std::try_to_lock)` constructions become `std::unique_lock<std::recursive_mutex>(sMutex, std::try_to_lock)` (or a `using` alias / `decltype(sMutex)` to keep the three sites in lockstep). `recursive_mutex::try_lock` is **defined** for the owning thread — it succeeds and bumps the ownership count.

Semantic note to confirm at grill: with a recursive mutex, the owned-re-entry case now *acquires* (returns `owns_lock() == true`) and runs the nested handler body, instead of early-returning. That matches `gDbgHelpMutex`'s "re-enter instead of deadlock" intent and is the safer behavior for a nested fault (the inner handler gets to log/break). If, instead, the desired behavior is "owned re-entry must still bail" (don't re-run a handler from within itself), keep the recursive mutex for the UB fix but add an explicit owning-thread / re-entry-depth check to early-return — note this in the plan only if grill picks it; default is plain recursive-acquire, mirroring `gDbgHelpMutex`.

### Option B — Documented accept-as-is

Leave `std::mutex`. Add a comment at the `sMutex` declaration (`:6`) recording that owned re-entry is formal UB but MSVC's SRWLOCK-backed `try_lock` returns `false` on an already-owned lock (benign early-return), and that the cross-thread guard is the only intended use. This is the do-nothing-but-document close; pick it only if the team prefers not to alter the fault-path locking primitive for a hazard with no observed manifestation.

Recommended: **Option A** — it removes the formal UB outright, is a one-type-change-plus-three-template-arg edit, and keeps `sMutex` consistent with the just-landed `gDbgHelpMutex` recursive/try_to_lock posture (one less "why is this one non-recursive?" surprise).

## Critical files

- `Common/Determinism.cpp` — the only file edited. `sMutex` declaration at `:6`; the three guard sites inside the lambdas installed by `SetupExceptionHandling` (`:17`): `_set_se_translator` (`:24`), `_set_invalid_parameter_handler` (`:46`), `AddVectoredExceptionHandler` (`:80`). Header-only `std::recursive_mutex` swap (Option A) or comment-only (Option B); no `.cpp`/`.h` interface change, no new symbol, no vcxproj change.
- `Common/Determinism.h` — read-only reference: `gDbgHelpMutex` (`:46`) is the recursive-mutex precedent whose rationale comment Option A mirrors. `SetupExceptionHandling` declaration (`:39`). No edit unless `sMutex` is ever promoted to an `extern` (it is not — file-static, no change).
- `Common/CLAUDE.md` — the "Exception Handling" section describes the handler install and the `gDbgHelpMutex` serialization; if Option A lands, a one-line note that the handler-level `sMutex` is likewise recursive (via `update-claude-docs` in the code-change process). No edit for Option B beyond the in-file comment.

## Out of scope

- **`gDbgHelpMutex` and the stack-walk path** (`StackWalker.h` / `FilteredStackWalker::ShowCallstack`). Already recursive/try_to_lock this session — not re-touched. This plan is only the handler-level `sMutex`.
- **The handler logic itself** — the `throw`/`sprintf_s` fixed-buffer formatting, `DEBUG_BREAK()` placement, exception-code switch, `LogStackWalker` call, `std::call_once` process-global install. No behavior change; only the guard mutex type (Option A) or a comment (Option B).
- **The cross-thread concurrent-fault latch semantics.** The "skip if another thread is mid-handler" behavior stays; this plan fixes only the *same-thread owned* re-entry UB, not the cross-thread serialization design.
- **Any determinism / CRC / sim-math / `kiVersion` / `.pack` / replay change** — none applies (fault-path-only, see Context).
- **Auditing other `std::mutex` + `try_to_lock` sites** elsewhere in the codebase for the same owned-re-entry pattern — a separate sweep if wanted; this plan fixes only the three Determinism.cpp handler sites.

## Acceptance criteria

- No `std::mutex::try_lock` (via `std::try_to_lock`) on `sMutex` remains reachable by a thread that may already own it — satisfied by Option A's `std::recursive_mutex` (defined for the owner) or, under Option B, by the documented-accept comment recording the MSVC-benign rationale.
- If Option A: `sMutex` is `std::recursive_mutex`; all three guard sites construct their `unique_lock` over that type and still compile in both client and server (Determinism.cpp is shared/ungated). The chosen owned-re-entry semantics (plain recursive-acquire vs explicit bail) match the grill decision.
- No new public symbol, no header interface change, no vcxproj edit.

## Notes

- **Single grill decision:** Option A (`std::recursive_mutex` for `sMutex`, mirroring `gDbgHelpMutex`) vs Option B (documented accept-as-is, keep `std::mutex`). Sub-point if A: default to plain recursive-acquire on owned re-entry (matches `gDbgHelpMutex`); only add an explicit re-entry-bail if the team wants "a handler must not re-run itself."
- **Precedent to mirror:** `gDbgHelpMutex` (`Determinism.h:46`) — same file pair, same try_to_lock guard, the recursive-mutex decision already taken for the stack-walk re-entry corner this session. Option A is the consistent extension of that decision to the handler-level mutex.
- **Why low priority / how it scored:** formal-UB-only ([thread.mutex.requirements.mutex]), MSVC-benign today (SRWLOCK `try_lock` returns `false` on an owned lock), reachable only on the nested-fault path, single file, handful of lines, no invariant exposure. Quick Win effort; cosmetic/formal-correctness impact; mechanical-and-narrow risk. Same accepted-as-benign class as the rest of the fault-path hardening.
