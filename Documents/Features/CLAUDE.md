# Features

Implementation plans for brand-new engine and game additions: new render passes, new effects, new systems, new collections, new network capabilities, new audio systems, new dev tooling that ships in the binary.

Counterpart: `Documents/Plans/` holds refactors and bugfixes (debt reduction). See `Documents/CLAUDE.md` for the full distinction.

## Index File

`Order.md` — all feature plans, sorted by score (lowest first). Score = Effort − Impact + Risks.

## Subdirectories

Organized by area: `Audio/`, `Engine/`, `Frame/`, `Graphics/`, `Misc/`, `Network/`.

## Rules

- Any new feature plan added to this directory **must** be added to `Order.md`.
- When a feature plan is executed, **remove it** from `Order.md` and **delete the plan file** from disk.
- If a plan turns out to be a refactor/bugfix in disguise (no new capability), move it to `Documents/Plans/` and update both `Order.md` files.
