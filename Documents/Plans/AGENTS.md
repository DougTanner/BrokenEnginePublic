# Plans

Tracked refactor and bugfix plans. Capability additions in `../Features/` (see `../Features/AGENTS.md`) are manually executed and are never scheduler inputs.

## Git-backed scheduler

An executable plan starts at byte zero with exactly one metadata line:

`<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-22T18:24:31.042Z","dependsOn":["Documents/Plans/Area/Prerequisite.md"]} -->`

Both keys are mandatory. `createdUtc` is immutable after creation. `dependsOn` is a unique ordinal-sorted list of canonical `Documents/Plans/**/*.md` paths. Every plan document in this tree carries the marker; a missing marker — including one preceded by a BOM, so it is not at byte zero — is a validation error naming the file. `AGENTS.md` and `CLAUDE.md` are exempt at every level of the tree and must never carry metadata.

WorktreeCli is the only scheduler parser and claim mutator. It selects the oldest eligible executable plan by `(createdUtc, canonical path)`. Existing valid dependencies block a child; a missing dependency is a satisfied stale edge reported as a notice. Invalid metadata and dependency cycles quarantine only the affected plans, so unrelated plans remain claimable.

`/next-plan` validates then uses `plan claim-next` with a newly-created receipt below the session worktree's `Temp/`. Claims are PC-local, one per session, fixed at 48 hours, and self-heal after expiry/orphaning. Deferral is `plan unclaim`, which makes the plan immediately eligible again.

Completion uses `plan prepare-completion`; explicit rejection uses `plan prepare-rejection --user-authorized-rejection`. Preparation removes direct child metadata edges and deletes the target in the Git worktree. After landing, `plan release-after-landing` proves terminal state before releasing the claim.

## Plan files

Plans live in area subdirectories, never directly at `Plans/`. An executable plan provides metadata, `# Title`, context, design, critical files, required `## Out of scope` boundaries, risk triggers/invariants, and observable acceptance criteria when a diff is not decisive. Put directional prerequisites in metadata, not prose.

A document presenting options rather than a decision-complete implementation does not belong here; it belongs in `../Investigations/` (see `../Investigations/AGENTS.md`) until the decision exists. Work blocked on another change expresses that as a `dependsOn` edge, which the scheduler already honours; work blocked on a decision is not a Plan yet.
