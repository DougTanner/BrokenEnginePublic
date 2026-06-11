# Architecture: Doc Accuracy

## Context

Source: /external-architecture-review on `Engine/Source/Profile`. The subsystem CLAUDE.md makes three claims the code contradicts, and one undocumented design intent belongs in the header. All items are doc/comment-only.

## Design

### Engine/Source/Profile/CLAUDE.md
- `:9` "All entry points are wrapped in `if constexpr (kbProfiling)`" — overstated: `TickVisibilityCadence()` (`ProfileManagerBase.cpp:83-93`), the free formatters (`ProfileScreens.cpp:14`, `:61`), and the virtual accessors (`ProfileManagerBase.cpp:435-453`) are deliberately unwrapped (the server Profile tab renders zeros when profiling is off, per `Server/CLAUDE.md`). Reword to "all recording/update entry points". [~3m]
- `:21` "Map resize wraps in `ScopedSuppressAllocationTracking` so profiling never pollutes its own counts" — incorrect rationale: `giAllocationsThisFrame` increments *before* the suppression check (`MemoryManager.cpp:12-26`); suppression only silences the `DEBUG_BREAK` tripwire. The first-use map setup does land in the counter (the snapshot at `ProfileManagerBase.cpp:118` precedes the allocation). Reword to "…so profiling never trips the allocation tripwire". [~3m]
- `:23` "Each CPU timer reports the heap allocations that occurred inside its scope" — imprecise: it is process-wide allocations during the scope's wall-clock window (global atomic, all threads). Add the clarifying phrase. [~2m]

### Engine/Source/Profile/ProfileManagerBase.h
- `mCpuTimerMutex` (`:337`): add a comment stating the intended granularity — one global mutex serializes all CPU timers across dispatch workers; designed for coarse phase scopes, not per-entity timers (finer granularity would distort the measurements it takes). [~3m]

## Critical files
- Engine/Source/Profile/CLAUDE.md
- Engine/Source/Profile/ProfileManagerBase.h

## Out of scope
- CLAUDE.md `:25` (`bSmoothNow` latch description) — becomes accurate when `Architecture_SmoothNowDoubleLatch.md` lands (fix the code, not the doc).
- CLAUDE.md `:11` (server `SmoothCpuTimers` contract) — rewritten by `Architecture_ClientServerGuardScope.md`.
- CLAUDE.md `:37` (`Game.h` include claim) — fixed by `Architecture_IncludeHygiene.md`.
- Wrapping `TickVisibilityCadence` in `kbProfiling` — rejected (KISS): five lines of steady_clock arithmetic, called only from already-gated contexts; fix the doc claim instead.

## Notes
- Doc/comment-only; zero behavior change; no invariant exposure.
- Land last in a co-scheduled Profile session (it cites line numbers the code plans move).

## Verification Notes
All items verified against source (2026-06-10 pass):
- CLAUDE.md line numbers exact (`:9`, `:21`, `:23`); the unwrapped entry points confirmed (`TickVisibilityCadence` `ProfileManagerBase.cpp:83-93`, formatters `ProfileScreens.cpp:14`/`:61`, virtual accessors `:435-453`), and the Server CLAUDE.md corroborates the renders-zeros-when-off intent.
- The `:21` rationale fix is correct: `MemoryManager.cpp` increments `giAllocationsThisFrame` at `:16` *before* the suppression check at `:19` — suppression only silences the `DEBUG_BREAK` tripwire. The aside also holds: `CpuStart`'s counter snapshot (`:118`) precedes the suppressed map setup (`:125`), so those allocations land in that timer's diff.
- The `:23` clarification is accurate — the counter is one process-wide atomic; `CpuStop` diffs wall-clock-window snapshots, so all threads' allocations during the window are attributed.
- No duplication with `Common/StaleDocClaimsSweep.md`: its Profile item (#6) targets the different `:35` "required position" sentence in the same file — co-schedule if both land in one session, and keep it consistent with `Architecture_InvariantHardening.md`'s position-pinning `static_assert`.
- Cross-references to the other three Profile plans verified consistent (each cited item is genuinely owned by the named plan).
- No caveats.
