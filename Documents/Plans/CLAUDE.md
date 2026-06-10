# Plans

Refactor/bugfix plan queue — debt reduction that doesn't add a new engine capability. Counterpart: `Documents/Features/`; the deciding test and the scoring anchors live in [`../CLAUDE.md`](../CLAUDE.md). `/next-plan` executes the top-scored plan and refreshes its file/line citations against current source.

## Order.md

Priority index — single table of all live plans sorted by score, lowest first (Score = Effort − Impact + Risks). It also carries:

- `### Reference / Index Documents` — second table for meta/overview docs that are never executed as plans (e.g. `Graphics/ShaderReview/00_Overview.md`); excluded from the priority walk and orphan scans.
- `## Dependencies` — ordering constraints between plans (shared files, decision prerequisites).
- `## File Groups` — plans touching the same files, to co-schedule in one session so line citations don't go stale between them.
- Per-area `## Debt Score` sections — review-sweep retrospectives; historical context for why the remaining plans exist.

## Subdirectories

Plans live in area subdirectories (`Common/`, `Engine/`, `Frame/`, `Graphics/`, `Save/`, …), never at the `Plans/` root. Add area folders as needed when a new area accrues plans.

## Rules

- Any new plan gets its scored `Order.md` row in the same edit session that creates the plan file, with the full row populated: `Tier`, `Effort`, `Impact`, `Risks`, computed `Score`, `Notes`. Use the scoring anchors in [`../CLAUDE.md`](../CLAUDE.md). Do not leave scoring "for the user to fill in later" — that abdication has caused plans to land outside the priority queue. If the change is genuinely too speculative to score, the plan does not belong here yet.
- Insert the new row at its score-correct sorted position and renumber the leftmost `#` column so it remains a contiguous 1-based ordinal. If the plan touches files already listed in `## File Groups` or `## Dependencies`, add it to the relevant entry there too.
- When a plan is executed: remove its row from `Order.md`, delete the plan file from disk, and renumber `#`. In `## Dependencies`/`## File Groups`, annotate the affected entry as landed-and-removed rather than deleting it — those annotations are the historical record (there is no archive folder).
- If a plan turns out to be a new capability rather than a refactor, move it to `Documents/Features/` and update both `Order.md` files.

### Required `Order.md` row format

```
| # | Plan | Tier | Effort | Impact | Risks | Score | Notes |
| <ord> | [<area>/<File>.md](<area>/<File>.md) | <Quick Win\|Small\|Medium\|Large\|Architectural> | <1-5> | <1-5> | <0-4> | <Effort − Impact + Risks> | <one-line summary of what lands> |
```

`Tier` is an informal size/risk descriptor for readers scanning the table; it does not affect score or ordering.

## Plan File Authoring

Shape: `# Title` → `## Context` → `## Design` → `## Critical files` → `## Notes` — section presence matters, exact ordering doesn't. On top of that:

- **`## Out of scope` (required).** Explicit list of adjacent things the plan does *not* address — the single most effective check against gold-plating during execution; without it, scope creep surfaces only when the diff is already big.
- **`## Acceptance criteria` (recommended when "done" is non-obvious).** Concrete, observable conditions. Skip for trivial mechanical refactors where the diff itself is the criterion.
- **Name interfaces, not just paths.** Prefer "the `LogDifferences` member of `BlastersUpdate`" over a bare `file:line` — the symbol survives renames and line drift, and `/next-plan`'s citation-refresh pass relies on symbol identity. Keep the path/line for jump-to-source convenience; citing line numbers freely is fine since they get refreshed at execution.
- **State invariant exposure.** Say explicitly whether the plan touches determinism/CRC sim paths, `kiVersion`/`.pack` layout, replays, client/server guard scope, or allocation-tracked paths — and pre-stage any single open decision for `/external-grill-plan` in `## Notes`.
- Plans whose deliverable is an options writeup rather than code are tagged "Decision plan (present options)" in their `Notes` row.
