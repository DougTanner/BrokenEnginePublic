# Features

Plans for work that adds a capability the engine does not already have. The classification test and canonical scoring anchors live in [`../AGENTS.md`](../AGENTS.md); shared queue lifecycle and plan-authoring rules live in [`../Plans/AGENTS.md`](../Plans/AGENTS.md).

## Feature Queue

The live feature queue is machine-local state under `%LOCALAPPDATA%\BrokenEngineLocks\plan-queue-state\<sha256(repo-common-dir)>\Features-Order.md`. WorktreeCli alone parses, validates, and mutates it. Feature plans remain tracked files in area subdirectories such as `Audio/`, `Engine/`, `Frame/`, `Graphics/`, and `Network/`.

Executable rows use the same schema, dependency identities, mutation timing, and authority rules as Plans. The `Reference / Index Documents` table is unscored and non-executable; WorktreeCli validates its paths and excludes those documents from scheduling.

## Feature-Specific Rules

- A plan that does not add a capability belongs in `Documents/Plans/`; move its file and queue identity together through the corresponding WorktreeCli mutation.
- Directional prerequisites may cross queues using normalized `Documents/Plans/...` or `Documents/Features/...` identities.
- Designs deferred on YAGNI grounds carry explicit `Revisit When` triggers in the plan body; `Frame/Future_*.txt` documents are the pattern.
- New feature plans use `.md` and follow the Plans heading, scope, invariant, and acceptance guidance. Existing `.txt` plans are not required to adopt Markdown heading structure.

## See Also

- [`../Plans/AGENTS.md`](../Plans/AGENTS.md) - queue operations and plan authoring
- [`../AGENTS.md`](../AGENTS.md) - feature classification and scoring anchors
