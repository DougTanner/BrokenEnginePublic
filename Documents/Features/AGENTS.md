# Features

Implementation plans for brand-new additions — work that gives the engine a capability it didn't have. Counterpart queue: [`../Plans/AGENTS.md`](../Plans/AGENTS.md) (refactors/bugfixes). The deciding test and the canonical scoring anchors are in [`../AGENTS.md`](../AGENTS.md).

## Index File

`Order.md` — all feature plans, sorted by score (lowest first). Plans live in area subdirectories: `Audio/`, `Engine/`, `Frame/`, `Graphics/`, `Network/`. Besides the scored table, `Order.md` has three sections plan authors must keep current:

- **Reference / Index Documents** — unscored reference/index docs; exempt from the scoring requirement and never independently scheduled.
- **Dependencies** — intra-queue ordering, plus cross-directory dependencies on `Documents/Plans/` plans touching the same files.
- **File Groups** — plans sharing files; execute together in one session.

## Rules

Queue lifecycle and the `Order.md` row format are identical to [`../Plans/AGENTS.md`](../Plans/AGENTS.md): a new plan gets its fully scored row in the same edit session, inserted score-sorted; an executed plan has its row removed and its file deleted. Features-specific:

- A plan that turns out to be a refactor/bugfix in disguise (no new capability) moves to `Documents/Plans/`, updating both `Order.md` files.
- Designs deferred on YAGNI grounds are scored normally but carry explicit "Revisit When" trigger conditions in the plan body — the `Frame/Future_*.txt` files are the pattern.

## Plan File Authoring

New plans follow the [`../Plans/AGENTS.md`](../Plans/AGENTS.md) authoring conventions (heading shape, required `## Out of scope` section, name interfaces not just paths) and use `.md`. Existing `.txt` plans are grandfathered with varied legacy headings.
