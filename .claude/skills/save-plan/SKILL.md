---
name: save-plan
description: Saves the current plan-mode plan from the user's home `.claude/plans/` directory into the repo's planning tree — `Documents/Plans/` for refactors/bugfixes or `Documents/Features/` for new capabilities — as a PascalCase `.md` file in the correct area subdirectory, then adds the required scored row to that tree's `Order.md`. Optional argument overrides the filename (e.g., `/save-plan MyPlan`).
argument-hint: [filename]
allowed-tools: [Read, Write, Edit, Bash, Glob]
disable-model-invocation: true
---

# Save Plan

Saves the current plan-mode plan from the user's home `.claude/plans/` directory (`C:/Users/<user>/.claude/plans/` on Windows, `~/.claude/plans/` on Unix) into the repo's planning tree.

## Instructions

1. **Find the plan file**: Use the plan file path from the conversation's plan-mode context — the harness provides the absolute path (e.g., `C:/Users/<user>/.claude/plans/1-foo.md`); never guess a relative `.claude/plans/` path. Read it. If no plan path appears in the conversation, list the home `.claude/plans/` directory newest-first and ask the user which file to save; if the directory is missing or empty, report that there is no plan to save and stop.

2. **Pick the destination**: Refactor/bugfix plans go in `Documents/Plans/`; brand-new capabilities go in `Documents/Features/` (deciding test in `Documents/CLAUDE.md`: does the plan add a capability the engine didn't have?). If it reads as a feature, confirm the tree with the user before saving. Plans live in an area subdirectory (`Engine/`, `Frame/`, `Graphics/`, `Network/`, ...), never at the tree root — pick the area matching the plan's subject, creating the folder only if no existing area fits.

3. **Determine filename**: If the user passed a filename argument (e.g., `/save-plan MyPlan`), use it, appending `.md` if absent. Otherwise derive a concise PascalCase name from the plan's `#` title — drop filler words, remove spaces and special characters, use `.md`:
   - `# Fix Partial Full State: Deferred Injection` → `DeferredFullStateInjection.md`
   - `# Add Player Respawn Logic` → `AddPlayerRespawnLogic.md`

   If the plan has no `#` title heading, derive the name from the plan's content — plan-mode file stems end in random slug words (e.g., `...-generic-pearl.md`), so do not reuse the stem verbatim.

4. **Handle collisions**: If the target file already exists, ask the user whether to overwrite or pick a new name. Do not silently overwrite.

5. **Save and index**: Write the plan content to `<tree>/<area>/<filename>`. Then read the destination tree's CLAUDE.md (`Documents/Plans/CLAUDE.md` or `Documents/Features/CLAUDE.md`) and follow its rules: add the fully scored row (`Tier`/`Effort`/`Impact`/`Risks`/`Score`/`Notes`) to that tree's `Order.md` at its sorted position now — both trees forbid leaving a plan file unindexed or unscored.

6. **Report**: Confirm the saved repo-relative path and the `Order.md` row added.
