# Plans

Refactor/bugfix plan queue — debt reduction that doesn't add a new engine capability. Counterpart: `Documents/Features/`; the deciding test and the scoring anchors live in [`../AGENTS.md`](../AGENTS.md). `/next-plan` executes the top-priority (lowest-score) plan and refreshes its file/line citations against current source.

## Order.md

All plan work requires a wrapper-created worktree with its live AgentCli session claim. Wrapper admission and exclusive primary AgentCli maintenance use one per-repository ledger, wait up to 660 seconds, and must not be replaced by an independent lock domain.

`Order.md` is a live queue, not a changelog. Its executable table contains every current plan, roughly ordered by score (Score = Effort − Impact + Risks), lowest first. Row order is only a loose tiebreaker; approximate placement is sufficient and AgentCli does not diagnose score ordering. Do not record landed/dropped plans, session retrospectives, removal annotations, or review-sweep narratives. Put durable context in the individual plan file instead.

AgentCli is the only component that parses, validates, or mutates executable rows. The machine-owned table has this exact shape:

```
| Plan | Tier | Effort | Impact | Risks | Score | Depends On | Notes |
| [<area>/<File>.md](<area>/<File>.md) | <Quick Win\|Small\|Medium\|Large\|Architectural> | <1-5> | <1-5> | <0-4> | <computed> | <normalized dependencies or single dash> | <one-line summary of what lands> |
```

`Depends On` is `-` or a semicolon-separated list of normalized repository-relative `Documents/Plans/<area>/<file>.md|.txt` and `Documents/Features/<area>/<file>.md|.txt` identities. A dependency row blocks its dependents while it exists, including while another session has claimed it. Do not express directional prerequisites in prose. Notes may contain ordinary prose, including `|`; AgentCli owns the fixed-column parsing and rendering.

Mandatory nondirectional constraints (`never interleave`, joint resolution, alone execution, or protocol/version/CRC/replay/`.pack`/`kiVersion` batching) belong in a standard `## Coordination` section in every affected live plan. Creating or changing such a constraint requires one atomic AgentCli add/update request covering every existing counterpart. Ordinary warning-only overlap may remain one-sided in a plan body and does not block selection.

`### Reference / Index Documents` remains the non-executable table for meta/overview documents such as `Graphics/ShaderReview/00_Overview.md`. AgentCli validates that those references resolve and do not also appear as executable plans, but they are never scheduled or scored.

### Primary and session authority

The clean primary/root checkout is the live queue used by validation for selection and by `claim-next`. A wrapper-created session worktree contains proposed plan files and add/update/complete mutations; those changes do not become live to other sessions until verified landing advances primary. `--repo` is the canonical Git common directory, `--primary-worktree` is the wrapper-provided authoritative primary checkout when a command reads live state, and `--worktree` is the registered checkout whose contained queue, plan, request, and staged-content paths the command may inspect or mutate.

Use `Tools\AgentCli\Platforms\VisualStudio2026\Output\AgentCli.exe` from the current checkout. Linked worktrees consume the provisioned immutable primary Output; routine workflows never build AgentCli or write through that link. AgentCli owns the existing global queue locks and row claims; never place claim state or owner/session metadata in `Order.md` or a plan file. Takeover still requires explicit user approval.

Plan-producing workflows first finish semantic validation and write the new plan files in the session, then submit a schema-versioned `plan order add` request beneath that worktree's `Temp/`. AgentCli computes Score, validates both queues and the complete dependency graph, and adds every requested row or none. A failed add deliberately leaves retryable orphan plan files in the session; they remain invisible to primary selection until landed. Existing-plan prose/row/dependency changes use one atomic `plan order update` request with staged plan bytes and expected hashes.

`plan order claim-next` validates and reads clean primary under both queue locks, requires the session and selected plan bytes to match primary, and creates the global row claim without editing repository files. `plan order complete` requires that owned claim and removes the session row, plan file, and dependency edges transactionally; finalization retains the claim until the cleanup lands and owns reconciliation-time `complete --reapply` plus the final owner-only unclaim.

## Subdirectories

Plans live in area subdirectories (`Common/`, `Engine/`, `Frame/`, `Graphics/`, `Save/`, …), never at the `Plans/` root. Add area folders as needed when a new area accrues plans.

## Rules

- Any new plan gets a fully populated AgentCli add entry in the same edit session that creates the plan file: `queue`, `plan`, `tier`, `effort`, `impact`, `risks`, `notes`, and optional `dependsOn`. Use the scoring anchors in [`../AGENTS.md`](../AGENTS.md); AgentCli computes Score. Do not leave scoring for later. If the change is too speculative to score, it does not belong in the queue yet.
- Group new plans into prerequisite-first request sequences. AgentCli adds an immediate-predecessor edge within each sequence; put independent plans in separate sequences. Dependencies on existing live plans are explicit `dependsOn` identities.
- Never edit an executable `Order.md` row directly. Use AgentCli `add`, `update`, or owned `complete`; the command validates and mutates both queue graphs atomically as required.
- If a plan turns out to be a new capability rather than a refactor, move it to `Documents/Features/` and submit the corresponding AgentCli queue mutation.

`Tier` remains an informal size/risk descriptor for readers scanning the table; it does not affect score or ordering.

## Plan File Authoring

Shape: `# Title` → `## Context` → `## Design` → `## Critical files` → `## Notes` — section presence matters, exact ordering doesn't. On top of that:

- **Use named process stages.** When a live plan needs to constrain repository workflow, refer to the seven named C++ Code Change Process stages from the root `AGENTS.md`, not a root stage number or a frozen list of mandatory roles. Internal numbered design steps remain local to the plan and do not need renaming.

- **`## Out of scope` (required).** Explicit list of adjacent things the plan does *not* address — the single most effective check against gold-plating during execution; without it, scope creep surfaces only when the diff is already big.
- **`## Acceptance criteria` (recommended when "done" is non-obvious).** Concrete, observable conditions. Skip for trivial mechanical refactors where the diff itself is the criterion.
- **Name interfaces, not just paths.** Prefer "the `LogDifferences` member of `BlastersPostRender`" over a bare `file:line` — the symbol survives renames and line drift, and `/next-plan`'s citation-refresh pass relies on symbol identity. Keep the path/line for jump-to-source convenience; citing line numbers freely is fine since they get refreshed at execution.
- **State invariant exposure.** Say explicitly whether the plan touches determinism/CRC sim paths, `kiVersion`/`.pack` layout, replays, client/server guard scope, or allocation-tracked paths — and pre-stage any single open decision for `/external-grill-plan` in `## Notes`.
- Plans whose deliverable is an options writeup rather than code are tagged "Decision plan (present options)" in their `Notes` row.
