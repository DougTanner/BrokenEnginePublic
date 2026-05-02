# Plans

Implementation plans for refactors, bugfixes, and debt reduction — things that don't add a new engine capability but improve how the engine is built or how correctly it runs.

Counterpart: `Documents/Features/` holds brand-new additions (new render passes, new effects, new systems). See `Documents/CLAUDE.md` for the full distinction.

## Index File

`Order.md` — all refactor/bugfix plans, sorted by score (lowest first). Score = Effort − Impact + Risks.

## Subdirectories

Organized by area: `DataPacker/`, `Engine/`, `Frame/`, `Graphics/` (includes `ShaderReview/` for per-shader defensive fixes), `Misc/`, `Network/`.

## Rules

- Any new refactor/bugfix plan added to this directory **must** be added to `Order.md`.
- When a plan is executed, **remove it** from `Order.md` and **delete the plan file** from disk.
- If a plan turns out to be a new capability rather than a refactor, move it to `Documents/Features/` and update both `Order.md` files.

## Plan File Authoring

Existing plans take the shape `# Title` → `## Context` → `## Design` → `## Critical files` → `## Notes`. Keep that shape. Existing plans cite file paths and line numbers freely — that is fine; `/next-plan` step 5 refreshes them at execution time.

When authoring a new plan, add the following on top of that shape:

- **`## Out of scope` section (required).** Explicit list of adjacent things this plan does *not* address. Forces scope boundaries up front and is the single most effective check against gold-plating during execution. Without this section, scope creep tends to surface only when the diff is already big.
- **`## Acceptance criteria` section (recommended when "done" is non-obvious).** Bullet list of concrete, observable conditions. Skip for trivial mechanical refactors where the diff itself is the criterion.
- **Name interfaces, not just paths.** When citing the change site, prefer "the `LogDifferences` member of `BlastersUpdate`" over "`Blasters.h:142`" alone. Both are useful, but the symbol name survives a file rename or line-number drift; `/next-plan`'s refresh pass also relies on symbol identity to relocate citations. Keep the path/line for jump-to-source convenience; do not let it be the *only* anchor.
