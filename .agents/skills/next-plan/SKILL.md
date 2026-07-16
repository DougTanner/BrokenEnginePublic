---
name: next-plan
description: Validates and claims the live primary plan queue through WorktreeCli, refreshes the selected plan against current code, and presents the full resolved plan at the single pre-implementation approval gate. Use when the user invokes `/next-plan`.
disable-model-invocation: true
argument-hint: "[plan-file-path]"
allowed-tools: [Read, Write, Grep, Glob, Agent, Edit, PowerShell, AskUserQuestion]
---

# Next Plan

Claim the first eligible live plan from clean primary state, confirm it remains
relevant, then use its risk tier to prepare and present the complete resolved
plan. Follow the [canonical execution-gate
contract](references/execution-gates.md) from claim through primary mutation;
do not stop after the claim or present only a claim/execution-card summary.
WorktreeCli owns queue parsing, locking, selection, and row mutation; this skill
never parses or edits an executable `Order.md` row.

## Preconditions

- `/next-plan` requires a wrapper-created isolated worktree, its live WorktreeCli
  session claim, and authoritative primary checkout and branch. The session
  starts clean at the same commit as primary; never create or adopt a worktree.
- The canonical sidecars derive the provisioned WorktreeCli, Git common
  directory, primary/session paths and branches, baseline, and wrapper owner.
  Never reconstruct or override those arguments. A missing executable requires
  explicitly authorized primary maintenance through `/compile`.
- Retain the wrapper owner for the selected row's lifecycle and use WorktreeCli
  only through the canonical sidecars for claim and completion.
- When loading `WorktreeCliSessionExclusion.psm1`, pass either an absolute path
  or an explicitly relative path beginning `./` or `.\\`. A bare
  `.agents\\...` value is interpreted as a module name rather than a file path.

## Canonical transitions

Each sidecar emits one JSON result: exit `0` is success, `2` is a deterministic
state blocker, and `1` is malformed input or internal failure. Do not recreate
these transitions with ad hoc Git or WorktreeCli commands.

- Run [`Invoke-NextPlanClaim.ps1`](scripts/Invoke-NextPlanClaim.ps1) with only
  `-Queue plans|features` and optional `-Plan`; retain its immutable claim
  receipt path and SHA-256.
- Run [`New-NextPlanPresentation.ps1`](scripts/New-NextPlanPresentation.ps1)
  with that receipt, a complete execution-card Markdown file under the session
  worktree's ignored `Temp/` directory, and `-FinalizationMode session-landing`.
  `/next-plan` never emits `primary-commit`; that remains a separate explicitly
  requested non-`/next-plan` finalization route. Display every
  returned range in order through `Read-AgentReportSection.ps1` with the exact
  presentation path and SHA-256.
- Only after the user's affirmative response to that display, run
  [`Confirm-NextPlanApproval.ps1`](scripts/Confirm-NextPlanApproval.ps1) with the
  presentation-receipt path and SHA-256. Do not edit implementation files until
  it returns `status: pass` and an approval receipt.
- After implementation checks, run
  [`Complete-NextPlan.ps1`](scripts/Complete-NextPlan.ps1) with the approval
  receipt path and SHA-256. Require `workflowTerminal: false`,
  `nextAction: finalize-changes`, and retain its completion receipt for the
  acceptance ledger and finalization. Immediately run
  [`Test-NextPlanReceiptChain.ps1`](scripts/Test-NextPlanReceiptChain.ps1) with
  `-CompletionReceiptPath` and `-CompletionReceiptSha256` from that completion
  result; require exit `0`, `status: pass`,
  `workflowTerminal: false`, and `nextAction: finalize-changes`. Retain the
  returned presentation, approval, and completion receipt paths/hashes and its
  authoritative `session-landing` `finalizationMode`.

## Workflow

1. Run [`Test-NextPlanWorkflowContract.ps1`](scripts/Test-NextPlanWorkflowContract.ps1)
   and the general `validate-skill` validator for `next-plan`; require both to
   pass before queue selection. Then run `plan order validate` against primary.
   If queue validation fails because an older
   unindexed plan is legitimate, repair it only through WorktreeCli `add`, land
   that queue-only repair, and restart from a fresh session. Never hand-edit an
   Order file or ignore a validation failure.
2. Use `Invoke-NextPlanClaim.ps1` for the requested queue or explicit plan.
   Report its exact dependency, claim, or stale-session blocker; do not fall
   back to manual locking or direct `claim-next` reconstruction.
3. Read the selected plan and current code. Refresh plainly stale citations
   only; do not elaborate the design during refresh. If an implementation step
   or acceptance check is not required by the plan's goal or an existing
   repository contract, identify it for removal at the approval decision rather
   than treating it as implementation authority. If the problem is gone or the
   plan no longer has value, ask whether to retain it for deferral or complete
   it as explicitly approved obsolete work.
4. Classify the actual change and complete its execution card before returning:
   - **Tier 1/2:** state the goal, explicit out-of-scope boundary, affected
     behavior or interfaces, proportionate acceptance checks, and the required
     roles. The plan remains task authority for its later implementation; do
     not create a source packet, provenance ledger, plan-audit, grill, or
     second approval.
   - **Tier 3:** read [Tier 3 preparation](references/tier3-workflow.md)
     completely, then state the goal, out-of-scope boundary, concrete trigger,
     changed interfaces or invariants, acceptance checks with expected
     observations, and required plus conditional roles. Run `/plan-audit` and
     `/external-grill-plan` only when that card leaves a material decision
     unresolved. If an unresolved decision needs user authority, report it as
     the residual after completing every other part of the card.
5. Use the presentation and approval sidecars to present and bind the full
   resolved plan, then obtain implementation approval exactly as the canonical
   execution-gate contract requires. The presentation
   includes every implementation step, out-of-scope boundary, interface or
   invariant, acceptance check with expected observation, role disposition,
   and unresolved decision; claim metadata or an execution card alone is never
   sufficient. Persist the approval receipt before editing code.
6. Retain the row and plan for rejection, deferral, or a blocker. Use
   `Complete-NextPlan.ps1` only for successful execution or user-approved terminal
   cleanup; it removes the plan row/file but deliberately retains the
   owner-held row claim until finalization/landing. For a post-completion row
   query, pass the plan identity relative to the `Order.md` directory (for
   example, `Network/Foo.md` for `Documents/Plans/Network/Foo.md`) and require
   `ownedByRequester: true`; `held: false` is correct only after finalization
   releases the claim. Finalization owns post-landing row unclaim and any
   reconciliation reapply. Before a successful completion, run
   [`Find-PlanClosureReferences.ps1`](../../scripts/Find-PlanClosureReferences.ps1)
   against the fixed session baseline and selected plan. Update every stale
   live Plans/Features citation to the completed plan or a renamed source path,
   then rerun the sweep with no unresolved hits before `complete`.
7. After completion, validate the receipt chain as required by the canonical
   transitions; a missing or non-passing chain is a blocker. Pass the
   completion receipt path/hash and the validator's exact result into
   `/verify-changes`, whose acceptance ledger must bind all three receipt
   paths/hashes and the authoritative finalization mode. Rerun the contract and
   general skill validators as final acceptance checks. Then pass the same
   completion receipt path/hash into `/finalize-changes` and follow states 3
   and 4 of the canonical execution-gate contract. Continue automatically
   through commit preparation, reconciliation, completion reapply, affected
   reverification, and primary-mutation preparation. Only the exact current
   primary-mutation summary may authorize primary history to change.

## Completion report

Return:

```text
Claim: <plan path or none>
Classification: Tier 1 | Tier 2 | Tier 3 and trigger
Route: <fast path | Tier 3 preparation | retained | completed>
Residuals: <blocker or none>

Execution card:
- Goal: <result>
- Out of scope: <boundary>
- Tier trigger: <trigger or none>
- Interfaces and invariants: <affected contracts>
- Acceptance checks: <check and expected observation>
- Roles: <required and conditional roles>
```

Then present the complete resolved plan and ask once whether to implement it.
The execution card is an index into that presentation, not a substitute for it.

For a completed route, continue into `/finalize-changes` instead of returning
this intermediate report. The next user-visible stopping point is the exact
primary-mutation summary required by the canonical execution-gate contract.

## Boundaries

- Does not execute the selected plan before state 2 of the canonical
  execution-gate contract or add a claim/execution-card approval prompt.
- After implementation approval, follows the contract's continuous-execution
  state and pauses only for a safety blocker or exact primary-mutation
  confirmation.
- Does not use research or duplicate reviews to manufacture a reason to keep a
  plan alive.
- Does not create a final evidence ledger unless a later queue mutation,
  reconciliation, or landing requires `/verify-changes`.
