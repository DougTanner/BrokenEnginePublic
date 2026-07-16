# Features

Implementation plans for brand-new additions — work that gives the engine a capability it didn't have. Counterpart queue: `../Plans/AGENTS.md` (refactors/bugfixes). The deciding test and the canonical scoring anchors are in `../AGENTS.md`.

## Index File

Feature work may use the checkout the user supplied. `plan order` selection, mutation, completion, and primary landing normally use a wrapper-created worktree and live WorktreeCli claim. An explicitly user-authorized `primary-commit` may adopt a clean primary checkout under the root finalization contract.

`Order.md` is the live feature queue. Its machine-owned executable table and `Depends On` column use the exact format and normalized cross-queue identities defined in `../Plans/AGENTS.md`. WorktreeCli alone parses, validates, or mutates executable rows. Approximate score order remains a loose tiebreaker, not a diagnosed invariant. Plans live in area subdirectories such as `Audio/`, `Engine/`, `Frame/`, `Graphics/`, and `Network/`.

The **Reference / Index Documents** table remains unscored and non-executable. WorktreeCli validates its paths and keeps those documents out of scheduling and orphan detection.

## Rules

Queue lifecycle, scoring, structured dependencies, Coordination policy, and WorktreeCli commands are identical to `../Plans/AGENTS.md`. The clean primary/root checkout is the live queue; add/update/complete changes in a registered session worktree are proposed state until verified landing advances primary. An explicitly user-authorized primary commit uses the same receipt, committed-primary validation, and owner-unclaim requirements as Plans. New feature files are written first and then indexed through a structured WorktreeCli add request. A failed add leaves a retryable session orphan, not live primary state. Never edit executable rows directly or put claim metadata in `Order.md` or plan files.

Directional cross-queue prerequisites use full normalized `Documents/Plans/...` or `Documents/Features/...` identities in `dependsOn`/`Depends On`. Mandatory nondirectional constraints use reciprocal `## Coordination` sections updated atomically for all existing counterparts. Ordinary overlap may remain a nonblocking one-sided warning in a plan body.

- A plan that turns out to be a refactor/bugfix in disguise (no new capability) moves to `Documents/Plans/` through the corresponding WorktreeCli queue mutation.
- Designs deferred on YAGNI grounds are scored normally but carry explicit "Revisit When" trigger conditions in the plan body — the `Frame/Future_*.txt` files are the pattern.

## Plan File Authoring

New plans follow the `../Plans/AGENTS.md` authoring conventions (heading shape, required `## Out of scope` section, risk trigger, and interface names rather than bare paths) and use `.md`. Internal numbered design steps remain plan-local. Heading structure is not enforced for `.txt` plans.
