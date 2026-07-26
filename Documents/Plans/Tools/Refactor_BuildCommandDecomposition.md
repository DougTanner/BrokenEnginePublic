<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":["Documents/Plans/Tools/Refactor_BuildCommandReliability.md"]} -->
# Refactor: Build Command Decomposition

## Context
Source: /external-refactor-clean on `Tools/` recursively. Two functions in `Tools/WorktreeCli/BuildCommand.cpp` exceed the 1,000 bt-token-v1 decomposition threshold and mix concerns: `InvalidateSelectedObjects` (1,236 bt-token-v1, lines 494-622) combines opaque MSBuild project-evaluation decoding with destructive object deletion, and `RunBuildCommandUnguarded` (1,643 bt-token-v1, lines 625-798) combines result-schema construction with the whole execution pipeline while `EmitFallbackBuildResult` (lines 802-825) duplicates that schema field-for-field. This plan is pure decomposition into file-local helpers with identical observable behavior — same stdout JSON bytes (modulo timestamps/elapsed), same stderr lines, same exit codes, same retained-log content and ordering.

Line numbers are as of this plan's creation. The dependency plan `Refactor_BuildCommandReliability.md` edits the same file first (it touches lines 83-90, 114, 418-461, 436, 613), so locate every region by function name; the cited ranges are anchors, not authorities.

## Scope contract
The listed scope is both target and ceiling: make the smallest complete change specified in Design and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (declarations, hoisted types, includes) the named changes require.

In scope — `Tools/WorktreeCli/BuildCommand.cpp` only, these regions:
- `InvalidateSelectedObjects` (lines 494-622), including hoisting its local `struct BuildItem` (lines 518-522) to file scope.
- `RunBuildCommandUnguarded` (lines 625-798), including its `EmitResult` (lines 647-654) and `FinalizeStreams` (lines 743-749) lambdas.
- `EmitFallbackBuildResult` (lines 802-825).
- The new file-local (anonymous-namespace or `static`) helpers these extractions produce, placed in the same file.

Out of scope:
- Changing build command syntax, MSBuild invocation or discovery, object invalidation policy, `broken-engine-build-result/v1` fields, exit codes, or output channels.
- Rewriting the argument-parsing range of `RunBuildCommandUnguarded` (the `--files`/target/passthrough parse, lines 656-683). This plan may move that range verbatim only as far as the top-level parse/validate/execute/emit shape requires, changing none of its logic or messages.
- Build timeout, long-path, and subprocess-failure behavior owned by `Refactor_BuildCommandReliability.md` and `Refactor_ProcessFailurePropagation.md`.
- Generalizing the new helpers outside `BuildCommand.cpp`: no moves into `Tools/ToolCommon`, and `Tools/WorktreeCli/BuildCommand.h` keeps `RunBuildCommand` as the sole declaration.
- Every other function in the file — `RunBuildCommand` (lines 828-842), `ComparablePath`, `RetainedLog`, `DiagnosticParser`, `AcquireBuildLock`, `RunBuildProcess`, `RunMsBuildToLog`, `FailBuild`/`FailBuildWindows`, and the rest — except for call-site updates the named extractions require.

## Design

### 1. `InvalidateSelectedObjects` split — evaluation decode out, destructive work stays
- Hoist `BuildItem` (`source`/`object` paths) to a file-local value type.
- Extract lines 502-591 into one value-returning file-local helper (suggested name `EvaluateProjectCompileItems`): build the `/getProperty:IntDir` + `/getItem:ClCompile` + `/nologo` query arguments, run `RunBuildProcess`, write captured output to the `RetainedLog` before the exit-code check (preserving current write-then-check ordering at lines 508-516), parse and shape-validate the JSON, and return the evaluated intermediate directory, the normalized comparable IntDir prefix (lowercased, backslash-terminated), and the `std::unordered_map<std::wstring, BuildItem>` keyed by `ComparablePath(source)`. On any failure the helper calls the existing `FailBuild` with the byte-identical current strings ("MSBuild project evaluation failed", "MSBuild evaluation omitted or malformed IntDir or ClCompile", "invalid evaluated IntDir", "could not read MSBuild evaluation: ...") and returns an empty optional.
- `InvalidateSelectedObjects` keeps: the Configuration/Platform precheck (lines 496-500) and the per-selected-file validation/deletion loop (lines 593-620) — `.cpp` filter, membership lookup, IntDir-escape check, `DeleteFileW`, the `WorktreeCli: invalidated` stderr line, and the `invalidatedObjects` append — with all current failure strings unchanged. [~1h]

### 2. One result-schema construction path shared with the fallback
- Introduce a file-local `NewBuildResult()` returning the `nlohmann::json` with the common field set currently duplicated between `RunBuildCommandUnguarded` lines 630-644 and `EmitFallbackBuildResult` lines 805-821 (`schemaVersion` through `diagnosticsTruncated`, identical initial values).
- Introduce one file-local emission helper that sets `exitCode`, `elapsedMilliseconds`, and `messages` (from `sBuildMessages`) and prints the single-line `dump(-1, ' ', false, error_handler_t::replace)` record to stdout; route both the `EmitResult` path and `EmitFallbackBuildResult` through it. The fallback keeps its exact current output: `elapsedMilliseconds` 0, `exitCode` `kiExitFailure`, and its own message appended after `sBuildMessages`. [~30m]

### 3. `RunBuildCommandUnguarded` execution-stage extraction
- After the parse range (lines 656-683) and the result-population/target-validation/discovery block (lines 685-721), extract the span from retained-log open (line 723) through the final MSBuild verdict (line 797) into one or two file-local helpers that carry a small file-local execution-state struct (target, build arguments, selected files, worktree root, MSBuild path, retained log, diagnostic parser, lock fields, and the result JSON reference) instead of a long parameter list. `FinalizeStreams` becomes a member or helper of that state with unchanged behavior and call points (after invalidation failure, after `RunMsBuildToLog`).
- The remaining top level of `RunBuildCommandUnguarded` reads as: construct result, parse, validate/discover, execute, emit. No stage reorders; every early-exit still emits exactly one JSON result. [~1h]

## Critical files
- `Tools/WorktreeCli/BuildCommand.cpp` — all edits.
- `Tools/WorktreeCli/BuildCommand.h` — expected unchanged; listed because the public entry contract lives here.

## Acceptance criteria
- No changed function exceeds approximately 1,000 bt-token-v1 (`.agents/scripts/Measure-Tokens.ps1`) or more than three meaningful nesting levels.
- Normal and exception-fallback results share one schema-construction path and one emission path.
- Behavior parity: selective invalidation semantics, retained-log write ordering, exactly-one-JSON stdout, every failure/message string, stderr progress lines, and exit codes are byte/behavior compatible. Decisive check: WorktreeCli compiles, and a before/after `WorktreeCli build` run of the same target produces identical JSON apart from `startedAt`, `elapsedMilliseconds`, `waitedSeconds`, and retained-log path/bytes.

## Notes
- Invariant exposure: `broken-engine-build-result/v1`, selective object invalidation, and shared build coordination; no engine CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- Tier 3 trigger: build/bootstrap contract (`/compile` and every session's builds flow through this command).
