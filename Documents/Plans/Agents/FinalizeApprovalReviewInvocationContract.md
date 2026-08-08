<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-08T19:22:43.570Z","dependsOn":[]} -->
# Fix: finalize-changes approval review — documented invocation omits mandatory parameters

## Context

After a fresh `/verify-changes` PASS on the final diff, the normal workflow's
documented approval-review invocation was run exactly as written:

```powershell
pwsh -NoProfile -File .agents/skills/finalize-changes/scripts/Show-FinalizeApprovalReview.ps1 -LaunchSmartGit
```

The process exited `1` before the script produced its JSON result. PowerShell
returned exactly:

```text
Show-FinalizeApprovalReview.ps1: Cannot process command because of one or more missing mandatory parameters: PrimaryWorktree ApprovedTip.
```

No `manualCommand` or schema/status result was produced, and SmartGit did not
launch. The failure blocked the landing-summary and confirmation flow and
forced rework to record this friction and refresh the prepared diff/review.
The current script declares mandatory `$PrimaryWorktree` and `$ApprovedTip`
parameters at `.agents/skills/finalize-changes/scripts/Show-FinalizeApprovalReview.ps1:28-29`,
while the documented switch-only form is at
`.agents/skills/finalize-changes/SKILL.md:82-90`. No retry with guessed
arguments was performed.

This script/skill boundary is outside the existing
`Documents/Plans/Agents/NextPlanClaimValidationBusy.md` Plan's scope. No
executable Plan was claimed; the existing prepared Plan was not changed or
landed, and primary remained unchanged.

Session provenance (machine-local; not reproducible after cleanup):

- Client: codex
- Session: `04995aca-82b8-40f3-af57-a0f98c78ab46`
- Session branch: `codex/04995aca-82b8-40f3-af57-a0f98c78ab46`
- Worktree: `.codex\worktrees\BrokenEnginePublic\04995aca-82b8-40f3-af57-a0f98c78ab46`
- Landing commit: `git log --diff-filter=A --format=%H -- Documents/Plans/Agents/FinalizeApprovalReviewInvocationContract.md`
- Run the review before `/cleanup-worktrees` removes this worktree: Codex transcript discovery requires the producing worktree to remain registered, and Claude review requires the exact session id above.

## Design

In a new session, run `/next-plan-review <landing commit>` supplying the
recorded client and session id, root-cause the friction from the proven
transcript, then make the smallest fix inside the `## In scope` boundary below.
If root-causing shows the fix lies outside that boundary, surface it for
re-planning instead of expanding scope.

## Critical files

- `.agents/skills/finalize-changes/SKILL.md` — normal workflow Step 4's approval-review invocation and result/error contract (`:82-90`).
- `.agents/skills/finalize-changes/scripts/Show-FinalizeApprovalReview.ps1` — review-result contract and mandatory `$PrimaryWorktree`/`$ApprovedTip` declarations (`:14-30`).

## In scope

- Root-cause investigation via `/next-plan-review` with the recorded provenance.
- The smallest resulting fix confined to the finalize skill's Step 4 invocation/result contract and the approval-review script's parameter/result boundary named above; the review decides whether the documentation or script contract must change.

## Out of scope

- `Documents/Plans/Agents/NextPlanClaimValidationBusy.md`, its prepared candidate, and any implementation or landing of either Plan.
- Primary-branch changes, Plan claims, landing locks, SmartGit behavior itself, runtime code, and unrelated skills/scripts.
- Any transcript path or transcript text in the repository.

## Risk tier and invariants

Expected Tier 2 (scoped tool behavior); escalate if the fix reaches landing
coordination, build/bootstrap coordination, or another shared subsystem. The
approval-review path must remain a single valid
`broken-engine-finalize-approval-review/v1` JSON result when invoked with its
documented arguments, preserve the nonblocking `opened`/`unavailable`/`failed`
outcomes, and keep malformed input as an explicit error. No determinism/CRC,
serialization, replay, wire, runtime allocation, shader, or live-verification
state is exposed.

## Acceptance criteria

- The invocation established by `/next-plan-review` and documented in finalize
  Step 4 successfully binds its parameters and returns the documented
  review-result schema with the expected `status`, `manualCommand`, and
  `message` fields for the available/unavailable SmartGit cases.
- A genuine malformed tip or unresolvable primary worktree remains an explicit
  `status:error` result rather than being hidden or treated as a successful
  review.
- `/next-plan-review` records the proven root cause and its smallest fix within
  the named skill/script contract boundary; if the root cause is outside that
  boundary, it is surfaced for re-planning instead.
- `plan validate` exits `0` with `status:valid` and `code:ok`; `/validate-skill`
  runs if the skill file changes.

## Notes

This Plan is keyed to the concrete finalize skill/script-plus-symptom pair:
the documented switch-only approval-review invocation fails before JSON because
`PrimaryWorktree` and `ApprovedTip` are mandatory. A later observation of the
same pair is a duplicate, not a new residual. The root cause is intentionally
deferred to `/next-plan-review`; this body records the exact error and forced
rework without embedding transcript material.
