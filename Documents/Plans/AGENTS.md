# Plans

Tracked refactor and bugfix plans. Capability additions in `../Features/` (see `../Features/AGENTS.md`) are manually executed and are never scheduler inputs.

## Git-backed scheduler

An executable plan starts at byte zero with exactly one metadata line:

`<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-22T18:24:31.042Z","dependsOn":["Documents/Plans/Area/Prerequisite.md"]} -->`

Both keys are mandatory. `createdUtc` is immutable after creation. `dependsOn` is a unique ordinal-sorted list of normalized `Documents/Plans/**/*.md` paths. Every plan document in this tree carries the marker; a missing marker — including one preceded by a BOM, so it is not at byte zero — is a validation error naming the file. `AGENTS.md` and `CLAUDE.md` are exempt at every level of the tree and must never carry metadata.

WorktreeCli is the only component that parses the scheduler and changes claims. It selects the oldest eligible executable plan by `(createdUtc, normalized path)`. Existing valid dependencies block a child; a missing dependency is a satisfied stale edge reported as a notice. Invalid metadata and dependency cycles quarantine only the affected plans, so unrelated plans remain claimable. To quarantine a plan is to exclude it from selection because its metadata is invalid or its dependencies form a cycle, without affecting other plans.

`/next-plan` validates then uses `plan claim-next`. Claims are PC-local, one per session, fixed at 48 hours, and self-heal after expiry/orphaning. An orphaned claim is one whose owning session or worktree no longer exists; self-healing means such a claim is released automatically after expiry. Deferral is `plan unclaim`, which makes the plan immediately eligible again.

Completion uses `plan complete`; explicit rejection uses `plan reject --user-authorized-rejection`. Preparation removes direct child metadata edges and deletes the target in the Git worktree. After landing succeeds, the claim is deleted.

## Plan files

Plans live in area subdirectories, never directly at `Plans/`. An executable plan provides metadata, `# Title`, context, design, critical files, a required `## In scope` section naming the specific functions, members, or regions to change, required `## Out of scope` boundaries, risk triggers/invariants, and observable acceptance criteria when a diff is not decisive. Put directional prerequisites in metadata, not prose.

The two scope sections are the control the finished change is measured against: `/scope-review` treats a changed region no `## In scope` clause covers, or one an `## Out of scope` line names, as unauthorized. A boundary written vaguely is a boundary that cannot be enforced.

A document presenting options rather than a decision-complete implementation does not belong here; it belongs in `../Investigations/` (see `../Investigations/AGENTS.md`) until the decision exists. Work blocked on another change expresses that as a `dependsOn` edge, which the scheduler already honours; work blocked on a decision is not a Plan yet.
