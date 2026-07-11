# Features

Implementation plans for brand-new additions — work that gives the engine a capability it didn't have. Counterpart queue: [`../Plans/AGENTS.md`](../Plans/AGENTS.md) (refactors/bugfixes). The deciding test and the canonical scoring anchors are in [`../AGENTS.md`](../AGENTS.md).

## Index File

`Order.md` — all feature plans, sorted by score (lowest first). Plans live in area subdirectories: `Audio/`, `Engine/`, `Frame/`, `Graphics/`, `Network/`. Besides the scored table, `Order.md` has three sections plan authors must keep current:

- **Reference / Index Documents** — unscored reference/index docs; exempt from the scoring requirement and never independently scheduled.
- **Dependencies** — explicit prerequisites and mandatory invariant landing constraints, including cross-queue dependencies on `Documents/Plans/`. Unfinished prerequisites block execution; nondirectional constraints do not block selection but remain mandatory at landing.
- **File Groups** — shared-file overlap warnings. Record the intersecting files and expected landing order; overlap alone does not block or require one session.

## Rules

Queue lifecycle and the `Order.md` row format are identical to [`../Plans/AGENTS.md`](../Plans/AGENTS.md): a new plan gets its fully scored row in the same edit session, inserted score-sorted; an executed plan has its row removed and its file deleted. Features-specific:

The AgentCli v2 plan-domain lock defined by [`../Plans/AGENTS.md`](../Plans/AGENTS.md) is authoritative across worktrees; the local `[CLAIMED <date>]` marker is informational. Feature keys are normalized paths relative to this directory's `Order.md`. Duplicate claims block. Different plans with ordinary file overlap may proceed independently; under root C++ Code Change Process step 12, the later lander reconciles the newer target-branch commit and reruns every affected review, build, and verification step before landing. Mandatory invariant constraints remain binding regardless of landing order.

- A plan that turns out to be a refactor/bugfix in disguise (no new capability) moves to `Documents/Plans/`, updating both `Order.md` files.
- Designs deferred on YAGNI grounds are scored normally but carry explicit "Revisit When" trigger conditions in the plan body — the `Frame/Future_*.txt` files are the pattern.

## Plan File Authoring

New plans follow the [`../Plans/AGENTS.md`](../Plans/AGENTS.md) authoring conventions (heading shape, required `## Out of scope` section, name interfaces not just paths) and use `.md`. Existing `.txt` plans are grandfathered with varied legacy headings.
