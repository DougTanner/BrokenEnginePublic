---
name: session-audit
description: >-
  Final fresh-eyes audit of late, reconciled, or previously unseen integration
  hypotheses in a complete logical change. Use when explicitly requested or
  when the post-reconciliation typed evaluator requires it. Findings only;
  never edits.
allowed-tools: [Read, Grep, Glob, PowerShell]
---

# Session Audit

Main dispatches one fresh `reviewer`. The reviewer does not edit, run mutating
commands, implement fixes, or delegate. Audit only hypotheses that earlier
domain reviews could not have covered; do not repeat their artifact-level
correctness, style, documentation, shader, or validation passes.

## Required Inputs

Require a self-contained, immutable brief containing:

- absolute adopted worktree, fixed session-start baseline commit, reconciled
  candidate commit/tree, its sole parent, and primary tip;
- Git-derived candidate changed-file inventory and touched regions, with implementation,
  propagation, review-fix, conditional-role, and reconciliation attribution,
  excluding named pre-existing or concurrently owned work;
- final approved plan and exact approved deltas, declared invariants, and
  acceptance criteria;
- one disposition for each conditional mode: late semantic fixes,
  reconciliation edits or invalidated assumptions, Tier-3 cross-file
  integration, and contract-significant regions unseen by domain review. Mark
  each `triggered` or `not triggered`; for a triggered mode name its exact
  regions and hypotheses;
- completed applicable domain-review handoffs for every changed artifact type,
  plus reconciliation, build, external-API-verification, accepted-fix/retest,
  residual, and focus-area handoffs (`none` is valid for each).
- the complete `broken-engine-session-audit-input/v2` evaluator input and its
  `broken-engine-session-audit-decision/v2` result. Require a well-formed
  `required:true` decision; explicit user requests are represented in that
  input and always require the audit. A well-formed `required:false` decision
  means this skill must not be dispatched automatically.

The v2 evaluator input names exact pre/post-reconciliation candidate commits and
derives both deltas from their parents using Git; the brief carries the verified
candidate tree separately. It retains the existing
conflict, dependency-overlap, coverage, late-fix, manual-resolution,
unseen-region, and explicit-request evidence. An identical conflict-free delta
with no dependency overlap may retain evidence; any changed delta routes the
affected checks and audit decision before primary mutation.

For an explicit user request, main dispatches one preparation `implementer` to
assemble the required brief from repository state — baseline, candidate, parent,
and primary commits, the Git-derived candidate inventory, and stage dispositions. The worker returns
that brief with the user-authorized audit scope mapped to the same bounded late,
reconciled, or unseen-integration hypotheses; main then dispatches the reviewer.
For a workflow-triggered audit, the finalization or preparation implementer
assembles the same brief and evaluator evidence and returns it through main. Tier-3 cross-file scope alone and a no-op reconciliation are not independent triggers. Return `BLOCKED` when lifecycle identity, attribution, intent, a
mode disposition, or an applicable prerequisite domain review is missing,
ambiguous, moving, or incomplete. The assigned implementer's assembly from
repository state is the only allowed source; the auditor never reconstructs
these inputs from conversation history or a mutable merge base.

## Method

Required order: terminal preparation -> candidate creation -> reconciliation/single-parent squash -> exact candidate verification -> finalization summary and explicit confirmation -> primary mutation.

1. Confirm the checkout, fixed baseline, candidate commit/tree, sole parent,
   and primary identities, then derive and inventory only the candidate-parent
   delta from Git.
2. For each triggered mode, inspect only its named regions and the minimum
   callers, consumers, mirrors, contracts, and whole-file context needed to
   prove or refute its hypotheses. Stop when all authorized hypotheses resolve.
3. For late fixes, check newly introduced determinism/CRC, phase, guard-affinity,
   doc-symbol, whole-file coherence, debris, and residual regressions only where
   the late handoff makes them reachable. Global
   `%LOCALAPPDATA%\BrokenEngine\AgentReports\` artifacts conforming to the shared
   reporting contract are intentional process state, not session debris.
4. For reconciliation, check manual resolutions and invalidated assumptions for
   semantic merge damage, half-applied mirrors, stale symbols, duplicated paths,
   and incorrect version or compatibility integration.
5. For unseen integration, trace only cross-file or contract-significant
   producer/consumer paths that no supplied domain review saw. Spot-check that
   accepted fixes and resolved residuals exist in the final tree.
6. Report only changed, reachable failures. Refute candidates against guards,
   preconditions, handoffs, and current contracts. Put proven pre-existing or
   out-of-scope defects in `Residuals`. Exclude stale citations in a claimed
   plan slated for completion-deletion.
7. Emit an atomic API verification request for any candidate depending on a
   non-obvious external rule; do not present it as confirmed. The main session
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
Decisive checks: <identity/inventory plus trace/read and result per authorized hypothesis>
Build required: none
Residuals: <pre-existing defect, incomplete trace, pending external verdict, or none>
```

Use `NEEDS_ACTION` for findings or pending external verification and `BLOCKED`
only for missing required evidence. Critical means data loss, broken
functionality, determinism failure, or an equivalent contract breach; every
other finding is Required.
