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
