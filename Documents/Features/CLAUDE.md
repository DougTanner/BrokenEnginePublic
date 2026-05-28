# Features

Implementation plans for brand-new engine and game additions: new render passes, new effects, new systems, new collections, new network capabilities, new audio systems, new dev tooling that ships in the binary.

Counterpart: `Documents/Plans/` holds refactors and bugfixes (debt reduction). See `Documents/CLAUDE.md` for the full distinction.

## Index File

`Order.md` — all feature plans, sorted by score (lowest first). Score = Effort − Impact + Risks.

## Subdirectories

Organized by area: `Audio/`, `Engine/`, `Frame/`, `Graphics/`, `Misc/`, `Network/`.

## Rules

- Any new feature plan added to this directory **must** be added to `Order.md` **in the same edit session that creates the plan file**, with the full table row populated: `Tier`, `Effort`, `Impact`, `Risks`, computed `Score`, and `Notes`. Use the scoring anchors in [`../CLAUDE.md`](../CLAUDE.md). Do not leave scoring "for the user to fill in later" — that abdication has caused plans to land outside the priority queue. If the change is genuinely too speculative to score, the plan does not belong here yet.
- Insert the new row at its score-correct sorted position (lowest score first), and renumber the leftmost `#` column for all rows below the insertion point so the column remains a contiguous 1-based ordinal. If the plan touches files already listed in the `## File Groups` or `## Dependencies` sections of `Order.md`, add it to the relevant entry there too.
- When a feature plan is executed, **remove it** from `Order.md` and **delete the plan file** from disk. Renumber the `#` column to close the gap.
- If a plan turns out to be a refactor/bugfix in disguise (no new capability), move it to `Documents/Plans/` and update both `Order.md` files.
- **Reference / index docs** (e.g., the ocean shader overview and tweaks-screen specs) are not independently scheduled — they live in `Order.md`'s "Reference / Index Documents" section, unscored, and are exempt from the scoring requirement above.

### Required `Order.md` row format

```
| # | Plan | Tier | Effort | Impact | Risks | Score | Notes |
| <ord> | [<area>/<File>](<area>/<File>) | <Quick Win\|Small\|Medium\|Large\|Architectural> | <1-5> | <1-5> | <0-4> | <Effort − Impact + Risks> | <one-line summary of what lands> |
```

`Tier` is an informal size/risk descriptor and does not affect score or ordering — it exists for readers scanning the table. Use the scoring anchors from [`../CLAUDE.md`](../CLAUDE.md). Scores are not comparable across the two `Order.md` files.

## Plan File Authoring

Same shape as `Documents/Plans/`: `# Title` → `## Context` → `## Design` → `## Critical files` → `## Notes`, plus a required `## Out of scope` section and a recommended `## Acceptance criteria` section. Name interfaces, not just paths, when citing change sites. See [`../Plans/CLAUDE.md`](../Plans/CLAUDE.md) for the full authoring conventions. Existing plans use either `.txt` or `.md`; new plans should use `.md`.
