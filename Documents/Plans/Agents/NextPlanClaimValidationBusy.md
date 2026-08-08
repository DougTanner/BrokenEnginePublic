<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-08T19:05:33.584Z","dependsOn":[]} -->
# Fix: Invoke-NextPlanClaim.ps1 — plan validation returns busy before claiming

## Context

During the bare `$next-plan` Preconditions and selection stage, the documented
read-only queue command succeeded and identified
`Documents/Plans/Frame/ReplayFullFrameReadStateGate.md` as the oldest eligible
Plan. The exact documented claim command then exited `1` and returned:

```text
{"schemaVersion":"broken-engine-next-plan-claim-result/v3","status":"blocked","code":"plan.validation-failed","message":"Plan validation failed.","claim":null,"validation":{"status":"error","code":"busy"}}
```

The command was:

```powershell
pwsh -NoProfile -File .agents/skills/next-plan/scripts/Invoke-NextPlanClaim.ps1
```

The result stopped the workflow at the validation step before any Plan was
claimed. The manager did not retry, reorder, repair, or claim another Plan, so
no workaround was used. No Plan was claimed in this run; the friction is
outside any claimed Plan boundary, and the active intent was only to select and
prepare the deterministic next executable Plan without changing that target or
claim state.

Session provenance (machine-local; not reproducible after cleanup):

- Client: codex
- Session: `04995aca-82b8-40f3-af57-a0f98c78ab46`
- Session branch: `codex/04995aca-82b8-40f3-af57-a0f98c78ab46`
- Worktree: `.codex\worktrees\BrokenEnginePublic\04995aca-82b8-40f3-af57-a0f98c78ab46`
- Landing commit: `git log --diff-filter=A --format=%H -- Documents/Plans/Agents/NextPlanClaimValidationBusy.md`
- Run the review before `/cleanup-worktrees` removes this worktree: Codex transcript discovery requires the producing worktree to remain registered, and Claude review requires the exact session id above.

## Design

In a new session, run `/next-plan-review <landing commit>` supplying the
recorded client and session id, root-cause the friction from the proven
transcript, then make the smallest fix inside the `## In scope` boundary below.
If root-causing shows the fix lies outside that boundary, surface it for
re-planning instead of expanding scope.

## Critical files

- `.agents/skills/next-plan/scripts/Invoke-NextPlanClaim.ps1` — the documented claim invocation, `plan validate` call, validation projection, and `plan.validation-failed` result path (`:22-24`).

## In scope

- Root-cause investigation via `/next-plan-review` with the recorded provenance.
- The smallest resulting fix confined to `.agents/skills/next-plan/scripts/Invoke-NextPlanClaim.ps1`'s validation invocation, projection, or result path, naming the exact function or region selected by the review.

## Out of scope

- Any Plan implementation or claim lifecycle operation.
- The selected `Documents/Plans/Frame/ReplayFullFrameReadStateGate.md` Plan and its target code.
- WorktreeCli scheduler implementation, unrelated skills/scripts, and any fix outside the named script unless a fresh review returns a re-planning pivot.
- Any transcript path or transcript text in the repository.

## Risk tier and invariants

Expected Tier 2 (scoped tool behavior); escalate if the fix reaches WorktreeCli
scheduler coordination or build/bootstrap coordination. The documented claim
invocation remains a single JSON-producing command that validates the current
Plan inventory before selecting or claiming; it must preserve explicit
no-claim behavior for genuine validation failures and does not expose
determinism/CRC, serialization, replay, wire, runtime allocation, shader, or
live-verification state.

## Acceptance criteria

- The recorded `Invoke-NextPlanClaim.ps1` invocation no longer reproduces the
  `plan.validation-failed` result with nested `validation.code: "busy"` for the
  proven environment, and a valid current inventory reaches the documented
  selection/claim contract.
- A genuine validation failure remains an explicit no-claim stop with a stable,
  actionable result rather than silently selecting or claiming a Plan.
- `/next-plan-review` records the proven root cause and its smallest fix within
  the named script boundary; if the root cause is outside that boundary, it is
  surfaced for re-planning instead.
- `plan validate` exits `0` with `status:valid` and `code:ok`; `/validate-skill`
  runs if any skill file is changed.

## Notes

This Plan is keyed to the concrete script-plus-symptom pair
(`.agents/skills/next-plan/scripts/Invoke-NextPlanClaim.ps1` returning
`plan.validation-failed` with nested validation `status:error` and `code:busy`).
A later observation of the same pair is a duplicate, not a new residual. The
root cause is intentionally deferred to `/next-plan-review`; this body records
the exact observed behavior and blocked claim without embedding transcript
material.
