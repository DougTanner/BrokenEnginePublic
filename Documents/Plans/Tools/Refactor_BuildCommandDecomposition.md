<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":["Documents/Plans/Tools/Refactor_BuildCommandReliability.md"]} -->
# Refactor: Build Command Decomposition

## Context
Source: /external-refactor-clean on `Tools/` recursively. Two BuildCommand functions exceed the 1,000 bt-token-v1 threshold and combine opaque MSBuild decoding or result-schema construction with destructive/execution work.

## Design

### `Tools/WorktreeCli/BuildCommand.cpp` — `InvalidateSelectedObjects`
- Split the 1,236 bt-token-v1 function at lines 494-622: lift `BuildItem` plus evaluated `IntDir` into a value type, extract MSBuild evaluation launch/JSON decode into a value-returning helper, and leave selected-source validation/object deletion in `InvalidateSelectedObjects`; preserve retained-log writes and every existing failure string. [~1h]

### `Tools/WorktreeCli/BuildCommand.cpp` — build result and execution stages
- Introduce one `NewBuildResult` schema constructor shared by the 1,643 bt-token-v1 `RunBuildCommandUnguarded` at lines 625-798 and `EmitFallbackBuildResult` at lines 802-825, plus one emission helper for exit code, elapsed time, and messages. [~30m]
- After the argument-parsing range owned by `Architecture_LibraryReplacement.md`, extract retained-log open through final MSBuild result into a small execution state rather than another long parameter list, leaving the top level as parse, validate/discover, execute, and emit. [~1h]

## Critical files
- `Tools/WorktreeCli/BuildCommand.cpp`
- `Tools/WorktreeCli/BuildCommand.h`
- WorktreeCli build fixtures

## Out of scope
- Changing build command syntax, MSBuild invocation, object invalidation policy, result fields, or output channels.
- Fixing build timeout, long-path, or subprocess errors owned by separate reliability plans.
- Generalizing the helpers outside BuildCommand.

## Acceptance criteria
- No changed function exceeds approximately 1,000 bt-token-v1 or more than three meaningful nesting levels.
- Normal and exception fallback results share one schema-construction path.
- Selective invalidation, retained-log ordering, exact-one-JSON stdout, messages, and exit codes remain byte/behavior compatible.

## Notes
- Invariant exposure: `broken-engine-build-result/v1`, selective object invalidation, and shared build coordination; no engine CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- Tier 3 trigger: build/bootstrap contract. Coordinate, but do not require directional ordering, with `Architecture_LibraryReplacement.md` because both touch `RunBuildCommandUnguarded`.
