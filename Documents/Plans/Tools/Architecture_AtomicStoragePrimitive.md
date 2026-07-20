# Architecture: Atomic Storage Primitive

## Context
Source: /external-architecture-review on `Tools/` recursively. ToolCommon metadata and WorktreeCli queue state duplicate atomic file replacement, and their long-path handling has diverged for tracked plan files inside deep session worktrees.

## Design

### `Tools/ToolCommon/CoordinationStore.cpp`
- Extract a long-path-safe `WriteBytesAtomic(path, bytes)` from `WriteMetadataAtomic` at lines 282-305, preserving unique temporary creation, full write, `FlushFileBuffers`, write-through replacement, and cleanup; implement JSON metadata writing as serialization plus the shared byte primitive. [~30m]

### `Tools/WorktreeCli/PlanOrderCommands.cpp`
- Remove the private `WriteBytesAtomic` at lines 1225-1244 and make `ApplyFileChanges` use the ToolCommon primitive for queue-store and tracked-plan legs, so `RunUpdate` no longer mixes pre-prefixed store paths with unprefixed paths from `ResolveContainedPath` at lines 2020-2039 and 2060-2063. [~30m]

## Critical files
- `Tools/ToolCommon/CoordinationStore.h`
- `Tools/ToolCommon/CoordinationStore.cpp`
- `Tools/WorktreeCli/PlanOrderCommands.cpp`

## Out of scope
- Providing crash atomicity across multiple files; that belongs to `Architecture_QueueTransactionIntegrity.md`.
- Changing queue schemas, rollback policy, metadata JSON, or command output.
- Replacing Win32 durable-write primitives.

## Acceptance criteria
- Metadata, queue, and tracked-plan replacements use one long-path-safe byte writer.
- Deep-worktree plan updates no longer depend on callers pre-prefixing every transaction path.
- AgentHarness and WorktreeCli build; coordination metadata and plan-order update fixtures preserve bytes and exit behavior.

## Notes
- Invariant exposure: coordination metadata and queue/plan persistence; no engine determinism/CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- Tier 3 trigger: shared storage primitive used by both tool executables and queue mutation.
