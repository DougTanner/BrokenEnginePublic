<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-08T17:04:02.401Z","dependsOn":[]} -->
# Fix: finalize candidate commit — resumed landing rejects the full authorized path set

## Context
While updating an already prepared landing commit to add a tooling-friction
Plan, the finalizer invoked
`.agents/skills/finalize-changes/scripts/Invoke-FinalizeCandidateCommit.ps1`
with all four caller-authorized landing paths. The completed/deleted Plan and
the two C++ paths were already committed and therefore were not current dirty
entries; only the new tooling-friction Plan was uncommitted. The script returned
`input.path-not-single-entry`. Repeating the candidate step with only the newly
uncommitted Plan succeeded, and approval preparation subsequently squashed both
commits. The missing resumed-invocation contract forced a failed attempt and a
retry during landing preparation.

The claimed active intent was
`Documents/Plans/Network/ServerSessionReadFleetDataRemoval.md`; its `## In scope`
named only deletion of `ServerSession::ReadFleetData` from
`ServerSession.h` and `ServerSession.cpp`. The finalize skill, its references,
and bundled scripts were therefore outside that boundary.

Session provenance (machine-local; not reproducible after cleanup):
- Client: codex
- Session: 3f6fce97-198e-4e6b-94d1-dc020a1fe045
- Session branch: codex/3f6fce97-198e-4e6b-94d1-dc020a1fe045
- Worktree: .codex\worktrees\BrokenEnginePublic\3f6fce97-198e-4e6b-94d1-dc020a1fe045
- Landing commit: `git log --diff-filter=A --format=%H -- <this plan path>`
- Run the review before /cleanup-worktrees removes this worktree: Codex
  transcript discovery requires the producing worktree to remain registered,
  and Claude review requires the exact session id above.

## Design
In a new session, run `/next-plan-review <landing commit>` supplying the
recorded client and session id, root-cause the friction from the proven
transcript, then make the smallest fix inside the `## In scope` boundary below.
If root-causing shows the fix lies outside that boundary, surface it for
re-planning instead of expanding scope.

## Critical files
- `.agents/skills/finalize-changes/SKILL.md` — caller-owned changed-path input
  and resumed normal-workflow contract.
- `.agents/skills/finalize-changes/references/scripts.md` — candidate-commit
  invocation and `-OwnedPaths` contract.
- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeCandidateCommit.ps1`
  — literal authorized-path validation and session candidate creation.

## In scope
- Root-cause investigation via /next-plan-review with the recorded provenance.
- The smallest resulting fix, confined to the finalize skill's changed-path and
  resumed-workflow guidance, the candidate script reference's `-OwnedPaths`
  contract, and the candidate script's authorized-path validation if the
  reviewed contract requires a behavior change.

## Out of scope
- The landed `ServerSession::ReadFleetData` removal and its completed Plan.
- AgentTools candidate binary production, which is owned by
  `Documents/Plans/Agents/AgentToolsCandidateBuildDocumentation.md`.
- Approval preparation, landing-lock, primary-advance, claim-release, and other
  finalization-script behavior.
- Unrelated skills/scripts and any transcript path or transcript text in the
  repository.

## Risk tier and invariants
Expected Tier 2: this is scoped finalization-tool behavior and documentation.
Escalate if the fix changes branch-advance, rollback, locking, or other landing
safety invariants. Never embed transcript paths or home paths. Preserve the
candidate step's guarantees that it stages only authorized current changes,
keeps unrelated state intact, and leaves already committed session work for the
approval-preparation squash.

## Acceptance criteria
- The documented resumed candidate-commit invocation states whether
  `-OwnedPaths` contains only current dirty authorized paths or the session's
  full caller-owned landing set, consistently across the skill, script
  reference, and script behavior.
- Repeating the observed flow after part of the authorized landing set is
  already committed creates the candidate on the first documented invocation,
  without `input.path-not-single-entry` or an ad hoc path-list retry.
- Approval preparation still squashes the earlier prepared work and the resumed
  candidate into the single landing commit without changing unrelated state.
- `/validate-skill` passes for any changed SKILL.md; `plan validate` exits 0.
