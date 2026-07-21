# Plans

Tracked refactor and bugfix plans. Capability additions belong in [`../Features/`](../Features/AGENTS.md); classification and score anchors live in [`../AGENTS.md`](../AGENTS.md).

## Live Queue

Executable rows are machine-local under `%LOCALAPPDATA%\BrokenEngineLocks\plan-queue-state\<sha256(repo-common-dir)>\Plans-Order.md`; feature rows use the sibling `Features-Order.md`. The `--plans-order` and `--features-order` defaults are logical queue identities, not tracked files. WorktreeCli is the only parser and mutator.

The queue contains only current work. Do not add completion history, removal notes, session narratives, or claim metadata. `Reference / Index Documents` is the separate non-executable table for overview documents.

Each row contains plan identity, tier label, effort, impact, risks, computed score, dependencies, and a one-line note. `Depends On` is `-` or semicolon-separated normalized `Documents/Plans/...` / `Documents/Features/...` identities. A live dependency row blocks its dependents, including while claimed. Put directional prerequisites in this column, not prose.

Mandatory nondirectional constraints such as never-interleave, joint resolution, or protocol/version/CRC/replay/pack batching belong in a standard `## Coordination` section in every affected plan. Update all existing counterparts in one tracked change.

## Mutation and Authority

- Plan-body prose is an ordinary tracked edit, including `## Coordination`. It requires no queue request unless row fields or plan identity also change.
- `plan order add` is staged for post-landing publication so a new row cannot reference an unlanded file. Add requests are atomic and include every row field; WorktreeCli computes score and validates both dependency graphs.
- `plan order update` submits immediately and atomically updates the live row plus the plan bytes from `stagedContent`. Its request carries `expectedPlanSha256` and `expectedRowSha256`; keep staged content under the worktree's `Temp/` directory. Use update for row-field changes, not prose-only edits.
- `plan order complete` requires the owning claim and publishes after landing. The session removes the tracked plan file; completion removes its row and dependency edges and tolerates that already-absent file.
- `claim-next` reads the live queue and verifies selected-plan bytes against primary. Takeover of another live owner requires explicit user approval.

Use `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe` from the provisioned primary Output link. The owning workflow supplies repository, primary-worktree, worktree, owner, session, and branch arguments; do not reconstruct landing or claim mechanics manually.

## Plan Files

Plans live in area subdirectories, never directly at `Plans/`. A new plan provides:

- `# Title`, context, design, critical files, and notes;
- required `## Out of scope` boundaries;
- observable acceptance criteria when the diff alone is insufficient;
- the concrete risk trigger and exposed determinism/CRC, protocol, serialization/layout, replay, affinity, threading, trust-boundary, or tracked-allocation invariants;
- stable interface or symbol names, with paths/lines as supporting evidence;
- a fully scored queue request using the anchors in [`../AGENTS.md`](../AGENTS.md).

Plans presenting options instead of implementation are labeled `Decision plan (present options)` in their queue note. Group prerequisite-first additions in one request sequence; keep independent additions in separate sequences.

## See Also

- [Execution gates](../../.agents/skills/next-plan/references/execution-gates.md) - plan approval and primary-mutation authority
- [`../Features/AGENTS.md`](../Features/AGENTS.md) - feature-only differences
- [`../../Tools/WorktreeCli/AGENTS.md`](../../Tools/WorktreeCli/AGENTS.md) - executable, store, claim, and build mechanics
