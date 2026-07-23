<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":[]} -->
# Refactor: Process Failure Propagation

## Context
Source: /external-refactor-clean on `Tools/` recursively. ToolCommon currently converts unexpected subprocess pipe and wait failures into ordinary child results and can lose the decisive Windows error before formatting it.

## Design

### `Tools/ToolCommon/ToolCliCommon.cpp` — `RunProcess`
- At lines 149-177, treat only `ERROR_BROKEN_PIPE` as normal stdout/stderr EOF, require `WaitForSingleObject` to return `WAIT_OBJECT_0`, and route every other read/wait failure through `ReportProcessFailure` and `std::nullopt`; close the read end before waiting after an unexpected read failure so the child cannot remain blocked on its merged output pipe. [~30m]

### `Tools/ToolCommon/ToolCliCommon.cpp` — `ReportProcessFailure`
- Capture `GetLastError` as the first operation at lines 15-20 and apply the same immediate-capture rule at the new read/wait failure sites, so callbacks and string construction cannot replace the reported OS error. [~15m]

## Critical files
- `Tools/ToolCommon/ToolCliCommon.cpp`
- `Tools/ToolCommon/ToolCliCommon.h`
- Existing WorktreeCli process/build fixtures

## Out of scope
- Changing command-line quoting, process job-object lifetime, output chunk ordering, or Git/build policies.
- Refactoring BuildCommand's result schema or AgentHarness sockets.
- Adding retries for opaque process failures.

## Acceptance criteria
- Unexpected pipe-read and process-wait failures cannot return a successful-looking `ProcessResult` or truncated retained log.
- Broken-pipe EOF, merged stdout/stderr ordering, chunk sinks, job-object cleanup, and child exit codes remain unchanged.
- Emitted Windows error codes describe the failing API call.

## Notes
- Invariant exposure: shared process execution used by WorktreeCli build, plan, and landing workflows; no engine CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- Tier 3 trigger: shared ToolCommon failure contract consumed across WorktreeCli subsystems.
