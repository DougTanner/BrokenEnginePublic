# Architecture: Plan Coordination Contract

## Context
Source: /external-architecture-review on `Tools/` recursively. WorktreeCli currently derives and validates plan-queue and plan-row identities in separate command modules, so schema or locator changes can silently diverge at the queue trust boundary.

## Design

### `Tools/WorktreeCli/PlanCommands.cpp`
- Replace the `PlanLocator`, `MakePlanLocator`, `ValidateMetadata`, `NewPlanMetadata`, `ReadExisting`, and `EnumerateClaims` contract at lines 31-229 with a WorktreeCli-owned `PlanCoordination` module that exposes typed queue/row identities, locator construction, metadata creation/validation, claim enumeration, and targeted claim reads while leaving verb policy in `RunPlanCommand`. [~1h]

### `Tools/WorktreeCli/PlanOrderCommands.cpp`
- Make `QueueLocator`, `MakeQueueLocator`, `ValidateQueueMetadata`, `ValidateClaimSnapshot`, `ClaimPath`, and `ReadClaim` at lines 967-1060 and 1452-1514 consume the same `PlanCoordination` contract; keep `QueueLocks` multi-lock ordering and plan-order state machines local. [~1h]

### Visual Studio project membership
- Add the new WorktreeCli-only source/header to `WorktreeCli.vcxproj` and `.filters`; do not compile it into AgentHarness. [~15m]

## Critical files
- `Tools/WorktreeCli/PlanCommands.cpp`
- `Tools/WorktreeCli/PlanOrderCommands.cpp`
- `Tools/WorktreeCli/PlanCoordination.h`
- `Tools/WorktreeCli/PlanCoordination.cpp`
- `Tools/WorktreeCli/Platforms/VisualStudio2026/WorktreeCli.vcxproj`
- `Tools/WorktreeCli/Platforms/VisualStudio2026/WorktreeCli.vcxproj.filters`

## Out of scope
- Changing queue schemas, paths, metadata fields, exit codes, lock ordering, or command-line behavior.
- Generalizing command state machines behind callbacks.
- Changing AgentHarness lock records.

## Acceptance criteria
- `plan queue`, `plan row`, and `plan order` use one queue/row identity and metadata-validation implementation.
- Existing WorktreeCli command schemas and positive/negative exit codes remain unchanged.
- WorktreeCli builds and representative queue validation/claim fixtures pass.

## Notes
- Invariant exposure: queue and row-claim ownership contract; no engine determinism/CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- Tier 3 trigger: queue coordination contract and new project membership across multiple command modules.
