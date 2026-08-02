---
name: next-plan
description: Validates and deterministically claims one Git-backed Documents/Plans Plan through WorktreeCli, resolves it against current code, and presents the resolved Plan and execution card for implementation approval; preparation-proven Tier-1 work continues without that pause. Use only when the latest user request explicitly invokes `/next-plan` or `$next-plan`.
disable-model-invocation: true
argument-hint: "[Documents/Plans/... | partial pattern]"
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
- Any other argument is a case-sensitive partial match against executable Plan
  paths relative to `Documents/Plans/`; exactly one match selects that Plan,
  and zero or multiple matches block.

See the queue before selecting, whether the invocation is bare or names a Plan:
`pwsh -NoProfile -File .agents/skills/next-plan/scripts/Get-NextPlanList.ps1`
takes no arguments, changes nothing, and reports every executable Plan with its
state and creation order. For a tier-constrained request, read the `Risk tier`
prose of the top eligible candidates in that order until one matches, then claim
that path.

Keep the process current directory at the session worktree root for every
bundled script invocation; never change into `.agents/skills/next-plan` or
treat its `scripts/...` path as a working-directory instruction. For bare
selection, run this command with no `-Plan` argument:
`pwsh -NoProfile -File .agents/skills/next-plan/scripts/Invoke-NextPlanClaim.ps1`
For a requested normalized path or partial pattern, append `-Plan` and quote
that value, for example:
`pwsh -NoProfile -File .agents/skills/next-plan/scripts/Invoke-NextPlanClaim.ps1 -Plan 'Documents/Plans/example.md'`
(`-Plan 'example.md'` forwards a partial pattern.) Run the bundled script as its
own shell call, never combined with other commands, so its single JSON object
stays parseable and the mutation-capable script is never re-run just to
disambiguate its output. Do not reconstruct the script's transitions. On
`status: pass`, act on the code: `ok` and `reused` both mean this session holds
the named claim. For a bare selection, `none-available` is a normal whole-skill
stop with nothing to claim, and selection is not re-run to look again. For a
`-Plan`-targeted invocation it means only that the requested Plan is ineligible,
so the manager may select or claim a different candidate in the same turn. Any
other status stops the skill without repair, reordering, or retry.

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
assignments, acceptance criteria, and unresolved decisions.

On Codex, present that complete spec as exactly one `<proposed_plan>` markdown
block — opening and closing tags each on their own line, at most one block per
turn, and only when the spec is complete — then end the turn without asking an
approval question, because the Codex client's own "Implement this plan?" prompt
collects the decision. Any revision is a new complete replacement block.

On Claude Code and every other host, deliver the full presentation as ordinary
rendered markdown in the final message of a completed turn, with the approval
question as the last line of that same message and no tool call after that text
— question tools included, because text emitted before a question-tool call may
never be displayed. The user's next message is the decision. A question UI is
allowed only in a later turn, after the presentation is already visible, and
only for short follow-up choices.

An affirmative response approves only the latest unchanged presentation. A
meaningful Plan, card, scope, invariant, acceptance, or decision change requires
a new complete presentation.

Per the claimed-executable-Plan paragraph after the Change Workflow steps in
root [AGENTS.md](../../../AGENTS.md), preparation that proves the Plan Tier 1,
decision-complete, and current continues straight into implementation without
this pause; main records those facts. The landing confirmation in
`/finalize-changes` always applies.

## Claim lifecycle

Before landing-commit creation, an `implementer` runs
`pwsh -NoProfile -File .agents/skills/next-plan/scripts/Complete-NextPlan.ps1`
with no arguments for completion or appends `-Reject` only after explicit
user-authorized rejection. Success removes only direct-child dependency edges,
deletes the selected Plan in the worktree, reports the `changedPaths` the
landing commit must contain, and returns `nextAction: finalize-changes`. The
claim stays held until landing succeeds.

Deferral uses
`pwsh -NoProfile -File .agents/skills/next-plan/scripts/Defer-NextPlan.ps1`
and only an ordinary live claim. After final preparation has run, deferral
requires an explicit user instruction given in the current session, recorded in
the handoff; nothing else unlocks it.
`/finalize-changes` deletes the claim after primary advances. Run
`pwsh -NoProfile -File .agents/skills/next-plan/scripts/Test-NextPlanWorkflowScripts.ps1 -Executable '<worktree-cli-path>'`
only when one of those scripts changes; substitute the provisioned `WorktreeCli`
path resolved by `Get-NextPlanContext` during Preconditions and selection for
`<worktree-cli-path>` and never pass the placeholder literally.

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
