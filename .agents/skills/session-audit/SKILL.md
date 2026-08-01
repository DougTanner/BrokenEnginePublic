---
name: session-audit
description: >-
  Final fresh-eyes audit of late, reconciled, or previously unseen integration
  hypotheses in a complete logical change. Use only when the user explicitly
  requests a session audit. Findings only; never edits.
disable-model-invocation: true
allowed-tools: [Read, Grep, Glob, PowerShell]
---

# Session Audit

Run only on an explicit user request. Main dispatches one fresh `reviewer`. The
reviewer does not edit, run commands that change state, implement fixes, or
delegate.
Audit only hypotheses that earlier domain reviews could not have covered; do not
repeat their artifact-level correctness, style, documentation, shader, or
validation passes.

Root `../../../AGENTS.md` owns stage order. This skill owns only the local
read-only action after a complete brief has triggered it: trace the named late,
reconciled, or previously unseen integration hypotheses and report findings.

## Required Inputs

Require a self-contained, immutable brief containing:

- the absolute adopted worktree and the session baseline commit;
- the changed-file inventory and touched regions with implementation,
  propagation, review-fix, conditional-role, and reconciliation attribution,
  excluding named pre-existing or concurrently owned work. The preparation
  implementer assembles that inventory with the read-only
  `.agents/scripts/Get-SessionChangeInventory.ps1 ... -Regions` run described
  below, never by enumerating the changed files and hunks inline; the
  attribution and the exclusion of pre-existing or concurrently owned work stay
  the implementer's judgment, which the script does not make;
- the final approved plan and the exact changes the user approved after it,
  declared invariants, and acceptance criteria;
- one decision per conditional mode below, marked `triggered` or
  `not triggered`; a triggered mode names its exact regions and hypotheses:
  - late semantic fixes — check newly introduced determinism/CRC, phase,
    guard-affinity, doc-symbol, whole-file coherence, debris, and residual
    regressions only where the late handoff makes them reachable;
  - reconciliation edits or invalidated assumptions — check manual resolutions
    and those assumptions for semantic merge damage, half-applied mirrors, stale
    symbols, duplicated paths, and incorrect version or compatibility integration;
  - Tier-3 cross-file integration;
  - contract-significant regions unseen by domain review — trace only their
    cross-file or contract-significant producer/consumer paths no supplied domain
    review saw, and spot-check that accepted fixes and resolved residuals exist
    in the final tree;
- completed applicable domain-review handoffs for every changed artifact type,
  plus reconciliation, build, external-API-verification, accepted-fix/retest,
  residual, and focus-area handoffs (`none` is valid for each).

The inventory run is `pwsh -NoProfile -File
.agents/scripts/Get-SessionChangeInventory.ps1 -RepositoryRoot <absolute adopted
worktree> -Baseline <full 40-character SHA> -Regions` (in Claude Code's Git Bash
terminal convert the script path and root with `cygpath -w` exactly as
`../cleanup-worktrees/SKILL.md` shows). It writes no file and prints one
`broken-engine-session-change-inventory/v1` object with `entries` and their
`class` values, `counts`, `triggers`, and the per-hunk `regions` table. Only
`status` `pass` (exit 0) is usable; `blocked` (exit 2) or `error` (exit 1) means
the inventory is missing for the rule below. Entries are capped at 500 rows and
regions at 400, so the implementer reports `truncation` and `truncated` with the
brief so a shortfall is never presented as a complete inventory. An untracked
file appears only when the caller supplies it with `-IncludeUntracked
<comma-separated paths>`, and `counts.unlistedUntracked` reports how many
untracked files the run did not list.

Main dispatches one preparation `implementer` to assemble that brief from
repository state and to map the user-authorized audit scope to the same named
late, reconciled, or unseen-integration hypotheses; main then dispatches the
reviewer. Return `BLOCKED` when the checkout, session baseline, attribution, intent, a
mode decision, or an applicable prerequisite domain review is missing,
ambiguous, moving, or incomplete. The assigned implementer's assembly from
repository state is the only allowed source; the auditor never reconstructs
these inputs from conversation history.

## Method

1. Confirm the checkout and derive the session diff from the session
   baseline, then inventory it.
2. For each triggered mode, apply its `## Required Inputs` procedure over only
   its named regions and the minimum callers, consumers, mirrors, contracts,
   and whole-file context needed to prove or refute its hypotheses. Stop when
   all authorized hypotheses resolve.
3. Report only changed, reachable failures. Refute proposed findings against guards,
   preconditions, handoffs, and current contracts. Put proven pre-existing or
   out-of-scope defects in `Residuals`. Exclude stale citations in a claimed
   plan that is deleted when it completes.
4. Emit a single-claim API verification request for any candidate depending on
   a non-obvious external rule; do not present it as confirmed. The main session
   reads every finding, deduplicates it, and classifies Intent
   (`conformance | plan_delta`) and Scope (`non_structural | structural`) before
   dispatching any fix.

## Output

```markdown
## Session Audit Results

### Findings
- `path:line` — **Critical | Required:** — mode — <reachable failure and evidence>
- none

### API Verification Requests
- <symbol/rule, exact proposition, dependent candidate finding, applicability, official source>
- none

### Traced Clean
<Only when clean: hypotheses traced, decisive refutations, and `PASS — audit complete; stop.`>

Status: PASS | NEEDS_ACTION | BLOCKED
Changed files: none
Decisive checks: <inventory plus trace/read and result per authorized hypothesis>
Build required: none
Residuals: <pre-existing defect, incomplete trace, pending external verdict, or none>
```

Use `NEEDS_ACTION` for findings or pending external verification and `BLOCKED`
only for missing required evidence. Critical means data loss, broken
functionality, determinism failure, or an equivalent contract breach; every
other finding is Required.
