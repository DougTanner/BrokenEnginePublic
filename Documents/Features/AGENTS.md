# Features

Implementation plans for brand-new additions — work that gives the engine a capability it didn't have. Counterpart queue: `../Plans/AGENTS.md` (refactors/bugfixes). The deciding test and the canonical scoring anchors are in `../AGENTS.md`.

## Index File

Feature work may use the checkout the user supplied. `plan order` selection, mutation, completion, and primary landing normally use a wrapper-created worktree and live WorktreeCli claim. An explicitly user-authorized `primary-commit` may adopt a clean primary checkout under the root finalization contract.

The feature queue is machine-local state under `%LOCALAPPDATA%\BrokenEngineLocks\plan-queue-state\<sha256(repo-common-dir)>\Features-Order.md`, seeded by `plan order init`; no `Order.md` lives in this tree. Its executable table and `Depends On` column use the exact format and normalized cross-queue identities defined in `../Plans/AGENTS.md`. WorktreeCli alone parses, validates, or mutates executable rows. Approximate score order remains a loose tiebreaker, not a diagnosed invariant. Plans live in area subdirectories such as `Audio/`, `Engine/`, `Frame/`, `Graphics/`, and `Network/`.

The **Reference / Index Documents** table remains unscored and non-executable. WorktreeCli validates its paths and keeps those documents out of scheduling and orphan detection.

## Rules

Queue lifecycle, scoring, structured dependencies, Coordination policy, direct plan-file editing, and WorktreeCli commands are identical to `../Plans/AGENTS.md`. The machine-local queue store is the live queue; `add` and `complete` from a registered session worktree publish at landing, becoming live only after it advances primary, while a row `update` submits immediately and returns its own receipt. An explicitly user-authorized primary commit uses the same committed-primary validation and owner-unclaim requirements as Plans and mutates the queue immediately. New feature files are written first and their row staged for the post-landing add; the claim requires no clean session or primary tree (`plan-byte-mismatch` is the sole plan guard). No landing means no rows and no orphans. Never edit an executable row directly or put claim metadata in a plan file.

Directional cross-queue prerequisites use full normalized `Documents/Plans/...` or `Documents/Features/...` identities in `dependsOn`/`Depends On`. Mandatory nondirectional `## Coordination` sections are plan-body prose edited directly; the authoring rule is to update every existing counterpart in the same change set, and a race resolves as a merge conflict at landing. Ordinary overlap may remain a nonblocking one-sided warning in a plan body.

- A plan that turns out to be a refactor/bugfix in disguise (no new capability) moves to `Documents/Plans/` through the corresponding WorktreeCli queue mutation.
- Designs deferred on YAGNI grounds are scored normally but carry explicit "Revisit When" trigger conditions in the plan body — the `Frame/Future_*.txt` files are the pattern.

## Plan File Authoring

New plans follow the `../Plans/AGENTS.md` authoring conventions (heading shape, required `## Out of scope` section, risk trigger, and interface names rather than bare paths) and use `.md`. Internal numbered design steps remain plan-local. Heading structure is not enforced for `.txt` plans.
