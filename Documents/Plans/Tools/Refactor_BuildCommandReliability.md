# Refactor: Build Command Reliability

## Context
Source: /external-refactor-clean on `Tools/` recursively. WorktreeCli build locking can overshoot configured timeouts, raw Win32 calls fail in deep worktrees despite the shared long-path contract, and helper-process launch details are omitted from the structured result.

## Design

### `Tools/WorktreeCli/BuildCommand.cpp` — `AcquireBuildLock`
- At lines 418-461, bound each sleep to the remaining configured time, stop on elapsed time at or above the limit, and report actual waited time with `steady_clock` or post-sleep accounting while preserving one final acquisition attempt at the deadline and the default 660-second behavior. [~15m]

### `Tools/WorktreeCli/BuildCommand.cpp` — raw Win32 paths
- Preserve display/result paths, but pass `ExtendedLengthPath` to the retained-log `CreateFileW` at line 114, build-lock `CreateFileW` at line 436, and selective-object `DeleteFileW` at line 613 so deep wrapper worktrees follow ToolCommon's documented raw-Win32 path contract. [~15m]

### `Tools/WorktreeCli/BuildCommand.cpp` — `RunBuildProcess`
- Route helper-process failures through `FailBuild` instead of `Fail` at lines 83-90 so vswhere and selective-evaluation OS details enter the result `messages`; remove the unused capture choice because both callers require captured output, while retaining each caller's contextual failure message. [~15m]

## Critical files
- `Tools/WorktreeCli/BuildCommand.cpp`
- WorktreeCli build fixtures

## Out of scope
- Decomposing oversized build functions, changing MSBuild discovery order, or changing the result schema.
- Changing ToolCommon process semantics owned by `Refactor_ProcessFailurePropagation.md`.
- Changing build lock location, serialized ownership, or default deadline.

## Acceptance criteria
- Every accepted `BROKEN_ENGINE_BUILD_LOCK_WAIT_SECONDS` value bounds the wait and `waitedSeconds` reflects elapsed time.
- Retained-log creation, build-lock acquisition, and selected-object deletion work in a path beyond legacy `MAX_PATH`.
- Helper-process launch details appear in the single structured build result without adding stdout records or changing exit codes.

## Notes
- Invariant exposure: serialized build coordination and the `broken-engine-build-result/v1` contract; no engine determinism/CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- Tier 3 trigger: shared build/bootstrap coordination and structured build-result contract.
