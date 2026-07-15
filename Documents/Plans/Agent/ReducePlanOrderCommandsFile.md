# Analyze a safe reduction boundary for PlanOrderCommands

## Context

`Tools/AgentCli/PlanOrderCommands.cpp` is 2,130 lines and 19,168 `bt-token-v1` tokens. That exceeds the repository's hard file-size guideline and was recorded as structural residual R001 in `Temp/AgentReports/e95e7d0b-2fed-4e30-843a-d7968e3d2a7f.md`.

This is a maintainability concern, not a demonstrated command failure: the Release/x64 AgentCli build and the deterministic plan-order fixture passed in the source verification. The file owns coupled validation, queue parsing, transaction, lock, and receipt behavior, so a speculative split risks changing queue authority semantics.

## Design

1. Invoke `/reduce-file` for `Tools/AgentCli/PlanOrderCommands.cpp` after the current shared-primary lifecycle work has settled.
2. Use the reduction analysis to map responsibilities, coupling, and viable file/class boundaries. Preserve the existing single queue-authority and coordination domains; do not introduce a second parser, lock, or receipt authority.
3. Present the resulting reduction options and exact migration boundary for approval before authoring an implementation plan. The approved implementation plan must name the affected AgentCli sources, project membership if files are added, and behavior-preserving verification.

## Critical files

- `Tools/AgentCli/PlanOrderCommands.cpp` — 2,130-line / 19,168-token structural analysis target.
- `Tools/AgentCli/PlanOrderCommands.h` and directly coupled AgentCli command sources — inspect only if the reduction analysis proves a boundary crosses them.
- `Documents/Plans/Agent/ReducePlanOrderCommandsFile.md` — decision-plan record.

## Out of scope

- Changing `plan order` behavior, queue-row format, locking, claims, receipt semantics, or shared-primary validation policy during this analysis.
- Selecting or implementing a file split before `/reduce-file` maps and justifies the boundary.
- Engine/runtime behavior, client/server targets, unit tests, and direct `Order.md` edits.

## Acceptance criteria

- `/reduce-file` produces an evidence-backed responsibility map and a bounded reduction recommendation for `PlanOrderCommands.cpp`.
- The chosen implementation boundary is approved before code changes begin.
- Any later implementation preserves AgentCli Release/x64 build success, capability checks, deterministic plan-order fixture coverage, and `git diff --check` through the supported workflows.

## Notes

- Decision plan only; defer implementation until the reduction analysis identifies a safe seam.
- Score: Effort 3, Impact 3, Risks 2, total 2; Tier Medium. The risks are confined to global developer-workflow queue authority rather than engine runtime determinism.
