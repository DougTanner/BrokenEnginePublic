---
name: verify-changes
description: >-
  Verify a change at a landing gate with a read-only acceptance table bound to
  the reviewed diff and prior role evidence.
allowed-tools: [Read, Grep, Glob, "Bash(git diff *)", "Bash(git status *)", "Bash(git ls-files *)", "Bash(git hash-object *)", "Bash(git submodule status *)", PowerShell]
---

# Verify Changes

Run only at a root [AGENTS.md](../../../AGENTS.md) landing gate. Main dispatches
one fresh read-only `reviewer`; it never edits, delegates, builds, launches
runtime work, mutates claims or the tree, or fixes/waives findings.
`plan validate` may perform only its established stale local-claim healing.

## Required inputs

Require the task brief from `../../references/subagent-reporting.md`, plus:

- adopted checkout and route (`session-finalization` or explicitly authorized
  `primary-commit`);
- final approved Plan/user instruction and approved deltas;
- tier/triggers, role assignments, acceptance table, and invariants; and
- caller-owned paths and concise ordered handoffs for implementation,
  propagation, checks, reviews, fixes, hygiene, builds, external claims, and
  residuals.

Do not accept an unapproved delta or pasted full logs. For a completed claimed
Plan whose file the change deletes, the manager supplies its approved text or
its Git-history path.

## Reviewed diff and authorization

1. Derive the reviewed diff from Git: `git diff --raw -M <primary-tip>...<session-branch>`,
   or the squashed landing commit against its parent when one exists. Include
   modes, additions/deletions, renames, symlinks, and gitlinks. Add the
   untracked files the caller declared as authorized additions.
2. Reconcile the diff paths against the caller-declared owned paths. Each
   changed region must name its authorizing Plan clause or user instruction.
   Extra, missing without a recorded no-change decision, or unauthorized bytes block.
3. Confirm every handoff still applies to this diff and no later change
   invalidated it. A fix ends the run: main applies it and re-review covers only
   the changed regions.
4. Inventory ignored/non-worktree state with exact path, owner/persistence
   contract, evidence, and `unchanged`, `intentionally persisted`, `restored`,
   or non-passing `residual` outcome.

## Acceptance table

Return one inline row per approved criterion, invariant, visible changed
behavior, required check/review, and residual:

`criterion | decisive check | PASS | FAIL | BLOCKED | UNVERIFIED | evidence`

Every `PASS` cites evidence this verifier read/ran or a mandated handoff
whose fields prove the row. Narrative alone is `UNVERIFIED`. Duplicate checks
name their independent signal. Tier sets the exploration ceiling: Tier 1 uses
static/schema/link/validator/affected-target compile evidence; Tier 2 adds the
approved observable scenario; Tier 3 adds exposed invariant/integration
evidence. Only an approved user delta may revise/defer a criterion, and a
deferral names its tracked Plan.

Reconcile each review as:
`review | delegated/inline | findings | accepted-fixed | refuted | unresolved | evidence`.
Require zero unresolved accepted findings and fix/recheck evidence. External
claims name atomic proposition, version/configuration, official primary source,
verdict, dependent finding, and evidence; unresolved or unadjudicated
refutations are non-passing.

For builds, require the authoritative `broken-engine-build-result/v1`: intended
target/selection/arguments, status/exit/failure kind, complete retained log,
decisive diagnostics, and all requested targets. Game builds additionally
require data mode, generation authority, Gaea outcome, canonical paths, and
current passing data-oracle verification. Do not accept schema/result/exit
mismatches.

Skill changes require a complete `/validate-skill` PASS handoff with mechanical
self-check, target validator exit/output, semantic review, and no Critical
finding.

## Executable Plan check

When the reviewed diff touches `Documents/Plans/**`, run WorktreeCli
`plan validate` against the session worktree and require a valid result. Record
notices and healing. Otherwise record `not triggered — no executable-Plan
change`. Primary post-commit validation belongs to finalization.

## Output

Return `Verification: PASS` only when ownership, authorization, and every row
pass. Otherwise return `Verification: BLOCKED` once with all decisive items; do
not retry. A PASS binds the reviewed diff; if that diff later changes
materially, re-review only the changed regions.

Follow `../../references/subagent-reporting.md`: route, checkout, the reviewed
diff, Git-derived inventory, acceptance/review/API tables, Plan check,
fix/re-entry history, non-passing items, and `Residuals` last.
