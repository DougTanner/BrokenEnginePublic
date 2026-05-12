# Refactor: Cross-Process Archetype Mutex

Source: post-audit follow-up from the island bake/export pipeline refactor.

## Context

`gArchetypeMutex` in `BakeIslandIntermediates.cpp` is a `std::mutex` — process-local. Two concurrent DataPacker instances running against the same engine-data tree (two devs sharing a network drive, CI racing a local dev run, a double-launched VS build) will both patch the same archetype `.terrain` in place. The current `ScopedLambda` RAII-restore (lines 295-299) captures byte-and-mtime state at patch time and writes it back unconditionally — there is no cross-process arbitration of the patch+bake+restore critical section.

Failure mode: process A patches archetype with seed Sa → process B reads archetype (now containing seed Sa) and patches with seed Sb → process B's `ScopedLambda` captured A's *patched* bytes as "original" → B restores to Sa instead of the true upstream archetype → A's `ScopedLambda` restores its own (correct) capture. Net result: archetype on disk has process-A's seed baked in permanently, future bakes inherit it, and `git diff` may or may not flag it depending on which captured the *upstream* version. Either way, both bakes' intermediates are correlated to wrong seeds.

The temp-file write path in `WriteFileBytes` (lines 125-135) is also a deterministic collision: both processes target `<archetype>.tmp`, so a concurrent `std::filesystem::rename` will race and one will lose with no diagnostic. Worth fixing because the bake step is the longest-running phase in DataPacker and concurrent runs are the most likely thing a developer does to "work around" the wait.

## Background

- `DataPacker/Source/BakeIslandIntermediates.cpp:115` — `std::mutex gArchetypeMutex;` declared at file scope. Acquired implicitly by `ScopedLambda`-bounded critical section in `BakeOne` (around lines 290-300, archetype read/patch/bake/restore).
- `DataPacker/Source/BakeIslandIntermediates.cpp:125-135` — `WriteFileBytes` writes to `<rFile>.tmp` then `std::filesystem::rename`. Single fixed temp suffix per target path.
- `DataPacker/Source/Main.cpp:783` — existing named-Win32-mutex single-instance guard (`"BrokenEngineDataPacker"`). Demonstrates the in-tree pattern for cross-process Win32 mutex usage including `ERROR_ALREADY_EXISTS` handling and RAII close via `unique_ptr<void, deleter>`.
- `DataPacker/Source/CLAUDE.md` documents the patch+bake+restore mutex as process-local ("under a process-local mutex"). That doc text needs to update once the mutex goes cross-process.

## Proposed Approach

1. Replace `gArchetypeMutex` (`std::mutex`) with a Win32 named-mutex factory keyed off the canonical archetype path:
   - Mutex name: `"Local\\BrokenEngineArchetype-"` + sanitized canonical path (replace path separators / reserved chars with `_` so the name is mutex-name-legal; canonicalize via `std::filesystem::weakly_canonical` before sanitizing so two processes accessing the same archetype via different relative paths converge on the same mutex name).
   - Acquire via `CreateMutexW` + `WaitForSingleObject(INFINITE)`. Release via `ReleaseMutex` + `CloseHandle`. Wrap in a small RAII helper local to this TU (`NamedArchetypeMutex`) following the `Main.cpp:783-791` pattern (one `unique_ptr<void, deleter>` over the handle).
   - Keep the helper one-per-`BakeOne`-call: enter the named mutex, run the patch+bake+restore inside, release on `ScopedLambda` unwind. The named mutex replaces the `std::lock_guard<std::mutex>` that today gates the `ScopedLambda` block.
2. Disambiguate the temp file in `WriteFileBytes`:
   - `tempFile = rFile; tempFile += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";`
   - Concurrent processes that crash mid-write each leave their own orphan `.tmp` instead of clobbering. The successful renamer wins atomically; the loser's rename fails with `ERROR_ALREADY_EXISTS` only if both targeted the *same* final path — which the new cross-process mutex prevents anyway. Belt-and-braces.
3. Update `DataPacker/Source/CLAUDE.md`'s "patch+bake+restore wrapped under a process-local mutex" sentence to reflect the cross-process semantics.

Name interfaces: the change site is the file-scope `gArchetypeMutex` in `BakeIslandIntermediates.cpp` and the `WriteFileBytes` free function in the same TU. The named-mutex RAII helper is new and local to this TU.

## Out of scope

- Promoting the named-mutex helper into `Common/` — only one TU needs it; YAGNI.
- Cross-process arbitration for `.tmp` files outside the archetype write path (`Texture::Save`'s temp-rename pattern, etc.) — those write per-asset paths that don't collide across processes the way archetypes do.
- Adding tests that simulate concurrent processes — the project rule forbids unit tests.
- Replacing the broader `Main.cpp:783` single-instance guard semantics — the existing guard *waits* rather than failing, which is the correct UX; the new mutex sits below it for the per-archetype critical section.

## Acceptance criteria

- Two simultaneous `DataPacker.exe` runs sharing an engine-data tree produce byte-identical archetype `.terrain` files at exit (i.e., the upstream archetype is restored, not a patched one).
- Concurrent crashes during `WriteFileBytes` leave distinct `<archetype>.<pid>.tmp` files rather than overwriting each other.
- `DataPacker/Source/CLAUDE.md` accurately describes the mutex as cross-process.

## Notes

The DataPacker `Main.cpp` already shows the right RAII shape (`CreateMutex` / `__assume` / `unique_ptr<void, deleter>` / `WaitForSingleObject` on `ERROR_ALREADY_EXISTS`). Mirror that shape; don't invent a new one.

Use `Local\` (not `Global\`) namespace for the mutex name — DataPacker runs per-user, no need to coordinate across desktops/sessions.
