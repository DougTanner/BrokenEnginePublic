---
name: next-plan
description: Validates and deterministically claims one Git-backed Documents/Plans Plan through WorktreeCli, resolves it against current code, and presents it at the single implementation-approval gate. Use only when the latest user request explicitly invokes `/next-plan` or `$next-plan`.
disable-model-invocation: true
argument-hint: "[Documents/Plans/... | filename.md]"
allowed-tools: [Read, Write, Grep, Glob, Agent, Edit, PowerShell, AskUserQuestion]
---

# Next Plan

Claim one executable Plan from tracked `Documents/Plans` metadata, confirm it remains relevant, and
present the complete resolved plan. Follow the canonical execution-gate contract
(`references/execution-gates.md`) through landing. WorktreeCli alone owns
metadata validation, claim locking, selection, terminal preparation, and release.
`Documents/Features` is manual and never participates in scheduler state.

The main session reads this skill, retains user intent, dispatches or resumes
bounded workers, adjudicates their concise handoffs, runs the user-facing Tier 3
grill, and presents the approval and landing gates. One `implementer` performs
claim and repository-backed preparation; later mechanics run in their assigned
workers. Workers never delegate and return any separate-role requirement to the
main session.

## Invocation and preconditions

- Start only when the latest user request explicitly invokes `/next-plan` or
  `$next-plan`.
  Once claimed, later approval or blocker-resolution turns continue that active
  workflow; an invocation earlier in unrelated history does not start one.
- Bare invocation selects the oldest eligible Plan by immutable `createdUtc`
  then canonical UTF-8 path. A canonical `Documents/Plans/...` argument selects
  that exact Plan. A filename-only argument selects its exact case-sensitive
  leaf-name match only when one validated executable Plan has it; zero or
  duplicate matches block. Reject Features and every other path shape.
- Require a wrapper-created isolated worktree, authoritative primary
  checkout/branch, and a clean session tree. Never create or adopt a worktree.
- Derive WorktreeCli, Git identities, baseline, and owner through
  `Get-AgentWorktreeSessionProvenance` from the in-worktree receipt and canonical
  sidecars. Missing tooling requires explicitly authorized primary maintenance
  through `/compile`.
- Retain the durable session owner and Temp receipt for the claim lifecycle.
  Never inspect or edit machine-local claims directly.

## Canonical sidecars

Each sidecar emits one JSON result: exit `0` succeeds, `2` is a deterministic
state blocker, and `1` is malformed input or internal failure. Do not recreate
their transitions with ad hoc WorktreeCli commands.

- `scripts/Invoke-NextPlanClaim.ps1` accepts an optional canonical `-Plan` or
  filename-only `-Plan`. Resolve a filename only after validation finds exactly
  one case-sensitive executable leaf-name match; zero or duplicate matches
  block without a claim. It verifies the provisioned WorktreeCli is a nonempty
  ordinary file; requires a clean session tree; reuses and digest-checks an
  owned deterministic receipt; authoritatively retires only an already-absent
  stale receipt; writes a new deterministic receipt beneath session `Temp`; and
  internally verifies claimed Plan bytes.
- `scripts/Complete-NextPlan.ps1` discovers the deterministic local receipt.
  Completion invokes `plan prepare-completion`;
  explicit user-authorized rejection invokes `plan prepare-rejection`. Require
  `workflowTerminal: false` and `nextAction: finalize-changes`. On success it
  persists the original receipt-bound terminal result and manifest digest at
  `Temp/next-plan-terminal-result.json`; later candidate creation validates this
  proof and never replays terminal preparation.
- `scripts/Defer-NextPlan.ps1` discovers the deterministic local receipt and
  releases only an ordinary live claim. It never defers `awaiting-landing`.
- Run `scripts/Test-NextPlanWorkflowSidecars.ps1`
  when a sidecar changes, never during ordinary selection.

## Workflow

Required order: terminal preparation -> candidate creation -> reconciliation/single-parent squash -> exact candidate verification -> finalization summary and explicit confirmation -> primary mutation.

1. Main dispatches one preparation `implementer` to run the claim sidecar for
   the resolved Plan. It validates primary metadata
   against the wrapper baseline before claiming. Treat `none-available` as a
   successful normal result. Report and stop on invalid metadata, quarantined
   cycles, dependency blockers, stale sessions, or claim conflicts; never add,
   repair, or reorder Plans during selection. Missing dependency paths are
   satisfied stale-edge notices; existing manual or invalid dependencies block.
2. The preparation worker reads the selected plan and current code. Treat every
   plan claim — paths, symbols, cited lines, described current behavior — as a hypothesis to confirm
   against the current tree; when reality contradicts the plan, reality wins.
   Keep the claimed plan immutable. Invoke `Invoke-NextPlanClaim.ps1`
   idempotently before the first `/plan-audit` or, for Tier 1, before
   presentation. Its hidden Plan-byte integrity gate blocks a mismatch; retain
   the claim and stop without review, presentation, or re-claim.
   Carry stale citation corrections in the execution card. Remove unnecessary
   steps/checks from the resolved presentation rather than granting them
   authority. If the problem is gone, return the decision requirement so main
   can ask whether to retain the plan or approve terminal cleanup as obsolete
   work. The byte-zero marker and `createdUtc`
   remain immutable.
3. The preparation worker classifies the actual change and prepares the
   execution card. Every card starts with `### What does this plan do?` and
   `### Why this is good for the codebase`, each in 2-4 plain sentences without
   process jargon. Then state goal, out-of-scope boundary, tier trigger,
   interfaces/invariants, acceptance checks with expected observations, and
   required/conditional roles.
   - Tier 1 skips plan review.
   - For Tier 2, main dispatches `/plan-audit` to one `reviewer` whose prompt
     carries the required delegation-basis and context records and the bounded brief fields
     (`../../references/subagent-reporting.md`).
    - For Tier 3, the preparation worker follows
      `references/tier3-workflow.md`.
   If mandatory reviewer delegation is unavailable, report a blocker; never
   substitute inline or same-context review.
4. The preparation worker invokes `Invoke-NextPlanClaim.ps1` idempotently
   immediately before its final handoff. Main presents the complete
   resolved plan from that handoff at the one approval gate
   under the canonical approval contract. It includes the full execution card,
   implementation step, boundaries, interfaces/invariants, acceptance checks,
   role dispositions, and unresolved decisions. Any material presentation
   change invalidates approval and requires a new complete presentation.
5. After approval, main dispatches bounded workers and continues through
   propagation, targeted checks, domain review, accepted fixes, hygiene, and acceptance verification. Every
   implementer delegation prompt carries the required delegation-basis and
   context records, the bounded brief fields
   (`../../references/subagent-reporting.md`), and the truth-grounding
   guardrail: plan and execution-card claims are hypotheses — on contradiction
   with the actual code, trust the code and return the contradiction to the
   manager rather than forcing the plan's description. Pause only for a safety
   blocker or the final landing confirmation. Never edit the claimed plan.
6. Before final evidence, an `implementer` invokes `Complete-NextPlan.ps1` with no
   arguments for completion or only `-Reject` for explicit user-authorized
   rejection. Its hidden receipt and Plan-byte checks block a mismatch and
   leave the Plan and claim intact. The sidecar writes a recoverable
   manifest, deletes the Plan, removes only direct child dependency edges, and
   leaves the receipt-bound claim `awaiting-landing`. Rejection follows the same
   path only with explicit user authority.
7. After terminal preparation, the finalization `implementer` creates the
   authorized candidate and reconciles/squashes it to one exact single-parent
   candidate commit/tree. Main dispatches `/verify-changes` to a fresh read-only
   reviewer only after that candidate exists; missing, non-commit, wrong-parent,
   wrong-tree, or changed-tip identity blocks rather than waiving evidence.
   `/finalize-changes` then consumes the verified candidate, completes the v2
   audit decision and finalization preparation, and returns the exact landing
   summary. Main presents it and obtains the canonical landing confirmation.
   Only after that confirmation does main resume the worker to advance primary
   to the exact verified candidate. The mandatory order is terminal preparation
   -> candidate creation -> reconciliation/single-parent squash -> exact
   candidate verification -> finalization summary and explicit confirmation ->
   primary mutation.

## Primary advance

A preparation or finalization `implementer`, according to the active stage,
performs the mechanics in this section and returns blockers or decision needs to
main; main does not inspect or mutate repository or claim state.

A primary advance never blocks or reroutes a claim. `claim-next` selects Plans
from the session worktree tree and requires the session `HEAD` to be an ancestor
of (or equal to) the primary tip, so a fresh or behind session claims with no
pre-claim rebase or re-baseline. An `ok: true` stale-baseline `missing-plan-file`
notice is non-blocking, and `/finalize-changes` reconciliation resolves the
advance. After claim, a primary advance does not by itself invalidate the
receipt. Deferral invokes `Defer-NextPlan.ps1`, making the Plan immediately
eligible again; never defer an `awaiting-landing` terminal receipt.

If instead primary may have been squash-rewritten — the cheap mid-session trigger
is a failed `git merge-base --is-ancestor $env:BROKEN_ENGINE_BASELINE <primary
tip>` with the branch name unchanged, though the sidecar decides authoritatively
from the reflog-derived fork point — recover automatically without prompting by
running
`Repair-AgentWorktreeSquashedBaseline.ps1` with `-RepositoryRoot`
`$env:BROKEN_ENGINE_PRIMARY_CHECKOUT`, `-Worktree`
`$env:BROKEN_ENGINE_WORKTREE_PATH`, and `-WorktreeCliExecutable` the primary
`WorktreeCli.exe`. It re-parents the session and rewrites the durable wrapper
receipt; re-export `BROKEN_ENGINE_BASELINE` to the reported `newBaseline` and
continue through deterministic receipt discovery. Run the repair before any `plan validate` or
`claim-next`. A `reparented-conflict` is either a mid-rebase conflict
(`rebaseInProgress` true — resolve, `git rebase --continue`, no scheduler op until
it completes) or an autostash pop-conflict after a completed rebase
(`rebaseInProgress` false — resolve, `git stash drop`, scheduler ops safe); see
`references/execution-gates.md` for the full clause.

## Handoff before approval

```text
Claim: <Plan path or none; internally resolved state when claimed>
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

The card indexes the complete resolved Plan; it never substitutes for it. A
rejection prepares terminal deletion only with explicit authority. Deferral
releases the claim; a blocker or digest mismatch retains the claim and Plan.
