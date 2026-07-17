---
name: next-plan
description: Validates and claims the live primary plan queue through WorktreeCli, refreshes the selected plan against current code, and presents the full resolved plan at the single pre-implementation approval gate. Use when the user invokes `/next-plan`.
disable-model-invocation: true
argument-hint: "[plan-file-path]"
allowed-tools: [Read, Write, Grep, Glob, Agent, Edit, PowerShell, AskUserQuestion, EnterPlanMode, ExitPlanMode]
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
  Never reconstruct or override those arguments; the one sanctioned override is
  the `BROKEN_ENGINE_BASELINE` re-baseline in the primary-advance recovery
  below. A missing executable requires explicitly authorized primary
  maintenance through `/compile`.
- Retain the wrapper owner for the selected row's lifecycle and use WorktreeCli
  only through the canonical sidecars for claim and completion.
- When loading `WorktreeCliSessionExclusion.psm1`, pass either an absolute path
  or an explicitly relative path beginning `./` or `.\\`. A bare
  `.agents\\...` value is interpreted as a module name rather than a file path.

## Canonical transitions

Each sidecar emits one JSON result: exit `0` is success, `2` is a deterministic
state blocker, and `1` is malformed input or internal failure. Do not recreate
these transitions with ad hoc Git or WorktreeCli commands. Run
[`Test-NextPlanWorkflowSidecars.ps1`](scripts/Test-NextPlanWorkflowSidecars.ps1)
whenever these sidecars or the receipt contract change — not per selection.

- Run [`Invoke-NextPlanClaim.ps1`](scripts/Invoke-NextPlanClaim.ps1) with only
  `-Queue plans|features` and optional `-Plan`; retain its immutable claim
  receipt path and SHA-256.
- Run [`New-NextPlanPresentation.ps1`](scripts/New-NextPlanPresentation.ps1)
  with that receipt, a complete execution-card Markdown file under the session
  worktree's ignored `Temp/` directory, and `-FinalizationMode session-landing`.
  `/next-plan` never emits `primary-commit`; that remains a separate explicitly
  requested non-`/next-plan` finalization route. Display every
  returned range in order through `Read-AgentReportSection.ps1` with the exact
  presentation path and SHA-256. The presentation receipt is the last
  pre-approval mutation; obtain the approval response through the plan-mode
  gate below when the host exposes plan mode, otherwise ask the one approval
  question directly.
- Only after the user's affirmative response to that presentation, run
  [`Confirm-NextPlanApproval.ps1`](scripts/Confirm-NextPlanApproval.ps1) with the
  presentation-receipt path and SHA-256. Do not edit implementation files until
  it returns `status: pass` and an approval receipt.
- After implementation checks, run
  [`Complete-NextPlan.ps1`](scripts/Complete-NextPlan.ps1) with the approval
  receipt path and SHA-256. Require `workflowTerminal: false`,
  `nextAction: finalize-changes`, and retain its completion receipt path and
  SHA-256 for the acceptance ledger and finalization. The receipt chain is
  validated exactly once per queue run, by `/finalize-changes`; do not re-run
  `Test-NextPlanReceiptChain.ps1` here or in `/verify-changes`.

## Plan-mode approval gate

On a host that exposes plan mode (Claude Code), the raw range display above is
the contract copy, not the approval prompt. Once every pre-approval
non-read-only step is done — the claim and presentation receipts are written
and the ranges displayed — enter plan mode, write the resolved plan
(execution card, full plan text, out-of-scope boundary, acceptance checks,
and the post-approval route) to the host-designated plan file, and present it
through the plan-approval UI. Plan-mode approval is the affirmative user
response that authorizes `Confirm-NextPlanApproval.ps1`; a rejection or
requested change returns to preparation without binding approval. Hosts
without plan mode keep the direct text prompt. The sidecar receipt chain and
the [canonical contract](references/execution-gates.md) approval semantics are
unchanged either way.

## Primary-advance recovery

The primary branch is expected to keep advancing while a session works — that
is the point of isolated worktrees. Every post-claim sidecar (presentation,
approval, completion, receipt chain) tolerates an advanced primary tip: the
session keeps working at its wrapper baseline and rebases only during
`/finalize-changes` reconciliation, when its verified commit is ready to land.
Do not rebase mid-workflow just because primary moved.

A sidecar exit-`2` blocker stating that primary HEAD advanced from the wrapper
baseline can therefore normally fire only at the claim gate, when primary
moved between wrapper creation and the claim. It is a recoverable state, never
a reason to abandon the session, ask the user, or request a fresh wrapper.
Recover in place, automatically:

1. Stash any working-tree edits (`git stash --include-untracked`; the ignored
   `Temp/` directory is unaffected) and keep them stashed until after the
   re-claim in step 3 — the claim requires a clean session. The session branch
   carries no local commits before finalization; if any exist, first
   `git reset --soft` them back into the working tree and stash those too.
   Then advance the branch to the current primary tip with
   `git rebase <primary-tip>` (a plain fast-forward, since the branch has no
   commits of its own).
2. Re-baseline `BROKEN_ENGINE_BASELINE` to the new primary tip. Environment
   changes do not persist between shell invocations, so set
   `$env:BROKEN_ENGINE_BASELINE = '<primary-tip>'` in the same command line as
   every subsequent sidecar call for the rest of the session, including the
   `/verify-changes` and `/finalize-changes` validators. A forgotten override
   surfaces as the `Session worktree HEAD moved from the wrapper baseline.`
   blocker; re-supply the override rather than treating it as corruption.
3. Receipts hashed against the old baseline are void. If the row was already
   claimed, release it with `WorktreeCli.exe plan row unclaim --repo
   COMMON-DIR --order ORDER --plan ROW-PLAN --owner OWNER` (identities from
   the old claim receipt). Re-run `Invoke-NextPlanClaim.ps1` with an explicit
   `-Plan` naming the previously claimed plan — a bare queue re-claim may
   select a different row after a parallel landing. Then restore the stashed
   edits (`git stash pop`) and re-run any later sidecar whose receipt was
   invalidated.
4. If the user already approved and the regenerated presentation's plan and
   execution-card bytes are identical, the prior approval carries forward per
   the [canonical contract](references/execution-gates.md): display the
   refreshed ranges, note the carried approval, and run
   `Confirm-NextPlanApproval.ps1` without asking again. Ask a new approval
   question only when the rebase changed the plan bytes, execution card,
   scope, or acceptance criteria.
5. Continue the workflow from the state that was blocked.

## Workflow

1. Run `plan order validate` against primary. Skill-text validation runs when
   skills change (per `/validate-skill`'s own trigger), not per selection.
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
     roles. Tier 2 runs `/plan-audit`; Tier 1 skips plan review. The plan
     remains task authority for its later implementation; do not grill or add
     a second approval.
   - **Tier 3:** read [Tier 3 preparation](references/tier3-workflow.md)
     completely, then state the goal, out-of-scope boundary, concrete trigger,
     changed interfaces or invariants, acceptance checks with expected
     observations, and required plus conditional roles. Run `/plan-audit` then
     `/external-grill-plan`. If an unresolved decision needs user authority,
     report it as the residual after completing every other part of the card.

   With the card, persist the `broken-engine-execution-control/v1` JSON
   (`schema`, `stages[]` each with a unique `id` and its `deliverables`;
   deferral candidates carry `deferredQueue`/`deferredPlan`) under the session
   worktree's ignored `Temp/` directory alongside the execution-card Markdown,
   and record its path and SHA-256 — the finalization ledger binds this exact
   record, and root AGENTS.md requires it persisted before implementation, not
   authored at session end.
5. Use the presentation and approval sidecars to present and bind the full
   resolved plan, then obtain implementation approval exactly as the canonical
   execution-gate contract requires — through the plan-mode approval gate when
   the host exposes plan mode. The presentation
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
   [`Find-PlanClosureReferences.ps1`](../../../.agents/scripts/Find-PlanClosureReferences.ps1)
   against the fixed session baseline and selected plan. Update every stale
   live Plans/Features citation to the completed plan or a renamed source path,
   then rerun the sweep with no unresolved hits before `complete`.
7. After completion, pass the completion receipt path/hash into
   `/verify-changes`, whose acceptance ledger binds that receipt identity; the
   full receipt chain is validated once, by `/finalize-changes`. Then pass the
   same completion receipt path/hash into `/finalize-changes` and follow states
   3 and 4 of the canonical execution-gate contract. Continue automatically
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

Then present the complete resolved plan — through the plan-mode approval gate
when available — and ask once whether to implement it. The execution card is an
index into that presentation, not a substitute for it.

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
