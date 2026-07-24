<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":["Documents/Plans/Tools/Refactor_ProcessFailurePropagation.md"]} -->
# Refactor: Build Command Reliability

## Context

Source: /external-refactor-clean on `Tools/` recursively. Three reliability defects live in `Tools/WorktreeCli/BuildCommand.cpp` (line numbers are current-file anchors for the named regions):

1. **Lock wait overshoot and misreported wait time.** `AcquireBuildLock` (lines 418-461) polls with fixed `sleep_for(std::chrono::seconds(5))` steps and a counter loop (`for (int64_t iElapsedSeconds = 0; iElapsedSeconds <= iWaitSeconds; iElapsedSeconds += 5)`). When the configured wait (`BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS`, default `kiBuildLockWaitSeconds` = 660) is not a multiple of 5, the loop sleeps past the limit, then exits without a final acquisition attempt at the deadline; `riWaitedSeconds` reports the loop counter, not time actually waited.
2. **Raw Win32 calls break in deep worktrees.** Three raw Win32 calls pass plain `std::filesystem::path` strings, so they fail beyond legacy `MAX_PATH` in deep wrapper worktrees even though ToolCommon publishes `ExtendedLengthPath` (`Tools/ToolCommon/ToolCliCommon.h:74`) as the documented raw-Win32 path contract: the retained-log `CreateFileW` in `RetainedLog::Open` (line 114), the build-lock `CreateFileW` in `AcquireBuildLock` (line 436), and the selective-object `DeleteFileW` in `InvalidateSelectedObjects` (line 613).
3. **Helper-process launch failures missing from the structured result.** `RunBuildProcess` (lines 83-90) sets `options.failureSink = Fail`, so launch failures from its two callers — vswhere discovery (line 379) and the selective-evaluation MSBuild query (line 507) — reach stderr but never `sBuildMessages`, and therefore never the `messages` array of the single `broken-engine-build-result/v1` stdout object. Its `bCaptureOutput` parameter is a dead choice: both callers pass `true`.

## Scope contract

The listed scope is both target and ceiling. The implementer makes the smallest complete change and adds no abstractions, configuration, refactors, or fixes to adjacent code encountered. Naming a file grants no permission to touch anything in it beyond the regions named below plus the mechanical necessities (includes, declarations) the named changes require.

**In scope** — `Tools/WorktreeCli/BuildCommand.cpp` only:

- `AcquireBuildLock`: the wait loop and its timeout reporting (lines 432-461).
- Exactly three call expressions: `CreateFileW` in `RetainedLog::Open` (line 114), `CreateFileW` in `AcquireBuildLock` (line 436), `DeleteFileW` in `InvalidateSelectedObjects` (line 613).
- `RunBuildProcess` (lines 83-90) and its two call sites (lines 379 and 507).
- `.agents/skills/compile/scripts/Test-BuildResultFixtures.ps1`: run it as verification; edit an existing assertion only if it contradicts the corrected timing or message behavior. Add no new fixture cases.

**Out of scope:**

- Decomposing oversized build functions, changing MSBuild discovery order, or changing the `broken-engine-build-result/v1` schema (no fields added or removed).
- Changing ToolCommon process semantics (`RunProcess`, `RunProcessOptions`) — owned by `Refactor_ProcessFailurePropagation.md`.
- Changing build lock location, serialized ownership, the 5-second poll cadence for waits longer than 5 seconds, or the 660-second default deadline.
- Adding stdout records or changing exit codes.

## Design

### `AcquireBuildLock` — bounded wait with accurate reporting [~15m]

Rework the wait loop (lines 432-461) to be deadline-driven:

- Record a `std::chrono::steady_clock` start time before the first attempt (`<chrono>` is already included; `steady_clock` is already used in `RunBuildCommandUnguarded`).
- Each iteration attempts acquisition, and on a sharing/lock violation sleeps `min(5 seconds, remaining time to the deadline)` — never past the configured limit.
- Stop when elapsed time reaches or exceeds the limit, preserving one final acquisition attempt at the deadline (a wait of 0 still makes exactly one attempt, as today).
- Set `riWaitedSeconds` from actual elapsed `steady_clock` time (both while waiting and on the timeout path), not the loop counter.
- Preserve unchanged: lock path construction, `OPEN_ALWAYS`/hidden-attribute semantics, owner-PID write, disposition strings (`"acquired"`/`"timeout"`/`"failed"`), the non-retryable-error `FailBuildWindows` path, the stderr progress line, and the timeout `FailBuild` message shape.

### Raw Win32 paths — apply `ExtendedLengthPath` [~15m]

Wrap only the argument passed to the Win32 call with `ExtendedLengthPath(...)` at the three named sites (lines 114, 436, 613). Display strings, `result` JSON fields, `mPath`, `rLockPath`, and `rInvalidatedObjects` keep the original non-extended paths — only the raw API call sees the `\\?\` form.

### `RunBuildProcess` — structured failure routing, drop dead parameter [~15m]

- Change `options.failureSink` from `Fail` to `FailBuild` so helper-process launch failures enter `sBuildMessages` and thus the result `messages` array. Each caller's existing contextual failure handling stays as-is.
- Remove the `bCaptureOutput` parameter; set `options.bCaptureOutput = true` unconditionally and drop the `true` argument at both call sites (lines 379 and 507), since both callers require captured output.

## Critical files

- `Tools/WorktreeCli/BuildCommand.cpp`
- `.agents/skills/compile/scripts/Test-BuildResultFixtures.ps1` (fixture verification; includes the `lock-timeout` case with wait shortened to 0)

## Acceptance criteria

- For every accepted `BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS` value (including values not divisible by 5), total wall time waiting for the lock never exceeds the configured value plus one attempt's overhead, one acquisition attempt occurs at the deadline, and the reported `lock.waitedSeconds` reflects actual elapsed time.
- Retained-log creation, build-lock acquisition, and selected-object deletion succeed in a worktree path beyond legacy `MAX_PATH`.
- A helper-process launch failure (vswhere or selective-evaluation query) appears in the `messages` array of the single structured build result, with no new stdout records and unchanged exit codes.
- `Test-BuildResultFixtures.ps1` passes, including the `lock-timeout` fixture.

## Notes

- **Tier 3 trigger:** shared build/bootstrap coordination and the structured build-result contract.
- **Invariant exposure:** serialized build coordination and the `broken-engine-build-result/v1` contract; no engine determinism/CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
