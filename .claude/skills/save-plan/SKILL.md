---
name: save-plan
description: Saves the current plan from the user's home `.claude/plans/` directory to the repo's `Documents/Plans/` directory as a text file. Use when the user explicitly asks to save a plan (e.g., "/save-plan", "save this plan", "save the plan to disk").
allowed-tools: [Read, Write, Bash, Glob]
disable-model-invocation: true
---

# Save Plan

Saves the current plan file from the user's home `.claude/plans/` directory (e.g., `C:\Users\<user>\.claude\plans\` on Windows, `~/.claude/plans/` on Unix) to `Documents/Plans/` inside the repo.

## Instructions

1. **Find the plan file**: Look for the plan file path mentioned in the conversation's plan mode context. The absolute path is provided by the harness (e.g., `C:\Users\dougt\.claude\plans\1-foo.md`) — always use that absolute path, not a relative `.claude/plans/` guess. Read it.

2. **Determine filename**: Derive a filename from the plan's `# Title` heading. Convert to PascalCase, remove spaces and special characters, and use `.txt` extension. For example:
   - `# Fix Partial Full State: Deferred Injection` → `DeferredFullStateInjection.txt`
   - `# Add Player Respawn Logic` → `AddPlayerRespawnLogic.txt`

   If the plan has no `# Title` heading, fall back to the plan file's stem (e.g., `1-foo.md` → `Foo.txt` with leading digits stripped).

3. **Check for arguments**: If the user provided a filename as an argument (e.g., `/save-plan MyPlan`), use that as the filename (append `.txt` if not present).

4. **Create destination if missing**: If `Documents/Plans/` does not exist in the repo, create it.

5. **Handle collisions**: If the target file already exists, ask the user whether to overwrite or pick a new name. Do not silently overwrite.

6. **Save the file**: Write the plan content to `Documents/Plans/<filename>` (relative to the repo root).

7. **Report**: Confirm the file was saved with its repo-relative path.
