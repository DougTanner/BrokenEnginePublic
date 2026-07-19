---
name: next-plan
description: Validates and claims the live primary plan queue through WorktreeCli, reads the selected plan against current code, and presents the full resolved plan at the single pre-implementation approval gate. Use when the user invokes `/next-plan`.
disable-model-invocation: true
argument-hint: "[plan-file-path]"
allowed-tools: [Read, Write, Grep, Glob, Agent, Edit, PowerShell, AskUserQuestion, EnterPlanMode, ExitPlanMode]
---

# Next Plan

Claim the first eligible live plan from the machine-local primary queue, confirm
it remains relevant, then use its risk tier to prepare and present the complete
resolved plan. Follow the [canonical execution-gate
contract](references/execution-gates.md) from claim through primary mutation;
do not stop after the claim or present only a claim/execution-card summary.
WorktreeCli owns queue parsing, locking, selection, and row mutation; the queue
is machine-local state, and this skill never parses or edits an executable row.

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
whenever these sidecars change — not per selection.

- Run [`Invoke-NextPlanClaim.ps1`](scripts/Invoke-NextPlanClaim.ps1) with only
  `-Queue plans|features` and optional `-Plan`.
- Implementation approval is obtained through the plan-mode approval gate
  below (or the direct question on hosts without plan mode) and lives in the
  transcript; there is no presentation or approval artifact to generate.
- After implementation checks, run
  [`Complete-NextPlan.ps1`](scripts/Complete-NextPlan.ps1) with `-Plan` naming
  the completed plan. Require `workflowTerminal: false` and
  `nextAction: finalize-changes`.

## Plan-mode approval gate

On a host that exposes plan mode (Claude Code), once preparation is done, enter
plan mode, write the resolved plan (execution card, full plan text,
out-of-scope boundary, acceptance checks, and the post-approval route) to the
host-designated plan file, and present it through the plan-approval UI.
Plan-mode approval is the affirmative user response; a rejection or requested
change returns to preparation. Hosts without plan mode ask the one approval
question directly. The [canonical contract](references/execution-gates.md)
approval semantics apply either way.

## Primary-advance recovery

The primary branch is expected to keep advancing while a session works — that
is the point of isolated worktrees. The session keeps working at its wrapper
baseline and rebases only during `/finalize-changes` reconciliation, when its
verified commit is ready to land. Do not rebase mid-workflow just because
primary moved; an advance voids nothing (see the canonical contract, state 3).

The one blocker an advance can cause is at the claim gate, when primary moved
between wrapper creation and the claim. Recover in place, automatically:

1. Advance the branch to the current primary tip with
   `git rebase <primary-tip>` (a plain fast-forward, since the branch has no
   commits of its own). The claim no longer requires a clean session tree, so
   there is no stash step; `plan-byte-mismatch` is the sole plan-content guard
   (`git-operation-in-progress` still applies).
2. Re-baseline `BROKEN_ENGINE_BASELINE` to the new primary tip. Environment
   changes do not persist between shell invocations, so set
   `$env:BROKEN_ENGINE_BASELINE = '<primary-tip>'` in the same command line as
   every subsequent sidecar call for the rest of the session. A forgotten
   override surfaces as the
   `Session worktree HEAD moved from the wrapper baseline.` blocker;
   re-supply the override rather than treating it as corruption.
3. Claim and continue.

After the claim, a plan file landing on primary can surface as a
`missing-plan-file` notice when the session's older tree cannot see it. That
stale-baseline notice is non-blocking at every `ok: true` gate and is resolved
by reconciliation — see the canonical contract's state 3; it never justifies a
mid-workflow rebase or a stop for user direction.

## Workflow

1. Run `plan order validate` against primary. Skill-text validation runs when
   skills change (per `/validate-skill`'s own trigger), not per selection.
   An unindexed plan file is now a non-blocking `orphan-plan` notice, not a
   validation failure; if such a plan is legitimate, submit its `add` against the
   machine-local store through WorktreeCli, revalidate, and re-claim. Never
   hand-edit a queue row or ignore a genuine validation failure.
2. Use `Invoke-NextPlanClaim.ps1` for the requested queue or explicit plan.
   Report its exact dependency, claim, or stale-session blocker; do not fall
   back to manual locking or direct `claim-next` reconstruction.
3. Read the selected plan and current code. Never edit the claimed plan file
   during execution — completion deletes it and a modified plan file blocks the
   completion sidecar. Locate code by symbol identity and carry materially stale
   citation corrections in the execution card; edit the plan file only when the
   user chooses deferral or retention. If an implementation step
   or acceptance check is not required by the plan's goal or an existing
   repository contract, identify it for removal at the approval decision rather
   than treating it as implementation authority. If the problem is gone or the
   plan no longer has value, ask whether to retain it for deferral or complete
   it as explicitly approved obsolete work.
4. Classify the actual change and complete its execution card before returning.
   Every card, at every tier, opens with two plain-language sections above its
   fields: `### What does this plan do?` then
   `### Why this is good for the codebase`, 2-4 sentences each. Write them so a
   reader follows them without opening the plan — no engine or process jargon
   (no tier numbers, baselines, CRC, SOA, or collection and subsystem
   type names). Then complete the fields:
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

   `/plan-audit` runs in a delegated Fable subagent; never invoke it in this
   session's own context. Its bounds are written for that reviewer, and an
   inline invocation applies them here instead. Replace a running audit reviewer
   only under the liveness and interruption contract in
   [subagent-reporting.md](../../references/subagent-reporting.md) — a wait
   boundary or elapsed time alone never justifies replacement; request partial
   findings first, and seed a justified replacement with them rather than
   restarting exploration. `/external-grill-plan` is the opposite: it interviews
   the user, so it runs in this session.

   If an `Agent` spawn is denied, stop and report it to the user before running
   the review anywhere. Do not silently continue in this context: a review that
   loses its fresh context is a degraded review, and the denial itself is
   evidence of a misconfiguration worth fixing. When the user directs the work
   to continue inline, carry `review freshness degraded` as a named residual
   through the acceptance table.
5. Present the full resolved plan and obtain implementation approval exactly as
   the canonical execution-gate contract requires — through the plan-mode
   approval gate when the host exposes plan mode. The presentation includes
   every implementation step, out-of-scope boundary, interface or invariant,
   acceptance check with expected observation, role disposition, and unresolved
   decision; claim metadata or an execution card alone is never sufficient.
6. Retain the row and plan for rejection, deferral, or a blocker. Use
   `Complete-NextPlan.ps1` only for successful execution or user-approved terminal
   cleanup. In session it runs the closure scan and deletes the plan file as an
   ordinary tracked deletion (`git rm`); it does not remove the queue row and
   does not validate in session. The owner-held row claim stays live and the row
   stays present (invisible to other claim-next calls) until landing removes it.
   For a post-completion row query, pass the plan identity relative to the queue
   directory (for example, `Network/Foo.md` for `Documents/Plans/Network/Foo.md`)
   and require `ownedByRequester: true`; `held: false` is correct only after
   finalization releases the claim. Finalization owns the post-landing row
   removal and unclaim (`plan order complete` against the machine-local queue,
   tolerant of the already-deleted plan file). Before completion, run
   [`Find-PlanClosureReferences.ps1`](../../../.agents/scripts/Find-PlanClosureReferences.ps1)
   against the fixed session baseline and selected plan. Update every stale
   live Plans/Features citation to the completed plan or a renamed source path,
   then rerun the sweep with no unresolved hits before `complete`.
7. After completion, run `/verify-changes` for the acceptance table, then
   continue into `/finalize-changes` and follow states 3 and 4 of the canonical
   execution-gate contract. Continue automatically through commit preparation,
   reconciliation, and landing preparation; the queue row publishes post-landing.
   Only the landing confirmation may authorize primary history to change.

## Completion report

Return:

```text
Claim: <plan path or none>
Classification: Tier 1 | Tier 2 | Tier 3 and trigger
Route: <fast path | Tier 3 preparation | retained | completed>
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

Then present the complete resolved plan — through the plan-mode approval gate
when available — and ask once whether to implement it. The execution card is an
index into that presentation, not a substitute for it; its two plain-language
sections are the readable way in, never a scope authority. The complete resolved
plan governs.

For a completed route, continue into `/finalize-changes` instead of returning
this intermediate report. The next user-visible stopping point is the landing
confirmation required by the canonical execution-gate contract.

## Boundaries

- Does not execute the selected plan before state 2 of the canonical
  execution-gate contract or add a claim/execution-card approval prompt.
- After implementation approval, follows the contract's continuous-execution
  state and pauses only for a safety blocker or the landing confirmation.
- Does not use research or duplicate reviews to manufacture a reason to keep a
  plan alive.
