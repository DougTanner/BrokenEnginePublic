---
name: next-plan
description: Validates and deterministically claims one Git-backed Documents/Plans Plan through WorktreeCli, resolves it against current code, and presents the resolved Plan and execution card for implementation approval; preparation-proven Tier-1 work continues without that pause. Use only when the latest user request explicitly invokes `/next-plan` or `$next-plan`.
disable-model-invocation: true
argument-hint: "[Documents/Plans/... | filename.md]"
allowed-tools: [Read, Write, Grep, Glob, Agent, Edit, PowerShell, AskUserQuestion]
---

# Next Plan

Use only for a current explicit `/next-plan` or `$next-plan` invocation. Main
retains user intent and dispatches fixed roles; workers never delegate.
WorktreeCli alone validates metadata, selects, claims, prepares final state,
and releases claims. `Documents/Features` is never scheduler input. The
cross-skill stage order lives in root [AGENTS.md](../../../AGENTS.md) Step 8,
and the landing confirmation belongs to `/finalize-changes`.

## Preconditions and selection

Require a clean wrapper-created session worktree and derive the authoritative
primary, baseline, owner, and provisioned WorktreeCli from
`Get-AgentWorktreeSessionContext`. Missing tooling requires explicitly
authorized primary maintenance through `/compile`. Never create/adopt a
worktree or inspect machine-local claims directly.

- Bare invocation selects the oldest eligible Plan by immutable `createdUtc`,
  then normalized UTF-8 path.
- A normalized `Documents/Plans/...` argument selects that Plan.
- A filename selects only one exact case-sensitive executable leaf match;
  zero/duplicates block. Reject every other path shape.

Run `scripts/Invoke-NextPlanClaim.ps1`; do not reconstruct its transitions. On
`status: pass`, act on the code: `ok` and `reused` both mean this session holds
the named claim, and `none-available` is a normal stop with nothing to claim.
Any other status stops the skill without repair, reordering, or retry.

## Preparation and execution card

One preparation `implementer` verifies every Plan statement against current code;
the Plan is immutable. Current code wins on contradiction, which is returned as
a card correction or meaningful delta rather than forced into the tree. If the
problem is gone, main asks whether to retain it or explicitly authorize obsolete
final cleanup.

The execution card begins with `### What does this plan do?` and `### Why this
is good for the codebase`, each 2-4 plain sentences, then records goal, out of
scope, tier trigger, interfaces/invariants, acceptance checks with expected
observations, and required/conditional roles. Tier 1 skips plan audit. Tier 2
uses one fresh `/plan-audit` reviewer. Tier 3 follows `/external-grill-plan`,
whose authoritative workflow reference owns its iterative preparation. Missing a
mandatory reviewer blocks.

Invoke the claim script idempotently immediately before the final preparation
handoff. Every delegation uses the single task brief in
`../../references/subagent-reporting.md` and states that Plan and card statements are
hypotheses: return contradictions to main.

## Implementation approval

Preparation and claim do not require approval. Present the complete resolved
Plan and execution card before implementation: scope, invariants, role
assignments, acceptance criteria, and unresolved decisions. Codex Plan Mode
returns one complete `proposed_plan`; another host uses its approval UI, or asks
one direct question. An affirmative response approves only the latest unchanged
presentation. A meaningful Plan, card, scope, invariant, acceptance, or decision
change requires a new complete presentation.

Per the claimed-executable-Plan paragraph after the Change Workflow steps in
root [AGENTS.md](../../../AGENTS.md), preparation that proves the Plan Tier 1,
decision-complete, and current continues straight into implementation without
this pause; main records those facts. The landing confirmation in
`/finalize-changes` always applies.

## Claim lifecycle

Before landing-commit creation, an `implementer` runs
`scripts/Complete-NextPlan.ps1` with no arguments for completion or `-Reject`
only after explicit user-authorized rejection. Success removes only direct-child
dependency edges, deletes the selected Plan in the worktree, reports the
`changedPaths` the landing commit must contain, and returns
`nextAction: finalize-changes`. The claim stays held until landing succeeds.

Deferral uses `scripts/Defer-NextPlan.ps1` and only an ordinary live claim;
never defer after final preparation has run. `/finalize-changes` deletes the
claim after primary advances. Run `scripts/Test-NextPlanWorkflowScripts.ps1`
only when one of those scripts changes.

## Preparation handoff

```text
Claim: <Plan path or none; resolved state when claimed>
Classification: Tier 1 | Tier 2 | Tier 3 and trigger
Approval pause: skipped (proven Tier-1) | required
Residuals: <blocker or none>

Execution card:
### What does this plan do?
<2-4 plain sentences>
### Why this is good for the codebase
<2-4 plain sentences>
- Goal: <result>
- Out of scope: <boundary>
- Tier trigger: <trigger or none>
- Interfaces and invariants: <contracts>
- Acceptance checks: <check and expected observation>
- Roles: <required and conditional assignments>
```
