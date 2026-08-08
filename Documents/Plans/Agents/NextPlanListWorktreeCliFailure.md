<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-08T16:27:47.886Z","dependsOn":[]} -->
# Fix: Get-NextPlanList.ps1 — mandatory queue listing fails before Plan selection

## Context

During the bare `$next-plan` run, the exact documented command

```powershell
pwsh -NoProfile -File .agents/skills/next-plan/scripts/Get-NextPlanList.ps1
```

exited `1` and emitted exactly:

```text
{"schemaVersion":"broken-engine-next-plan-list-error/v1","status":"error","code":"list.failed","message":"WorktreeCli could not list the executable Plans."}
```

The script's documented queue-list precondition therefore failed before
selection or claim, and the run stopped without a workaround or retry. The
observed error mapping is at `.agents/skills/next-plan/scripts/Get-NextPlanList.ps1:9-15`,
where the script invokes WorktreeCli `plan list` and converts a nonzero exit to
`list.failed`. This friction is outside any claimed Plan because this run held
no claim and its active intent was only to select and prepare the next eligible
Plan.

Session provenance (machine-local; not reproducible after cleanup):

- Client: codex
- Session: `011b9fee-a1f4-422f-9060-0f4f72501b68`
- Session branch: `codex/011b9fee-a1f4-422f-9060-0f4f72501b68`
- Worktree: `.codex\worktrees\BrokenEnginePublic\011b9fee-a1f4-422f-9060-0f4f72501b68`
- Landing commit: `git log --diff-filter=A --format=%H -- Documents/Plans/Agents/NextPlanListWorktreeCliFailure.md`
- Run the review before `/cleanup-worktrees` removes this worktree: Codex transcript discovery requires the producing worktree to remain registered, and Claude review requires the exact session id above.

## Design

In a new session, run `/next-plan-review <landing commit>` supplying the
recorded client and session id, root-cause the friction from the proven
transcript, then make the smallest fix inside the `## In scope` boundary below.
If root-causing shows the fix lies outside that boundary, surface it for
re-planning instead of expanding scope.

## Critical files

- `.agents/skills/next-plan/scripts/Get-NextPlanList.ps1` — `plan list` invocation and nonzero-result mapping at lines 9-15.

## In scope

- Root-cause investigation via `/next-plan-review` with the recorded provenance.
- The smallest resulting fix confined to `.agents/skills/next-plan/scripts/Get-NextPlanList.ps1`'s queue-list invocation/result path, naming the exact region selected by the review.

## Out of scope

- The unclaimed `$next-plan` selection/claim run and every executable Plan it would have considered.
- WorktreeCli scheduler/list implementation, `NextPlanWorkflowCommon.psm1`, unrelated skills/scripts, and any other file unless a fresh review returns a re-planning pivot.
- Any transcript path or transcript text in the repository.

## Risk tier and invariants

Expected Tier 2 (scoped tool behavior); escalate if the fix reaches
build/bootstrap coordination. The documented queue-list command remains
read-only, emits one versioned JSON result, preserves WorktreeCli's listing
contract, and never selects or claims a Plan while listing fails.

## Acceptance criteria

- The recorded documented invocation no longer exits `1` with `code: "list.failed"` for the proven environment, and it returns the documented executable-Plan listing envelope needed for selection.
- A genuine WorktreeCli listing failure remains an explicit no-claim stop with a stable, actionable result rather than silently selecting or claiming a Plan.
- `/next-plan-review` records the proven root cause and its smallest fix within the named script boundary; if the root cause is outside that boundary, it is surfaced for re-planning instead.
- `plan validate` exits `0` with `status:valid` and `code:ok`, and `/validate-skill` runs if any skill file is changed.

## Notes

This Plan is keyed to the concrete script-plus-symptom pair
(`.agents/skills/next-plan/scripts/Get-NextPlanList.ps1` returning
`list.failed` while WorktreeCli could not list executable Plans). A later
observation of the same pair is a duplicate, not a new residual. Root cause is
intentionally deferred to `/next-plan-review`; this body records the exact
observed behavior and no workaround without embedding transcript material.
