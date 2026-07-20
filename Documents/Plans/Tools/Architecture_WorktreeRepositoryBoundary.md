# Architecture: Worktree Repository Boundary

## Context
Source: /external-architecture-review on `Tools/` recursively. Repository-specific Git and path policy currently lives in ToolCommon, which is compiled into AgentHarness, while two WorktreeCli modules independently parse the same Git worktree protocol.

## Design

### `Tools/ToolCommon/ToolCliCommon.cpp` and `Tools/ToolCommon/CoordinationStore.cpp`
- Move the WorktreeCli-only `RunGit` interface at `ToolCliCommon.cpp:180-193` and `NormalizeRepositoryRelativeKey` policy at `CoordinationStore.cpp:224-238` out of ToolCommon; retain generic process execution, storage, and non-repository path primitives shared by both executables. [~30m]

### `Tools/WorktreeCli/PlanOrderCommands.cpp` and `Tools/WorktreeCli/LandingLockLifecycle.cpp`
- Add one WorktreeCli-local repository-support module that owns `RunGit`, `WorktreeInfo`, and strict parsing of `git worktree list --porcelain -z`; make `ResolveWorktrees` at `PlanOrderCommands.cpp:317-375` and `AllRegisteredWorktreesClear` at `LandingLockLifecycle.cpp:104-184` apply their distinct selection/rejection policies to the same parsed records. [~1h]

### Visual Studio project membership
- Register the repository-support source/header only in `WorktreeCli.vcxproj` and `.filters`, and remove ToolCommon declarations that no AgentHarness source consumes. [~15m]

## Critical files
- `Tools/ToolCommon/ToolCliCommon.h`
- `Tools/ToolCommon/ToolCliCommon.cpp`
- `Tools/ToolCommon/CoordinationStore.h`
- `Tools/ToolCommon/CoordinationStore.cpp`
- `Tools/WorktreeCli/PlanOrderCommands.cpp`
- `Tools/WorktreeCli/LandingLockLifecycle.cpp`
- `Tools/WorktreeCli/Platforms/VisualStudio2026/WorktreeCli.vcxproj`
- `Tools/WorktreeCli/Platforms/VisualStudio2026/WorktreeCli.vcxproj.filters`

## Out of scope
- Replacing Git command execution with libgit2 or changing accepted repository/worktree states.
- Moving generic `RunProcess`, UTF conversion, or coordination storage out of ToolCommon.
- Changing AgentHarness transport or lock policy.

## Acceptance criteria
- AgentHarness's ToolCommon object set contains no repository workflow or Git helper.
- Both WorktreeCli worktree consumers parse one shared porcelain-v1 `-z` representation while retaining their documented bare/prunable policies.
- Both tool projects build and existing landing/plan-order fixtures preserve outputs and exit codes.

## Notes
- Invariant exposure: landing and plan-queue worktree discovery; no engine determinism/CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- Tier 3 trigger: shared ToolCommon/WorktreeCli boundary and project-membership change.
