# Plans

Implementation plans for refactors, bugfixes, and debt reduction — things that don't add a new engine capability but improve how the engine is built or how correctly it runs.

Counterpart: `Documents/Features/` holds brand-new additions (new render passes, new effects, new systems). See `Documents/CLAUDE.md` for the full distinction.

## Index File

`Order.md` — all refactor/bugfix plans, sorted by score (lowest first). Score = Effort − Impact + Risks.

## Subdirectories

Organized by area: `DataPacker/`, `Engine/`, `Frame/`, `Graphics/` (includes `ShaderReview/` for per-shader defensive fixes), `Misc/`, `Network/`.

## Rules

- Any new refactor/bugfix plan added to this directory **must** be added to `Order.md` **in the same edit session that creates the plan file**, with the full table row populated: `Tier`, `Effort`, `Impact`, `Risks`, computed `Score`, and `Notes`. Use the scoring anchors in [`../CLAUDE.md`](../CLAUDE.md). Do not leave scoring "for the user to fill in later" — that abdication has caused plans to land outside the priority queue. If the change is genuinely too speculative to score, the plan does not belong here yet.
- Insert the new row at its score-correct sorted position (lowest score first), and renumber the leftmost `#` column for all rows below the insertion point so the column remains a contiguous 1-based ordinal. If the plan touches files already listed in the `## File Groups` or `## Dependencies` sections, add it to the relevant entry there too.
- When a plan is executed, **remove it** from `Order.md` and **delete the plan file** from disk. Renumber the `#` column to close the gap.
- If a plan turns out to be a new capability rather than a refactor, move it to `Documents/Features/` and update both `Order.md` files.

### Required `Order.md` row format

```
| # | Plan | Tier | Effort | Impact | Risks | Score | Notes |
| <ord> | [<area>/<File>.md](<area>/<File>.md) | <Quick Win\|Small\|Medium\|Large\|Architectural> | <1-5> | <1-5> | <0-4> | <Effort − Impact + Risks> | <one-line summary of what lands> |
```

`Tier` is an informal size/risk descriptor and does not affect score or ordering — it exists for readers scanning the table.

## Plan File Authoring

Existing plans take the shape `# Title` → `## Context` → `## Design` → `## Critical files` → `## Notes`. Keep that shape. Existing plans cite file paths and line numbers freely — that is fine; `/next-plan` step 5 refreshes them at execution time.

When authoring a new plan, add the following on top of that shape:

- **`## Out of scope` section (required).** Explicit list of adjacent things this plan does *not* address. Forces scope boundaries up front and is the single most effective check against gold-plating during execution. Without this section, scope creep tends to surface only when the diff is already big.
- **`## Acceptance criteria` section (recommended when "done" is non-obvious).** Bullet list of concrete, observable conditions. Skip for trivial mechanical refactors where the diff itself is the criterion.
- **Name interfaces, not just paths.** When citing the change site, prefer "the `LogDifferences` member of `BlastersUpdate`" over "`Blasters.h:142`" alone. Both are useful, but the symbol name survives a file rename or line-number drift; `/next-plan`'s refresh pass also relies on symbol identity to relocate citations. Keep the path/line for jump-to-source convenience; do not let it be the *only* anchor.
