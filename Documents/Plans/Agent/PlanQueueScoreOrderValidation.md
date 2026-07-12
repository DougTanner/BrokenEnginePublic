# Validate Plan Queue Score Ordering

## Context

`Documents/Plans/Order.md` promises one `## Plans` table sorted by numeric `Score`, and `/next-plan` selects by walking that table top-down. The live table violates that invariant in two places: `Graphics/Managers/Refactor_PipelineManagerSplit.md` has score 1 after score-3 rows, and `File/Architecture_FileManagerSplitDecision.md` has score -2 after a score-4 row. Their score arithmetic is correct and both linked files exist; their row positions are wrong. As a result, an unclaimed higher-priority plan can be skipped behind lower-priority work.

The queue is edited by several skills and by humans, but enforcement is currently prose-only (`Documents/Plans/AGENTS.md` and `.agents/skills/next-plan/SKILL.md`). There is no deterministic check for score arithmetic, nondecreasing row order, duplicate paths, or missing linked files before `/next-plan` trusts table position. This follow-up comes from a session-audit residual; repairing unrelated queue rows was outside that active change.

## Design

1. Stable-sort the existing `Documents/Plans/Order.md` `## Plans` rows by numeric Score, preserving the current relative order of equal-score rows and leaving the reference, dependency, and file-group sections byte-unchanged. This moves `File/Architecture_FileManagerSplitDecision.md` into the score--2 block and `Graphics/Managers/Refactor_PipelineManagerSplit.md` into the score-1 block without changing either plan or score.
2. Add a small reusable PowerShell validator under `.agents/skills/next-plan/scripts/`. It accepts one or more `Order.md` paths, parses only the `## Plans` table, and exits nonzero with exact row/path evidence for malformed rows, incorrect `Effort - Impact + Risks` arithmetic, decreasing numeric scores, duplicate plan paths, or links that do not resolve relative to the queue file. Output and failure ordering must be deterministic.
3. Invoke the validator in `/next-plan` before orphan reconciliation or candidate selection so top-down selection never proceeds on an invalid queue. Invoke it after queue mutations in `/save-plan` and `/create-follow-up-plans`; a failed post-check must be repaired in the same turn rather than reported as a successful queue update. Validate both `Documents/Plans/Order.md` and `Documents/Features/Order.md` where a skill can mutate either tree.
4. Add the canonical manual validation command to the Plans and Features queue instructions so direct human edits use the same check. Keep row sorting manual and stable; the validator diagnoses but does not silently rewrite either queue.

## Critical files

- `Documents/Plans/Order.md` — stable numeric-score repair for the live `## Plans` table.
- `.agents/skills/next-plan/SKILL.md` and new `.agents/skills/next-plan/scripts/ValidatePlanOrder.ps1` — fail-fast validation before priority selection.
- `.agents/skills/save-plan/SKILL.md` and `.agents/skills/create-follow-up-plans/SKILL.md` — post-mutation validation.
- `Documents/Plans/AGENTS.md` and `Documents/Features/AGENTS.md` — canonical manual validation command.

## Out of scope

- Re-scoring plans, changing tiers or Notes, or choosing a new relative order among equal-score rows.
- Changing plan claim/lock semantics, orphan scoring, dependency resolution, or `/next-plan` candidate eligibility.
- Auto-sorting malformed queues, rewriting reference/index tables, or validating prose in Dependencies/File Groups.
- Adding a compiled tool, unit tests, CI service, or runtime/game verification.

## Acceptance criteria

- Both queue files pass the validator with correct arithmetic, nondecreasing scores, unique plan paths, and resolving plan links.
- `File/Architecture_FileManagerSplitDecision.md` appears in the score--2 position and `Graphics/Managers/Refactor_PipelineManagerSplit.md` in the score-1 block; all equal-score rows retain their pre-repair relative order.
- A temporary fixture for each supported failure class returns nonzero and identifies the offending queue path and row; a valid fixture returns zero. Verification uses disposable files outside the planning trees and leaves no repository artifact.
- `/next-plan` stops before claim/orphan mutation on invalid ordering, while `/save-plan` and `/create-follow-up-plans` require a clean deterministic post-check before reporting success.

## Notes

- Developer workflow and documentation only. No C++ build, client/server runtime, DataPacker/export, determinism/CRC, replay, wire protocol, `kiVersion`/`.pack`, shader, guard-scope, or allocation-tracked-path exposure.
- The validator is read-only. The one-time stable sort remains an explicit reviewed `Order.md` edit.
