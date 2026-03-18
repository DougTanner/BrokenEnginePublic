---
name: save
description: Saves the current plan to Documents/Plans/ as a text file. Use when the user explicitly asks to save a plan (e.g., "/save", "save this plan", "save the plan to disk"). Only invoke when the user explicitly requests it — never trigger automatically when exiting plan mode.
allowed-tools: [Read, Write, Bash, Glob]
user-invocable: true
---

# Save Plan

Saves the current plan file from `.claude/plans/` to `Documents/Plans/` in the repo.

## Instructions

1. **Find the plan file**: Look for the plan file path mentioned in the conversation's plan mode context (in the `.claude/plans/` directory). Read it.

2. **Determine filename**: Derive a filename from the plan's `# Title` heading. Convert to PascalCase, remove spaces and special characters, and use `.txt` extension. For example:
   - `# Fix Partial Full State: Deferred Injection` → `DeferredFullStateInjection.txt`
   - `# Add Player Respawn Logic` → `AddPlayerRespawnLogic.txt`

3. **Check for arguments**: If the user provided a filename as an argument (e.g., `/save MyPlan`), use that as the filename (append `.txt` if not present).

4. **Save the file**: Write the plan content to `Documents/Plans/<filename>` (relative to the repo root).

5. **Report**: Confirm the file was saved with its path.
