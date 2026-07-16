# Plans

Refactor/bugfix plan queue; sibling feature rules live in `../AGENTS.md`. The [canonical execution-gate contract](../../.agents/skills/next-plan/references/execution-gates.md) approves the full plan once; only the exact primary-mutation summary can authorize primary history to change.

## Order.md

Ad hoc planning and plan-file cleanup may use the checkout the user supplied. `plan order` operations normally use a wrapper-created session worktree and live WorktreeCli claim. An explicitly user-authorized `primary-commit` may adopt clean primary under the root finalization contract.

`Order.md` is a live queue, not a changelog. Its executable table contains every current plan, roughly ordered by score (Score = Effort − Impact + Risks), lowest first. Row order is only a loose tiebreaker; approximate placement is sufficient and WorktreeCli does not diagnose score ordering. Do not record landed/dropped plans, session retrospectives, removal annotations, or review-sweep narratives. Put durable context in the individual plan file instead.

WorktreeCli is the only component that parses, validates, or mutates executable rows. The machine-owned table has this exact shape:

```
| Plan | Tier | Effort | Impact | Risks | Score | Depends On | Notes |
| [<area>/<File>.md](<area>/<File>.md) | <Quick Win\|Small\|Medium\|Large\|Architectural> | <1-5> | <1-5> | <0-4> | <computed> | <normalized dependencies or single dash> | <one-line summary of what lands> |
```

`Depends On` is `-` or a semicolon-separated list of normalized repository-relative `Documents/Plans/<area>/<file>.md|.txt` and `Documents/Features/<area>/<file>.md|.txt` identities. A dependency row blocks its dependents while it exists, including while another session has claimed it. Do not express directional prerequisites in prose. Notes may contain ordinary prose, including `|`; WorktreeCli owns the fixed-column parsing and rendering.

Mandatory nondirectional constraints (`never interleave`, joint resolution, alone execution, or protocol/version/CRC/replay/`.pack`/`kiVersion` batching) belong in a standard `## Coordination` section in every affected live plan. Creating or changing such a constraint requires one atomic WorktreeCli add/update request covering every existing counterpart. Ordinary warning-only overlap may remain one-sided in a plan body and does not block selection.

`### Reference / Index Documents` remains the non-executable table for meta/overview documents such as `Graphics/ShaderReview/00_Overview.md`. WorktreeCli validates that those references resolve and do not also appear as executable plans, but they are never scheduled or scored.

### Primary and session authority

The clean primary/root checkout is the live queue used by validation and `claim-next`. Normal queue operations use a wrapper-created session worktree; their mutations become live only when landing advances primary. An explicitly user-authorized primary commit may use registered primary as its worktree through the root `primary-commit` path. `--repo` is the Git common directory, `--primary-worktree` is authoritative live state, and `--worktree` is the registered checkout whose queue, plan, request, and staged content the command may inspect or mutate.

Use `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe` from the current checkout. Linked worktrees consume the provisioned immutable primary Output; routine workflows never build WorktreeCli or write through that link. WorktreeCli owns the existing global queue locks and row claims; never place claim state or owner/session metadata in `Order.md` or a plan file. Takeover still requires explicit user approval.

Plan-producing workflows first finish semantic validation and write the new plan files in the session, then submit a schema-versioned `plan order add` request beneath that worktree's `Temp/`. WorktreeCli computes Score, validates both queues and the complete dependency graph, and adds every requested row or none. A failed add deliberately leaves retryable orphan plan files in the session; they remain invisible to primary selection until landed. Existing-plan prose/row/dependency changes use one atomic `plan order update` request with staged plan bytes and expected hashes.

`plan order claim-next` validates clean primary under both queue locks, requires matching selected-plan bytes, and creates the row claim without edits. `plan order complete` requires that claim and transactionally removes the row, plan, and dependency edges. A session landing retains it for `complete --reapply` and owner-unclaim; a primary commit retains it until finalization validates committed primary against the receipt and unclaims it.

## Subdirectories

Plans live in area subdirectories (`Common/`, `Engine/`, `Frame/`, `Graphics/`, `Save/`, …), never at the `Plans/` root. Add area folders as needed when a new area accrues plans.

## Rules

- Any new plan gets a fully populated WorktreeCli add entry in the same edit session that creates the plan file: `queue`, `plan`, `tier`, `effort`, `impact`, `risks`, `notes`, and optional `dependsOn`. Use the scoring anchors in `../AGENTS.md`; WorktreeCli computes Score. Do not leave scoring for later. If the change is too speculative to score, it does not belong in the queue yet.
- Group new plans into prerequisite-first request sequences. WorktreeCli adds an immediate-predecessor edge within each sequence; put independent plans in separate sequences. Dependencies on existing live plans are explicit `dependsOn` identities.
- Never edit an executable `Order.md` row directly. Use WorktreeCli `add`, `update`, or owned `complete`; the command validates and mutates both queue graphs atomically as required.
- If a plan turns out to be a new capability rather than a refactor, move it to `Documents/Features/` and submit the corresponding WorktreeCli queue mutation.

`Tier` remains an informal size/risk descriptor for readers scanning the table; it does not affect score or ordering.

## Plan File Authoring

Shape: `# Title` → `## Context` → `## Design` → `## Critical files` → `## Notes` — section presence matters, exact ordering doesn't. On top of that:

- **State the risk trigger.** A live plan names any Tier 3, queue, landing, or invariant exposure. Tier 3 uses a short execution card and conditional audit/grilling only for material ambiguity; the final evidence ledger is reserved for queue mutation, reconciliation, or landing. Internal numbered design steps remain local to the plan.

- **`## Out of scope` (required).** Explicit list of adjacent things the plan does *not* address — the single most effective check against gold-plating during execution; without it, scope creep surfaces only when the diff is already big.
- **`## Acceptance criteria` (recommended when "done" is non-obvious).** Concrete, observable conditions. Skip for trivial mechanical refactors where the diff itself is the criterion.
- **Ground the plan.** Every design step and acceptance check follows from the goal or an existing contract. Use repository evidence for facts; leave unresolved material choices to the user.
- **Name interfaces, not just paths.** Prefer "the `LogDifferences` member of `BlastersPostRender`" over a bare `file:line` — the symbol survives renames and line drift, and `/next-plan`'s citation-refresh pass relies on symbol identity. Keep the path/line for jump-to-source convenience; citing line numbers freely is fine since they get refreshed at execution.
- **State invariant exposure.** Say explicitly whether the plan touches determinism/CRC sim paths, `kiVersion`/`.pack` layout, replays, client/server guard scope, or allocation-tracked paths. Pre-stage unresolved architectural decisions only when Tier 3 needs user input before approval.
- Plans whose deliverable is an options writeup rather than code are tagged "Decision plan (present options)" in their `Notes` row.
