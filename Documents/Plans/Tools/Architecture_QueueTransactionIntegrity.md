# Architecture: Queue Transaction Integrity

## Context
Source: /external-architecture-review on `Tools/` recursively. WorktreeCli validates an all-or-none queue mutation contract, but publication is only atomic per target file and readers do not capture a coherent two-queue generation.

## Design

### `Tools/WorktreeCli/PlanOrderCommands.cpp` — `ApplyFileChanges`
- Replace the control-flow-only rollback at lines 1253-1292 with a crash-recoverable transaction boundary for Plans, Features, and update plan bodies: durably stage replacements, publish a journal or generation commit marker, and run mandatory recovery before queue reads. Preserve per-file atomic replacement as the publication primitive. [~1h]

### `Tools/WorktreeCli/PlanOrderCommands.cpp` — `RunValidate`
- Capture Plans, Features, and the relevant claim snapshot under canonically ordered queue commit guards before validation at lines 1655-1667, then release guards before recursive plan-file checks so concurrent add/update/complete cannot produce mixed-generation diagnostics. [~30m]

### `Tools/WorktreeCli/PlanOrderCommands.cpp` — `RunUpdate`
- Re-read and re-hash every tracked target after commit guards are acquired and before `ApplyFileChanges` at lines 2020-2084; during recovery or rollback, restore a target only when its current bytes match the transaction's published replacement hash, otherwise emit an explicit recovery conflict without overwriting foreign edits. [~30m]

## Critical files
- `Tools/WorktreeCli/PlanOrderCommands.cpp`
- `Tools/WorktreeCli/PlanOrderCommands.h`
- `Tools/WorktreeCli/AGENTS.md`
- Queue transaction fixtures under the existing WorktreeCli test surface

## Out of scope
- Changing plan row schemas, score calculation, dependency semantics, or live-claim ownership.
- Generalizing transaction support beyond WorktreeCli queue/plan files.
- Changing primary landing publication rules.

## Acceptance criteria
- Termination after any individual file publication is recoverable to one complete generation before the next read.
- Concurrent validation never reports graph or file conflicts from a mixed Plans/Features generation.
- Concurrent plan-body edits fail with a hash/recovery conflict and are never overwritten or restored from stale cached bytes.
- Add, update, complete, validate, and injected-interruption fixtures preserve documented receipts, unlock reporting, and exit codes.

## Notes
- Invariant exposure: plan-queue atomicity, dependency graph coherence, and tracked plan-body optimistic concurrency; no engine determinism/CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- Tier 3 trigger: crash recovery and cross-file queue transaction contract. `/external-grill-plan` must choose journal/commit-marker versus single-generation storage after current filesystem and landing constraints are re-established.
