<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":[]} -->
# Refactor: Process Failure Propagation

## Context

Source: /external-refactor-clean on `Tools/` recursively. `toolcli::RunProcess` in `Tools/ToolCommon/ToolCliCommon.cpp` converts unexpected subprocess pipe and wait failures into ordinary child results:

- The `ReadFile` capture loop treats every `FALSE` return as EOF. Only `ERROR_BROKEN_PIPE` (child closed its end) is legitimate EOF; any other read error today silently truncates output and falls through to a successful-looking `ProcessResult`.
- The `WaitForSingleObject(hProcess.Get(), INFINITE)` return value is discarded, so a wait failure also falls through to `GetExitCodeProcess` and a normal-looking result.
- `ReportProcessFailure` reads `::GetLastError()` inside the failure-message expression, after evaluating `rOptions.failureSink` and beginning string construction, so the decisive Windows error can be replaced before it is formatted.

This plan makes those failures observable through the existing `failureSink`/`std::nullopt` failure contract without changing any success-path behavior.

## Design

All edits are in `Tools/ToolCommon/ToolCliCommon.cpp`. No signature, header, or caller changes.

### 1. `ReportProcessFailure` — capture the OS error first

Capture `::GetLastError()` into a local `DWORD` as the first statement of `ReportProcessFailure`, before the `rOptions.failureSink` check and before any string construction, and format that local into the failure message. Existing call sites and the message format (`"<operation> failed (Windows error <code>)"`) are unchanged. [~15m]

### 2. `RunProcess` — propagate unexpected read failures

In the `bCaptureOutput` capture loop: when the `ReadFile` loop exits, immediately determine the exit cause via `::GetLastError()` (before any other Win32 call).

- `ERROR_BROKEN_PIPE`: normal EOF — continue exactly as today.
- Any other error: call `ReportProcessFailure(rOptions, ...)` at that point (before closing handles or waiting, so the reported error is the `ReadFile` error), then close the pipe read end (`hPipeRead.Reset()`) so the child cannot remain blocked writing to its merged output pipe, then `::WaitForSingleObject(hProcess.Get(), INFINITE)` so the child is not abandoned, then return `std::nullopt`. [~30m]

### 3. `RunProcess` — propagate wait failures

On the success path, require `::WaitForSingleObject(hProcess.Get(), INFINITE)` to return `WAIT_OBJECT_0`; on any other return value, call `ReportProcessFailure` and return `std::nullopt` instead of proceeding to `GetExitCodeProcess`. The immediate-capture rule from step 1 keeps the reported error accurate at this site too. [included in the ~30m above]

## Critical files

- `Tools/ToolCommon/ToolCliCommon.cpp` — the only file edited: `ReportProcessFailure` (anonymous namespace) and `RunProcess` (capture loop and wait) only.
- `Tools/ToolCommon/ToolCliCommon.h` — read-only reference for `RunProcessOptions`, `ProcessResult`, and the `RunProcess` declaration; no edits.
- Read-only affected-site check (behavior consumers of the `std::nullopt` failure contract; no edits expected): `RunGit` in `ToolCliCommon.cpp`, and `RunBuildProcess` / `RunMsBuildToLog` in `Tools/WorktreeCli/BuildCommand.cpp`.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change implementing the three design steps and nothing else — no new abstractions, options, retries, configuration, or cleanup of adjacent code.

**In scope (only these regions):**
- `ToolCliCommon.cpp` — `ReportProcessFailure` body.
- `ToolCliCommon.cpp` — `RunProcess`: the `bCaptureOutput` read loop's exit handling and the `WaitForSingleObject`/`GetExitCodeProcess` sequence, plus any comment those edits make stale (e.g., the EOF comment above the read loop).
- Mechanical necessities only (no new includes or declarations are expected to be needed).

**Out of scope:**
- Everything else in `ToolCliCommon.cpp`/`ToolCliCommon.h`, including `Handle`, command-line quoting (`QuoteCommandLineArgument`/`BuildCommandLine`), job-object creation/lifetime, pipe creation, `CreateProcessW` launch handling, `RunGit`, and encoding/path helpers.
- Changing output chunk ordering, sink semantics, `ProcessResult` shape, or Git/build policies.
- Refactoring `BuildCommand`'s result schema or AgentHarness sockets.
- Adding retries for opaque process failures.
- Any change to `RunProcess`'s public signature or `RunProcessOptions`.

## Risk tier

Tier 3 trigger: shared ToolCommon process-execution failure contract consumed across WorktreeCli subsystems (build, plan, landing). Invariant exposure is limited to that shared tool contract: no engine CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.

## Acceptance criteria

- An unexpected `ReadFile` failure (anything but `ERROR_BROKEN_PIPE`) and an unexpected `WaitForSingleObject` result (anything but `WAIT_OBJECT_0`) each produce a `failureSink` report and `std::nullopt` — never a successful-looking `ProcessResult` or silently truncated retained log.
- After an unexpected read failure, the pipe read end is closed before waiting on the child, and the child is still waited on before returning.
- Broken-pipe EOF, the zero-byte-read `continue`, merged stdout/stderr ordering, `outputSink` chunk delivery, job-object cleanup, and child exit-code reporting remain byte-for-byte unchanged in behavior.
- Every emitted Windows error code is the one set by the failing API call — captured before any callback, handle close, or string construction can overwrite it.
- WorktreeCli builds via `/compile`; `RunGit`, `RunBuildCommand` callers need no edits (their existing `!result` handling already covers the new failure returns).
