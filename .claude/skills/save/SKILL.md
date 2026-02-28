---
name: save
description: Saves the current plan to Documents/Plans/ as a text file. Use when exiting plan mode to preserve the plan in the repo.
allowed-tools: [Read, Write, Bash, Glob]
user-invocable: true
---

# Save Plan

Saves the current plan file from `.claude/plans/` to `Documents/Plans/` in the repo.

## Instructions

1. **Find the plan file**: Look for the plan file path mentioned in the conversation's plan mode context (e.g., `C:\Users\dougt\.claude\plans\<name>.md`). Read it.

2. **Determine filename**: Derive a filename from the plan's `# Title` heading. Convert to PascalCase, remove spaces and special characters, and use `.txt` extension. For example:
   - `# Fix Partial Full State: Deferred Injection` → `DeferredFullStateInjection.txt`
   - `# Add Player Respawn Logic` → `AddPlayerRespawnLogic.txt`

3. **Check for arguments**: If the user provided a filename as an argument (e.g., `/save MyPlan`), use that as the filename (append `.txt` if not present).

4. **Save the file**: Write the plan content to `C:/Users/dougt/Documents/BrokenEnginePublic/Documents/Plans/<filename>`.

5. **Report**: Confirm the file was saved with its path.
