# CrashReport DxDiag String Data Race

## Context

`engine::HandleException` (`Engine/Source/CrashReport.cpp:10`) reads the file-static `static std::string sDxDiag` (`CrashReport.cpp:8`) on the main thread — `ofstream << sDxDiag;` at `CrashReport.cpp:64` — while the asynchronous `engine::ReadDxDiag` task (`CrashReport.cpp:72`) concurrently *writes* it via repeated `sDxDiag += ...` (`CrashReport.cpp:127-130`, four appends per property → reallocations).

The writer is launched as a detached-by-future task in `MainThread`: `readDxDiag = std::async(std::launch::async, ReadDxDiag);` (`Engine/Source/Main.cpp:96`), with the `std::future<void> readDxDiag;` declared local to `MainThread` (`Main.cpp:91`). That future is **never `.get()`-ed** — it only joins in its destructor when `MainThread` returns, i.e. after the entire boot + main-loop window. So there is no happens-before edge between the writer's appends and the reader for the whole life of the process up to `MainThread` exit.

`HandleException` is reachable from the main loop's catch handlers (`Main.cpp:658` / `Main.cpp:662`) and from the SIGABRT path (per `Engine/Source/CLAUDE.md` "Crash Reporting": the handler is reachable from `SIGABRT` during heap corruption and must not re-enter the allocator). A concurrent read of a `std::string` mid-reallocation is a **use-after-free / formal UB** — strictly worse than a scalar tear: the capacity buffer the reader iterates can be freed and reused by the writer's growth.

**Current mitigations (why this is latent, not live):** (1) the crash-during-DxDiag-read window is small and unlikely; (2) DxDiag is skipped entirely under a debugger — `ReadDxDiag` early-returns on `IsDebuggerPresent()` (`CrashReport.cpp:74`) and the launch is gated the same way (`Main.cpp:94`).

**Invariant exposure: none.** This is diagnostics / crash-report only. No determinism / CRC sim path, no `kiVersion` / `.pack` layout, no replays, no network. The fix touches only the crash-report subsystem and the DxDiag launch site. It is not allocation-tracked sim code (startup/teardown allocate freely per the Engine hub), but the crash-handler-side read constraint below is hard.

## Design

This is **not** an atomic-flag fix on the string itself — adding `std::atomic` around `sDxDiag` does not make the string's contents safe to read mid-reallocation. The fix shape must establish a real **happens-before** edge (or eliminate concurrent access) between the writer's completion and any read by `HandleException`.

**Hard constraint (from `Engine/Source/CLAUDE.md`):** `HandleException` is reachable from `SIGABRT` during heap corruption and must not re-enter the allocator. So the publish must allocate on the async thread *before* the crash; the handler may only read an already-built, already-published buffer. The handler must also never *block* on a lock the faulting thread might hold.

Three candidate fix shapes (decision pre-staged for `/external-grill-plan`, see Notes):

- **(a) Join the future before entering the main loop.** Add `readDxDiag.get()` (or `.wait()`) after boot, before the main loop. Simplest correctness, but serializes boot behind the DxDiag duration (DxDiag enumeration can be slow) — probably unacceptable for boot latency. Rejected unless grill says boot delay is fine.

- **(b) Mutex + ready atomic.** Guard `sDxDiag` with a `std::mutex`; `ReadDxDiag` locks while appending (or builds locally and locks once to swap in), sets a `std::atomic<bool> sbDxDiagReady` on completion. `HandleException` checks the ready flag and, if set, `try_lock`s — **never a blocking lock** (the faulting thread could hold it). On `try_lock` failure or not-ready, it writes a placeholder (`<DxDiag unavailable>`). Correct, but the crash handler taking *any* lock under heap corruption is delicate.

- **(c) Build-local, publish via atomic pointer/flag swap (recommended pre-stage).** `ReadDxDiag` builds the full text into a local `std::string`, then publishes a single atomic handoff at the end — e.g. move into a heap-owned buffer and `std::atomic<const char*>`-store a raw `c_str()`-stable pointer (plus an atomic length), or store into a leaked/static-lifetime `std::string*` via `std::atomic<std::string*>`. `HandleException` does a single relaxed/acquire atomic load: null → write placeholder; non-null → stream the already-complete, never-again-mutated buffer. Lock-free read, single-publish, no reallocation visible to the reader, and the allocation happens entirely on the async thread before any crash. The published buffer must have static/leaked lifetime so the pointer stays valid through process exit (the async thread does not outlive `MainThread`, but the buffer it published must). This satisfies the no-allocator-reentry constraint exactly.

All three leave the `IsDebuggerPresent` skip and the `Main.cpp:94` launch gate intact.

## Critical files

- `Engine/Source/CrashReport.cpp` — the `sDxDiag` file-static (`:8`), its read in `HandleException` (`:64`), and its writes in `ReadDxDiag` (`:127-130`). Primary edit site for whichever shape is chosen (publish mechanism + handler-side read/placeholder).
- `Engine/Source/Main.cpp` — the `readDxDiag` future declaration (`:91`) and `std::async` launch (`:96`); only touched if shape (a) is chosen (add `.get()`/`.wait()` before the main loop) or if (c)'s publication needs a launch-site change. Shapes (b)/(c) localize entirely within `CrashReport.cpp`.
- `Engine/Source/CrashReport.h` — only if the publish mechanism needs a new declaration (unlikely; the file-static and helpers are TU-local).
- `Engine/Source/CLAUDE.md` "Crash Reporting" — add a one-line note recording the established happens-before / lock-free-read contract once the shape lands.

## Out of scope

- Reworking *what* DxDiag collects, its format, or the four-append-per-property loop's allocation pattern (other than as incidental to the publish-by-local-build in shape (c)).
- The other crash-report payloads (`OfstreamStackWalker`, `LogDumpBuffers`, the fixed-wchar path buffers) — those are not shared with the async writer and are out of scope.
- The `IsDebuggerPresent` skip behavior and the single-instance / RO_INIT boot logic.
- Any change to the `Main.cpp` catch-handler structure (`:646-664`) or the SIGABRT registration.
- The broader publish-global singleton guard work (`Engine/SingletonPublishGlobalGuardSweep.md`) — disjoint concern; this plan adds no `gp*` publish guard.
- No `kiVersion` / `.pack` / CRC / replay / network changes (there are none here).

## Notes

- **Pre-staged decision for `/external-grill-plan`:** pick the fix shape — (a) join-before-main-loop (simplest, rejected if boot latency matters), (b) mutex + ready-atomic with crash-handler `try_lock`, or (c) build-local + atomic-pointer single-publish (recommended). Recommend (c): lock-free read, single publish, allocation happens on the async thread before any crash, satisfies the `HandleException`-must-not-re-enter-the-allocator constraint, and the handler never blocks on a lock the faulting thread could hold. Confirm the published buffer's lifetime strategy (leaked static `std::string*` vs `std::atomic<const char*>` to a static-lifetime buffer) and the placeholder text for the not-yet-ready / crash-before-DxDiag case.
- **Crash-handler constraint (hard):** whatever shape lands, `HandleException`'s DxDiag read must be allocation-free and non-blocking — it is reachable from `SIGABRT` during heap corruption (`Engine/Source/CLAUDE.md`). Shape (c)'s atomic-pointer load is a single non-allocating, non-blocking read; shape (b) must use `try_lock`, never a blocking lock.
- This is a latent UB fix in the crash path with low practical reach today (small window + debugger skip), but it is genuine use-after-free, not a benign scalar tear — worth fixing loud rather than documenting.
