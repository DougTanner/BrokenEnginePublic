<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-08T16:27:49.462Z","dependsOn":[]} -->
# Fix: Get-NextPlanList.ps1 — WorktreeCli could not list executable Plans

## Context

During the bare `$next-plan` Preconditions and selection step, the documented read-only command

```powershell
pwsh -NoProfile -File .agents/skills/next-plan/scripts/Get-NextPlanList.ps1
```

exited `1` and returned exactly `{"schemaVersion":"broken-engine-next-plan-list-error/v1","status":"error","code":"list.failed","message":"WorktreeCli could not list the executable Plans."}`. This prevented the queue from being seen and stopped Plan selection and claiming. The `/next-plan` instructions require stopping on any non-pass status, so no repair, reordering, retry, or workaround was performed. No Plan was claimed; the friction is outside any claimed Plan boundary.

Session provenance (machine-local; not reproducible after cleanup):

- Client: codex
- Session: `c8764053-eba2-4cb2-a8db-56e40965c858`
- Session branch: `codex/c8764053-eba2-4cb2-a8db-56e40965c858`
- Worktree: `.codex\worktrees\BrokenEnginePublic\c8764053-eba2-4cb2-a8db-56e40965c858`
- Landing commit: `git log --diff-filter=A --format=%H -- Documents/Plans/Agents/GetNextPlanListFailure.md`
- Run the review before `/cleanup-worktrees` removes this worktree: Codex transcript discovery requires the producing worktree to remain registered, and Claude review requires the exact session id above.

## Design

In a new session, run `/next-plan-review <landing commit>` supplying the recorded client and session id, root-cause the friction from the proven transcript, then make the smallest fix inside the `## In scope` boundary below. If root-causing shows the fix lies outside that boundary, surface it for re-planning instead of expanding scope.

## Critical files

- `.agents/skills/next-plan/scripts/Get-NextPlanList.ps1` — documented executable-Plan queue listing and result path.

## In scope

- Root-cause investigation via `/next-plan-review` with the recorded provenance.
- The smallest resulting fix confined to `.agents/skills/next-plan/scripts/Get-NextPlanList.ps1`'s queue-list invocation and result path, naming the exact function or region selected by the review.

## Out of scope

- Any Plan implementation or claim lifecycle operation.
- WorktreeCli scheduler implementation, unrelated skills/scripts, and any fix outside the named script unless a fresh review returns a re-planning pivot.
- Any transcript path or transcript text in the repository.

## Risk tier and invariants

Expected Tier 2 (scoped tool behavior); escalate if the fix reaches WorktreeCli scheduler coordination or build/bootstrap coordination. The documented list result and exit-code contract remain intact, and the fix does not expose determinism/CRC, serialization, replay, wire, runtime allocation, shader, or live-verification state.

## Acceptance criteria

- The recorded `Get-NextPlanList.ps1` invocation no longer reproduces the `list.failed` error and returns the documented executable-Plan inventory result with exit `0` when the scheduler can list the current tree.
- A genuine listing failure remains an explicit documented error rather than being hidden or misreported.
- `/next-plan-review` records the proven root cause and its smallest fix within the named script boundary; no unrelated scheduler or active-Plan behavior changes.
- `plan validate` exits `0` with `status:valid` and `code:ok`; `/validate-skill` is run if any skill file is changed.

## Notes

This Plan is keyed to the concrete script-plus-symptom pair (`.agents/skills/next-plan/scripts/Get-NextPlanList.ps1` returning `list.failed` with `WorktreeCli could not list the executable Plans.`). A later observation of the same pair is a duplicate, not a new residual. The root cause is intentionally deferred to `/next-plan-review`; this body records the exact observed behavior and the blocked selection without embedding transcript material.
