<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-04T03:00:16.813Z","dependsOn":[]} -->
# Fix: New-PlanFile.ps1 — folded validation omits an untracked Plan

## Context

During the `/next-plan` claim exit in Codex session `6319f1ba-463a-44c2-84b1-a08e180206a0`, the following exact creation command created `Documents/Plans/Agents/AgentHarnessImGuiTabSelection.md` and returned its folded validation as a pass (`status:valid`, `code:ok`):

```powershell
$RepositoryRoot = (git rev-parse --show-toplevel).Trim(); $Script = Join-Path $RepositoryRoot '.agents/scripts/New-PlanFile.ps1'; pwsh -NoProfile -File $Script -Area Agents -Name AgentHarnessImGuiTabSelection.md -Body (Join-Path $RepositoryRoot 'Temp/AgentHarnessImGuiTabSelection.body.md'); Write-Output "EXIT=$LASTEXITCODE"
```

The exact targeted validation command then reported `plan-not-found` because the new file was untracked and therefore absent from the executable Plan inventory:

```powershell
$exe=(Resolve-Path 'Tools/WorktreeCli/Platforms/VisualStudio2026/Output/WorktreeCli.exe').Path; $repo=(git rev-parse --path-format=absolute --git-common-dir).Trim(); $wt=(git rev-parse --show-toplevel).Trim(); & $exe plan validate --repo $repo --worktree $wt --plan Documents/Plans/Agents/AgentHarnessImGuiTabSelection.md; Write-Output "exit=$LASTEXITCODE"; & $exe plan list --repo $repo --worktree $wt | Select-String -Pattern 'AgentHarnessImGuiTabSelection|status|code|plans|diagnostic' -Context 0,0; Write-Output "listExit=$LASTEXITCODE"
```

Staging only the new Plan and rerunning validation made it appear and validate. This friction was outside the active Water Plan's implementation boundary and did not invalidate its acceptance.

Session provenance (machine-local; not reproducible after cleanup):

- Client: codex
- Session: `6319f1ba-463a-44c2-84b1-a08e180206a0`
- Session branch: `codex/6319f1ba-463a-44c2-84b1-a08e180206a0`
- Worktree: `.codex\worktrees\BrokenEnginePublic\6319f1ba-463a-44c2-84b1-a08e180206a0`
- Landing commit: `git log --diff-filter=A --format=%H -- Documents/Plans/Agents/NewPlanFileUntrackedValidation.md`
- Run the review before `/cleanup-worktrees` removes this worktree: Codex transcript discovery requires the producing worktree to remain registered, and Claude review requires the exact session id above.

## Design

In a new session, run `/next-plan-review <landing commit>` supplying the recorded client and session id, root-cause the friction from the proven transcript, then make the smallest fix inside the `## In scope` boundary below. The review must determine whether `New-PlanFile.ps1` should validate a newly written untracked path explicitly, report a blocked/indeterminate result until the Plan is tracked, or use another existing repository-owned mechanism. If root-causing shows the fix lies in WorktreeCli inventory semantics or another file outside the boundary, surface it for re-planning instead of expanding scope.

## Critical files

- `.agents/scripts/New-PlanFile.ps1:164-193` — Plan write, folded `plan validate` invocation, and result projection that produced the inconsistent pass.

## In scope

- Root-cause investigation via `/next-plan-review` with the recorded provenance.
- The smallest resulting fix confined to `.agents/scripts/New-PlanFile.ps1`'s Plan-write/validation-result path, naming the exact function or region selected by the review.

## Out of scope

- The landed Water shoaling/beach change and `Documents/Plans/Agents/AgentHarnessImGuiTabSelection.md` content.
- WorktreeCli scheduler inventory semantics, staging unrelated paths, scheduler claims, and any other skill/script unless a fresh review returns a re-planning pivot.
- Any transcript path or transcript text in the repository.

## Risk tier and invariants

Expected Tier 2 (scoped tooling behavior); escalate if the fix reaches build/bootstrap coordination. Plan creation remains BOM-less with the byte-zero metadata marker, validation output remains the documented `broken-engine-new-plan-file/v1` envelope, and no unrelated index/worktree paths are staged or changed.

## Acceptance criteria

- The recorded invocation no longer produces a folded pass that conflicts with a targeted WorktreeCli validation: a newly written Plan is either visible and valid in the executable inventory or the result explicitly reports the tracking/validation state as non-passing.
- The workaround of staging only the new Plan is no longer required for the documented creation/validation flow, unless the review proves that staging is an intentional caller-owned prerequisite and updates the result contract accordingly.
- `/next-plan-review` records the proven root cause and its smallest fix within the named script boundary; no WorktreeCli or active Plan behavior is changed.
- `plan validate` exits `0` with `status:valid` and `code:ok` for a valid tracked Plan, and `/validate-skill` is run if any skill file is changed.

## Notes

This Plan is keyed to the concrete script-plus-symptom pair (`.agents/scripts/New-PlanFile.ps1` creating an untracked Plan while folding a validation pass). A later observation of the same pair is a duplicate, not a new residual. The root cause is intentionally deferred to `/next-plan-review`; the current body records the exact observed behavior and workaround without embedding transcript material.
