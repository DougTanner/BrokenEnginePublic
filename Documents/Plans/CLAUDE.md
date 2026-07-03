# Plans

Refactor/bugfix plan queue — debt reduction that doesn't add a new engine capability. Counterpart: `Documents/Features/`; the deciding test and the scoring anchors live in [`../CLAUDE.md`](../CLAUDE.md). `/next-plan` executes the top-priority (lowest-score) plan and refreshes its file/line citations against current source.

## Order.md

Priority index — single table of all live plans sorted by score, lowest first (Score = Effort − Impact + Risks). It reflects only the CURRENT set of live plans; **it is not a changelog.** Do not record landed/dropped plans, "this run / this session" retrospectives, "has landed and been removed" annotations, or review-sweep narratives here — every row and reference must correspond to a plan file that exists on disk right now. It also carries:

- `### Reference / Index Documents` — second table for meta/overview docs that are never executed as plans (e.g. `Graphics/ShaderReview/00_Overview.md`); excluded from the priority walk and orphan scans.
- `## Dependencies` — ordering constraints between live plans (shared files, decision prerequisites).
- `## File Groups` — live plans touching the same files, to co-schedule in one session so line citations don't go stale between them.

Debt-score / review-sweep retrospectives do **not** belong in `Order.md` — capture that context (if wanted) in the individual plan files' `## Context`, not in the priority index.

A Notes cell prefixed `[CLAIMED]` marks a plan a session is actively executing — `/next-plan` claims the row on selection and removes row + file only on completion (rejection unclaims). Other sessions skip claimed rows; only the user may unclaim a stale one.

## Subdirectories

Plans live in area subdirectories (`Common/`, `Engine/`, `Frame/`, `Graphics/`, `Save/`, …), never at the `Plans/` root. Add area folders as needed when a new area accrues plans.

## Rules

- Any new plan gets its scored `Order.md` row in the same edit session that creates the plan file, with the full row populated: `Tier`, `Effort`, `Impact`, `Risks`, computed `Score`, `Notes`. Use the scoring anchors in [`../CLAUDE.md`](../CLAUDE.md). Do not leave scoring "for the user to fill in later" — that abdication has caused plans to land outside the priority queue. If the change is genuinely too speculative to score, the plan does not belong here yet.
- Insert the new row at its score-correct sorted position. If the plan touches files already listed in `## File Groups` or `## Dependencies`, add it to the relevant entry there too.
- When a plan is executed (or dropped): remove its row from `Order.md` and delete the plan file from disk. Also **prune every reference to it** from `## Dependencies` and `## File Groups` — delete the plan from any entry that names it, and delete entries that no longer reference two or more live plans. Do **not** leave "landed and been removed" / "dropped as invalid" annotations behind; there is no historical record here (`git log` and the git history of deleted plan files are the archive). Any surviving context worth keeping (e.g. an API a follow-up plan builds on) is stated as present-tense current state ("reuses the existing `X` API"), never as "added this session by the now-landed `Y`".
- If a plan turns out to be a new capability rather than a refactor, move it to `Documents/Features/` and update both `Order.md` files.

### Required `Order.md` row format

```
| Plan | Tier | Effort | Impact | Risks | Score | Notes |
| [<area>/<File>.md](<area>/<File>.md) | <Quick Win\|Small\|Medium\|Large\|Architectural> | <1-5> | <1-5> | <0-4> | <Effort − Impact + Risks> | <one-line summary of what lands> |
```

`Tier` is an informal size/risk descriptor for readers scanning the table; it does not affect score or ordering.

## Plan File Authoring

Shape: `# Title` → `## Context` → `## Design` → `## Critical files` → `## Notes` — section presence matters, exact ordering doesn't. On top of that:

- **`## Out of scope` (required).** Explicit list of adjacent things the plan does *not* address — the single most effective check against gold-plating during execution; without it, scope creep surfaces only when the diff is already big.
- **`## Acceptance criteria` (recommended when "done" is non-obvious).** Concrete, observable conditions. Skip for trivial mechanical refactors where the diff itself is the criterion.
- **Name interfaces, not just paths.** Prefer "the `LogDifferences` member of `BlastersPostRender`" over a bare `file:line` — the symbol survives renames and line drift, and `/next-plan`'s citation-refresh pass relies on symbol identity. Keep the path/line for jump-to-source convenience; citing line numbers freely is fine since they get refreshed at execution.
- **State invariant exposure.** Say explicitly whether the plan touches determinism/CRC sim paths, `kiVersion`/`.pack` layout, replays, client/server guard scope, or allocation-tracked paths — and pre-stage any single open decision for `/external-grill-plan` in `## Notes`.
- Plans whose deliverable is an options writeup rather than code are tagged "Decision plan (present options)" in their `Notes` row.
