# Exception-Safe Fork/Join in `Multithreading::Dispatch`

## Context

`common::Multithreading::Dispatch` (`Common/Threading/Multithreading.h:22-70`) is the fork/join worker pool. It wakes one `PersistentWorker` per chunk, runs the remainder chunk on the calling (main) thread, then joins every worker via `PersistentWorker::Wait`. The wake/wait handshake itself (a pair of `std::binary_semaphore` `mWake`/`mDone` per worker, plus `mbDispatched`/`mException`/`mShutdown` published across the semaphore release/acquire edge) is sound — the binary semaphore provides the happens-before edge and there is no lost-wakeup hazard. The bug is that the **join is not exception-safe**: any throw out of a worker chunk *or* the main-thread chunk abandons still-running workers that hold live references into the caller's stack frame.

The worker chunk runs the caller's `processRange` (a lambda capturing caller state by reference). All three real call sites pass a named `processRange` with a half-open range `(int64_t iBegin, int64_t iEnd)` signature: `Engine/Source/GameBase.cpp:191-200` (loops `game::RunFrameTick`), `Projects/.../Spaceships/SpaceshipsRender.cpp:181`, `Projects/.../Network/Client/ClientReconciler.cpp:97-106` (loops `ReconcileCoord`). Neither `RunFrameTick` nor `ReconcileCoord` is declared `noexcept`. The engine promotes SEH faults into C++ exceptions via `SetupExceptionHandling`, installed by every `ThreadLocal` ctor including each worker's (`ThreadLocal.cpp:17-20`, `PersistentWorker.cpp:10`), so a fault inside a worker chunk *does* surface as a throwable exception that the worker captures into `mException` (`PersistentWorker.cpp:23-26`) and `Wait` rethrows (`PersistentWorker.cpp:54-58`). The exception path is therefore reachable in practice, not merely theoretical.

This plan covers the three findings from `Common Analysis/Multithreading.md` that survived validation against source (C1 Critical, H1 High, H2 High). The remaining report items were dropped (see Out of scope).

## Design

Make the entire dispatch region exception-safe so that **no worker is still touching the caller's stack when `Dispatch` unwinds**, and so the `mWake`/`mDone` token counts stay balanced for the life of the process.

### C1 + H1 — drain-all join, single rethrow (the main-thread chunk + worker-join loop in `Multithreading::Dispatch`, `Multithreading.h:59-69`) — effort: 2

Current code:
- `Multithreading.h:60-63` runs the main-thread chunk `processRange(iPos, iCount)` *before* the join. A throw here exits `Dispatch` immediately, skipping the wait loop entirely. Every woken worker keeps `mbDispatched == true`, may still be executing `mWork()` against now-destroyed caller stack, and will later `mDone.release()` a token that the *next* `Dispatch`'s `Wait` consumes prematurely (silent early-join reading partial results).
- `Multithreading.h:66-69` joins via `for (... pWorker : mWorkers) pWorker->Wait();`. `Wait` rethrows on the *first* faulting worker, so workers after it are never waited — same abandonment + stale-`mDone`-token hazard, plus any later worker's `mException` is silently lost.

Fix — collapse both into one structure:
1. Wrap the main-thread chunk (`Multithreading.h:60-63`) in `try { ... } catch (...) { capture first exception; }` so a main-chunk throw still falls through to the join.
2. Replace the join loop with a drain-all loop that `try`s each `pWorker->Wait()` and captures the **first** `std::exception_ptr` via `std::current_exception()` without breaking, so every worker is waited (every `mDone` token consumed, every `mbDispatched` cleared, every `mException` observed-and-cleared).
3. After the loop, if a captured exception exists, `std::rethrow_exception(first)`.

Net invariants restored: (a) `Dispatch` never returns or unwinds while a worker chunk is still running, eliminating the use-after-scope/data race; (b) one `mWake` release pairs with exactly one `mDone` acquire per dispatch for every worker, eliminating cross-`Dispatch` token desync; (c) a worker exception still propagates (first one wins; the main-chunk exception participates in the same "first wins" capture).

`Wait` is already a no-op for never-dispatched workers (the `if (mbDispatched)` guard, `PersistentWorker.cpp:49`), so the drain-all loop is safe to run over all `mWorkers` regardless of how many were woken.

### H2 — RAII-restore the worker log indent (the worker lambda body in `Multithreading::Dispatch`, `Multithreading.h:48-55`) — effort: 1

The worker lambda manually does `iPriorIndent = gpThreadLocal->miLogIndent; gpThreadLocal->miLogIndent = iLogIndent; processRange(...); gpThreadLocal->miLogIndent = iPriorIndent;`. If `processRange` throws, line 54's restore is skipped; since the worker is persistent and reused, the corrupted indent leaks into every subsequent dispatch on that worker. `LogTickScope` (line 50) is already RAII and unwinds correctly — only the manual indent save/restore leaks. The worker's `catch` (`PersistentWorker.cpp:23-26`) means the lambda's stack unwinds, so a scope guard *will* fire.

Fix — replace the manual save/restore with an RAII guard so the restore runs on the throw path. Mirror the existing `LogTickScope` shape (preferred, consistent with the file) by adding a sibling `LogIndentScope(int64_t)` in `Common/Threading/ThreadLocal.h` that snapshots/sets `gpThreadLocal->miLogIndent` in its ctor and restores in its dtor; or use `common::ScopedLambda` (per `Common/CLAUDE.md`). Prefer the `LogIndentScope` form for symmetry with `LogTickScope`. Add a one-line comment that the tick/indent are propagated so worker logs tag under the caller's tick/indent (folds in report L3).

## Critical files

- `Common/Threading/Multithreading.h` — the `Dispatch` template: worker lambda (`:48-55`), main-thread chunk (`:60-63`), join loop (`:66-69`). All behavior edits land here except the optional new guard type.
- `Common/Threading/ThreadLocal.h` — add `LogIndentScope` beside `LogTickScope` (`:56-77`) if that approach is chosen for H2.
- `Common/Threading/PersistentWorker.cpp` — reference only (no change): confirms `Wait` rethrow semantics (`:47-60`), worker exception capture (`:23-26`), and the `mbDispatched` no-op guard relied on by the drain-all loop (`:49`).

## Out of scope

- **Memory-ordering of `mShutdown`** (report H3): the relaxed load/store is already correct — the `mWake`/`mDone` semaphore handshake supplies the happens-before edge. No change; do not switch to `seq_cst` or add asserts.
- **`std::move_only_function` SBO / heap-allocation on the dispatch path** (report M1): speculative; no observed allocation-tracker break. Do not restructure the closure or add `PersistentWorker` members.
- **Single-driver / reentrancy `ASSERT(IsMainThread())`** (report M3): defensive caller-contract validation; CLAUDE.md says assume params valid. Not added.
- **Work-partition balancing / main-thread remainder share** (report M2): functionally fine; no change.
- **`WorkerCount() + 1` DRY nit** (M4), **`mWork = nullptr` reset / `mThread`-last comment** (L5/L6), **ctor de-duplication / singleton asymmetry** (L4): style/comment-only; leave to style review.
- **`Dispatch` signature `FUNC&` vs `FUNC&&` and the stale `(int64_t i)` style-guide example** (L1/L2): real but Low and doc/API-shape — see Notes; not changed under this concurrency-fix plan.

## Acceptance criteria

- A throw from any single worker chunk leaves **all** workers waited (all `mDone` tokens consumed, all `mbDispatched` cleared) before `Dispatch` unwinds, and the first such exception propagates out of `Dispatch`. Correctness argued by inspection: the drain-all loop has no early `break`/`return`; the only path out is after every `Wait` has run, then a single `rethrow_exception`.
- A throw from the main-thread chunk likewise joins all workers before propagating (the `try/catch` around the main chunk falls through to the same drain-all loop).
- After any throwing dispatch, a subsequent `Dispatch` waits correctly (no premature join from a stale `mDone` token) — argued by the restored one-`mWake`-↔-one-`mDone`-per-worker invariant; spot-checkable by confirming each woken worker's `mDone` is acquired exactly once per dispatch in the loop.
- A throw from a worker chunk leaves that worker's `gpThreadLocal->miLogIndent` restored to its pre-dispatch value (RAII guard fires during the worker's `catch` unwind). Verifiable by reasoning: the restore is in a destructor on the lambda's stack, which unwinds before the `catch` at `PersistentWorker.cpp:23-26` captures the exception.
- Happy-path behavior (no throw) is unchanged: same partitioning, same indent/tick propagation, same join order.

## Notes

- The concurrency-correctness argument rests on the existing, validated handshake: `mWake.release()` synchronizes-with the worker's `mWake.acquire()`, and the worker's `mDone.release()` synchronizes-with `Wait`'s `mDone.acquire()`; the fix only changes control flow on the throw path, never the handshake itself.
- The first-exception-wins choice matches `Wait`'s existing single-rethrow behavior and avoids aggregating multiple exceptions (KISS); later workers' exceptions are still observed-and-cleared (so they cannot corrupt a future dispatch), just not rethrown.
- Report L1/L2 (signature `FUNC&` rejects inline rvalue lambdas; style-guide rule 51 at `Documents/C++StyleGuide.txt:214` shows a per-item `(int64_t i)` callback while the real contract is a half-open range `(iBegin, iEnd)`) are genuine but Low/doc-shape and deliberately deferred; if later actioned, change to `FUNC&&` and update rule 51 to the range form together.
