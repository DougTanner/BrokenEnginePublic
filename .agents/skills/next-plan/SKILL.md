---
name: next-plan
description: Validates and claims the live primary plan queue through WorktreeCli, resolves the selected plan against current code, and presents it at the single implementation-approval gate. Use only when the latest user request explicitly invokes `/next-plan` or `$next-plan`.
disable-model-invocation: true
argument-hint: "[features|Documents/Plans/...|Documents/Features/...]"
allowed-tools: [Read, Write, Grep, Glob, Agent, Edit, PowerShell, AskUserQuestion]
---

# Next Plan

Claim one plan from the machine-local queue, confirm it remains relevant, and
present the complete resolved plan. Follow the [canonical execution-gate
contract](references/execution-gates.md) through landing. WorktreeCli alone owns
queue parsing, locking, selection, and row mutation.

## Invocation and preconditions

- Start only when the latest user request explicitly invokes `/next-plan` or
  `$next-plan`.
  Once claimed, later approval or blocker-resolution turns continue that active
  workflow; an invocation earlier in unrelated history does not start one.
- Bare invocation selects the Plans queue. `features` explicitly selects the
  Features queue. A canonical `Documents/Plans/...` or
  `Documents/Features/...` argument selects that exact plan and its queue.
  Reject every other argument instead of guessing.
- Require a wrapper-created isolated worktree, live WorktreeCli session claim,
  authoritative primary checkout/branch, and a clean session tree. Never create
  or adopt a worktree.
- Derive WorktreeCli, Git identities, baseline, and owner only through the
  wrapper environment and canonical sidecars. Missing tooling requires
  explicitly authorized primary maintenance through `/compile`.
- Retain the wrapper owner for the selected row's lifecycle. Never parse or edit
  the machine-local queue directly.

## Canonical sidecars

Each sidecar emits one JSON result: exit `0` succeeds, `2` is a deterministic
state blocker, and `1` is malformed input or internal failure. Do not recreate
their transitions with ad hoc WorktreeCli commands.

- [`Invoke-NextPlanClaim.ps1`](scripts/Invoke-NextPlanClaim.ps1) accepts optional
  `-Queue plans|features` (default `plans`) and optional canonical `-Plan`. It
  preflights `validate`, `claim-next`, `complete`, and `plan row status`
  capabilities; requires a clean session tree; and stops on any orphan plan.
- [`Complete-NextPlan.ps1`](scripts/Complete-NextPlan.ps1) requires `-Plan` and
  `-PlanSha256` from the claim. Require `workflowTerminal: false` and
  `nextAction: finalize-changes`.
- Run [`Test-NextPlanWorkflowSidecars.ps1`](scripts/Test-NextPlanWorkflowSidecars.ps1)
  when a sidecar changes, never during ordinary selection.

## Workflow

1. Run the claim sidecar for the resolved queue/plan. It validates the primary
   queue before claiming. Report and stop on validation diagnostics, orphan-plan
   notices, dependency blockers, stale sessions, or claim conflicts; never add
   or repair a queue row during `/next-plan`.
2. Read the selected plan and current code. Keep the claimed plan immutable.
   Compute its SHA-256 and require an exact match with `claim.planSha256` before
   the first `/plan-audit` or, for Tier 1, before presentation. A mismatch is
   terminal: retain the row and stop without review, presentation, or re-claim.
   Carry stale citation corrections in the execution card. Remove unnecessary
   steps/checks from the resolved presentation rather than granting them
   authority. If the problem is gone, ask whether to retain the plan or approve
   terminal cleanup as obsolete work.
3. Classify the actual change and prepare the execution card. Every card starts
   with `### What does this plan do?` and
   `### Why this is good for the codebase`, each in 2-4 plain sentences without
   process jargon. Then state goal, out-of-scope boundary, tier trigger,
   interfaces/invariants, acceptance checks with expected observations, and
   required/conditional roles.
   - Tier 1 skips plan review.
   - Tier 2 delegates `/plan-audit` to one `reviewer`.
   - Tier 3 reads [Tier 3 preparation](references/tier3-workflow.md), delegates
     `/plan-audit`, then runs `/external-grill-plan` in the user-facing session.
   If reviewer delegation is unavailable, stop; only explicit user direction
   permits inline review, recorded as `review freshness degraded`.
4. Recompute the plan SHA-256 immediately before presenting and require the
   claimed digest. Present the complete resolved plan at the one approval gate
   under the canonical approval contract. It includes the full execution card,
   implementation step, boundaries, interfaces/invariants, acceptance checks,
   role dispositions, and unresolved decisions. Any material presentation
   change invalidates approval and requires a new complete presentation.
5. After approval, implement continuously through propagation, targeted checks,
   domain review, accepted fixes, hygiene, and acceptance verification. Pause
   only for a safety blocker or the final landing confirmation. Never edit the
   claimed plan.
6. Before completion, invoke `Complete-NextPlan.ps1` with the claimed plan path
   and original digest. A digest mismatch is terminal and leaves the plan/row
   intact. The sidecar also verifies row ownership, runs the closure scan, and
   stages plan deletion. The owner-held row remains until finalization publishes
   completion after landing.
7. Run `/verify-changes`, then `/finalize-changes`. Continue through commit and
   reconciliation preparation; only the canonical landing confirmation may
   authorize primary history mutation.

## Primary advance

Before claim, if primary advanced, require the session tree to be clean, rebase
the session branch onto the current primary tip, verify it is still
clean, and re-baseline `BROKEN_ENGINE_BASELINE` to that tip for every subsequent
sidecar invocation. After claim, continue at the wrapper baseline until
`/finalize-changes`; a primary advance or non-blocking stale-baseline
`missing-plan-file` notice does not invalidate claim or approval.

## Handoff before approval

```text
Claim: <plan path or none>
Classification: Tier 1 | Tier 2 | Tier 3 and trigger
Route: <fast path | Tier 3 preparation | retained>
Residuals: <blocker or none>

Execution card:

### What does this plan do?
<2-4 plain sentences>

### Why this is good for the codebase
<2-4 plain sentences>

- Goal: <result>
- Out of scope: <boundary>
- Tier trigger: <trigger or none>
- Interfaces and invariants: <affected contracts>
- Acceptance checks: <check and expected observation>
- Roles: <required and conditional roles>
```

The card indexes the complete resolved plan; it never substitutes for it. A
rejection, deferral, blocker, or digest mismatch retains the row and plan.
