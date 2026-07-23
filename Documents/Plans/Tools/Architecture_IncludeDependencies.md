<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":[]} -->
# Architecture: Include Dependencies

## Context
Source: /external-architecture-review on `Tools/` recursively. The PCH-less tool sources have two verified unused includes and several declarations that compile only through transitive standard-library headers.

## Design

### `Tools/WorktreeCli/LandingLockCommands.h`
- Remove the unused `<string>` include at line 3; the header's only declaration uses built-in types. [~5m]

### `Tools/WorktreeCli/PlanScheduler.cpp`
- Remove the unused `<cctype>` include at line 9; whitespace handling in `Trim` and `TrimLineEnd` does not call character-classification APIs. [~5m]

### `Tools/WorktreeCli/LandingLockLifecycle.h` and `Tools/ToolCommon/ToolCliCommon.h`
- Make the standard types named by `LandingLockLifecycle.h:11-23` direct dependencies and make shared `std::exception` consumption explicit for the four PCH-less translation units that catch it; follow ToolCommon's centralized shared-consumption rule rather than relying on JSON or sibling headers transitively. [~15m]

## Critical files
- `Tools/WorktreeCli/LandingLockCommands.h`
- `Tools/WorktreeCli/LandingLockLifecycle.h`
- `Tools/WorktreeCli/PlanScheduler.cpp`
- `Tools/ToolCommon/ToolCliCommon.h`

## Out of scope
- Redesigning the intentional `ToolCliCommon.h` aggregation hub.
- Moving repository-specific helpers or changing project membership.
- Formatting or naming cleanup unrelated to include ownership.

## Acceptance criteria
- Verified unused includes are absent and declarations no longer rely on incidental transitive standard headers.
- AgentHarness and WorktreeCli compile without PCH support.

## Notes
- Invariant exposure: none.
