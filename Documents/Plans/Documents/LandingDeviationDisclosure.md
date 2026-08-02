<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-02T01:11:51.620Z","dependsOn":[]} -->
# Landing Summary Deviation Disclosure

## Context

Two audited landings show that user-approved deviations reached primary without a durable or accurate record:

- Landing `301ad036` (Claude session `128eef4b-34b0-4c4b-b6d9-d6dcd216f5aa`): at 17:59:34Z the user was asked to choose a capacity bound and told 2675 was the only value with a proven-impossible overflow; the landed plan sizes the arena at 1819, and the 19:14:18Z landing summary stated "Two separate passes agreed on it" when the two passes had derived 2675 and 1819 from different cell bounds. The summary also never stated that the user's literal request — "make it start small and grow automatically" — was found infeasible by the dispatched researcher and replaced with a boot-time demand-sized pool.
- Landing `a7280bc9` (Claude session `2180aa65-6fb0-4cc6-833d-49cbefce555d`): the user twice overrode the claimed Plan's `## Out of scope` (18:52:56Z, 19:20:00Z), and `plan complete` then deleted the Plan with that text still forbidding the change that landed. The authorization for the largest part of the diff now exists only in a session transcript.

Root cause: `/finalize-changes` requires a landing summary but requires no disclosure of decisions later evidence superseded, no statement when a delegated verdict replaced the user's literal instruction, and no durable record of post-plan approvals when Plan completion deletes the Plan file.

## Design

Add two required elements to the `/finalize-changes` contract:

1. Landing summary fields: a `Superseded decisions` line for every number or premise the user was previously asked to decide that later evidence overturned (both values plus the deciding evidence), and a `Substituted approaches` line whenever a delegated verdict ruled out a user's stated approach (the verdict and the substitute). `none` when empty, so the field's absence is never ambiguous.
2. Durable record: when the session recorded user approvals that contradict or extend the claimed Plan's scope sections, the squashed landing commit body lists them as `Approved deviations:` lines before `plan complete` deletes the Plan. The Plan file itself stays immutable per the claim contract; the commit body is the durable carrier.

Both are prose obligations on the finalize flow plus the summary-rendering step; no schema or lock behavior changes.

## Critical files

- `.agents/skills/finalize-changes/SKILL.md` — the landing-summary contract and final Plan preparation step.
- `.agents/skills/finalize-changes/scripts/Show-FinalizeApprovalReview.ps1` — summary rendering, if the fields are mechanized there rather than stated as prose obligations.

## In scope

- `SKILL.md`: the two summary fields and the `Approved deviations:` commit-body requirement, each stated once.
- `Show-FinalizeApprovalReview.ps1`: only if summary rendering is script-produced there; otherwise unchanged.

## Out of scope

- The confirmation contract, lock behavior, rebase/advance mechanics, and `/verify-changes`.
- Editing claimed Plan files after approval, and any scheduler metadata change.
- Retroactive rewriting of already-landed commit messages.

## Risk tier and invariants

Tier 1 — documentation/contract additions to one skill with no signature or invariant exposure; if the summary script is edited, the edit is limited to adding output lines.

Invariants: exactly one confirmation still authorizes the landing; the summary the user confirms names every superseded decision; a deleted Plan's post-plan approvals survive in the landing commit body.

## Acceptance criteria

- `/validate-skill` passes on the edited `SKILL.md`.
- The skill text requires both fields with explicit `none` defaults, and requires `Approved deviations:` commit-body lines whenever recorded approvals contradict the completed Plan's scope sections.
