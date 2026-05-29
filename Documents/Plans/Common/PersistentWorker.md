# PersistentWorker — Enforce Lifecycle Invariants and Make Shutdown Ordering Explicit

## Context

`Common/Threading/PersistentWorker.h` / `.cpp` is a long-lived single-worker thread driven by a two-`binary_semaphore` (`mWake`/`mDone`) wake/done handshake, with `std::exception_ptr` forwarding across `Wake`/`Wait`. It is owned by:

- `common::Multithreading` worker pool (`Multithreading.cpp:13,23`)
- `engine::StreamingVoices::mFillWorker` (`StreamingVoices.h:49`)
- `engine::CommandBufferManager::mSubmitGlobal` / `mSubmitMain` (`CommandBufferManager.h:22-23`)
- `engine::SwapchainManager::mPresent` (`SwapchainManager.h:40`)

Every owner follows a strict Wake/Wait ping-pong with Wait-before-Wake and Wait-before-destroy. `Engine/Source/Audio/CLAUDE.md` documents the canonical discipline for `mFillWorker`: "Strict alternation: every public method calls `mFillWorker.Wait()` at entry; only `Update` calls `Wake(...)` at exit." Under this discipline `mWake` is provably 0 (no token outstanding) at destruction, and `mWork`/`mException` are touched by only one side at a time, separated by the semaphore's synchronizes-with edges.

The class is **correct today**, but the single-dispatch / Wait-before-destroy precondition is invisible (no assert), and `mShutdown`'s memory ordering is `relaxed` on both sides — correct only *accidentally*, because the semaphore (not the atomic) carries the happens-before edge. This plan locks the invariants in cheaply without changing the (working) protocol. The companion `Common Analysis/Multithreading.md` reaches the same conclusion (shutdown path correct; recommends a documenting comment and asserting the handshake invariant).

Source report: `Common Analysis/PersistentWorker.md`. Several of its High findings were downgraded to Medium during validation because the data-race / `binary_semaphore` over-release (count > `max()` = UB) and exception-drop hazards are reachable **only** under misuse (double-`Wake`, or destruction with an outstanding dispatch) that no current owner performs. The fix is to *assert* those preconditions rather than redesign the channel or add teardown error handling.

## Design

All changes are in `Common/Threading/PersistentWorker.h` / `.cpp`. No protocol change; no behavior change in correct (current) usage.

1. **`Wake` single-dispatch assert** — effort 1
   `Wake` (`PersistentWorker.cpp:40-45`) assigns `mWork`/sets `mbDispatched`/releases `mWake` unconditionally. Add `ASSERT(!mbDispatched);` as the first statement so a double-`Wake` (which would reassign `mWork` while the worker may be invoking it, and push `mWake` past `max()`==1 → UB) trips immediately instead of corrupting the in-flight callable. Matches the codebase posture: "assume parameters valid, but assert invariants" (`Common/CLAUDE.md` Exception Handling — `ASSERT` `DEBUG_BREAK()`s + throws).

2. **`~PersistentWorker` Wait-before-shutdown assert** — effort 1
   The destructor (`PersistentWorker.cpp:33-38`) does `store(mShutdown); mWake.release(); join()`. It implicitly requires the worker to be idle (`mWake`==0, no outstanding dispatch); otherwise the second `release()` would push the count-1 semaphore past `max()` → UB, or the destructor's token could be consumed as work rather than shutdown and the worker could block forever on the next `acquire()`, hanging `join()`. Add `ASSERT(!mbDispatched);` as the first statement to surface a destroy-with-outstanding-dispatch precondition violation. This also makes the M2 "exception silently dropped on teardown" path unreachable (an un-`Wait`ed dispatch at destruction now asserts), so no teardown logging is added (KISS).

3. **Make `mShutdown` ordering explicit (release/acquire)** — effort 1
   Change the store to `std::memory_order_release` (`PersistentWorker.cpp:35`) and the load to `std::memory_order_acquire` (`PersistentWorker.cpp:15`). Cheap on x86; documents the real intent so the shutdown signal carries its own happens-before edge instead of silently riding on the semaphore handshake (refactor-safe). Add a one-line comment at the load and at the declaration (`PersistentWorker.h:23`) noting the `mWake`/`mDone` semaphores already provide the synchronizes-with edge and `mShutdown` must only be read immediately after `mWake.acquire()`.

4. **Explicitly delete copy/move** — effort 1
   The class holds `std::thread`, two `std::binary_semaphore` (non-movable), and `std::move_only_function`, and the worker lambda captures `this` (`PersistentWorker.cpp:7`), baking the object address into the running thread. It is non-copyable/non-movable in practice, but only implicitly via member properties. Add explicit `PersistentWorker(const PersistentWorker&) = delete;` and `PersistentWorker& operator=(const PersistentWorker&) = delete;` (move members are then implicitly not declared) to `PersistentWorker.h` so the `this`-capture safety contract is visible and a future member swap can't silently re-enable a move.

5. **Strengthen the `mThread` "must be last" comment and add a class-level lifecycle contract comment** — effort 1
   Extend `PersistentWorker.h:26` to note the lambda captures `this` and reads all other members, so member declaration order is a correctness requirement. Add a short class-level comment stating the lifecycle contract: strict Wake/Wait ping-pong (one `Wake` per `Wait`), `Wait` before the next `Wake`, `Wait` before destruction, single calling thread.

## Critical files

- `Common/Threading/PersistentWorker.cpp` — `Wake` assert (`:40-45`), destructor assert + `mShutdown` release store (`:33-38`), worker-loop `mShutdown` acquire load + comment (`:15`).
- `Common/Threading/PersistentWorker.h` — copy/move `= delete`, `mShutdown` declaration comment (`:23`), `mThread` comment (`:26`), class-level lifecycle comment.

## Out of scope

- **Splitting shutdown onto a separate signal / redesigning the wake channel** (report M1): YAGNI. Every owner is strict ping-pong; the single-semaphore channel is correct and the asserts enforce the precondition. Do not change the protocol.
- **Teardown exception logging** (report M2): dropped. The destructor assert makes "destroy with outstanding dispatch" a caught precondition violation, so the exception-drop path is unreachable. Adding a drain+`LOG` on teardown contradicts KISS and the assert posture.
- **`THREAD_PRIORITY_TIME_CRITICAL` return-value check / restoration** (report M3): dropped. `Common/CLAUDE.md` documents TIME_CRITICAL as the intentional engine-wide worker priority; the worker blocks on a semaphore when idle. Pure return-value-validation ask, contradicts the documented pattern and the "do not add validation" directive.
- **Adding `#include <thread>/<semaphore>/<atomic>/...` to the header** (report L5): dropped. House style aggregates standard headers in `Common/ExternalHeaders.h` (via `Pch.h`); the report itself concluded no change.
- **`Multithreading.h` `Dispatch` closure / indent-restore / SBO findings**: tracked separately in `Common Analysis/Multithreading.md`; not in this file's scope.
- No formatting, renaming, or restyling of adjacent code.

## Acceptance criteria

- Correct (current) usage is unchanged: a `Wake` followed by `Wait` runs `mWork`, propagates any exception, and leaves `mbDispatched` false; destruction after the final `Wait` sets `mShutdown`, releases one `mWake` token consumed by the idle worker's `acquire()`, and `join()` returns. No new tokens, no new blocking.
- Concurrency reasoning: `mWake`/`mDone` remain count-1 `binary_semaphore`s; with the asserts, no execution path can issue a second `release()` on an already-1 semaphore (`Wake`-while-dispatched and destroy-while-dispatched both assert before the `release()`), so the `max()` precondition (UB) cannot be violated. The `mShutdown` release-store happens-before the worker's acquire-load both via the new release/acquire ordering on the atomic *and* via the `mWake.release()`/`acquire()` edge — the shutdown signal is now carried by the variable that names the condition, independent of the semaphore. `mWork`/`mException` stay non-atomic and safe: each is touched by exactly one side between synchronizes-with edges, an invariant the new asserts enforce.
- `ASSERT(!mbDispatched)` in `Wake` and the destructor fires (DEBUG_BREAK + throw) on double-`Wake` or destroy-with-outstanding-dispatch.
- Copy and move are `= delete`d; any attempt to copy/move `PersistentWorker` is a compile error.
- All four owners (`Multithreading`, `StreamingVoices`, `CommandBufferManager`, `SwapchainManager`) build and run with no assert triggered (they already obey Wait-before-Wake/destroy).

## Notes

- `ASSERT` `DEBUG_BREAK()`s only with a debugger attached and otherwise throws with `std::source_location` (`Common/CLAUDE.md`), so the asserts are zero-cost diagnostics in normal runs, not new error handling.
- `Wait()` keeping its `if (mbDispatched)` no-op early-out is intentional and unchanged (report L3) — it pairs with the new `Wake` assert.
- The `[[unlikely]]` hints on the shutdown and exception branches are fine as-is (report L4); no change.
