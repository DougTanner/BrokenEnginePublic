---
name: cleanup-worktrees
description: Remove Broken Engine session worktrees created by the Claude Code and Codex CLI wrapper scripts once they are 48+ hours old by folder creation time, regardless of dirty or unlanded state. Use this skill manually each morning before creating new sessions; it leaves younger worktrees untouched and reports saved Codex refs without deleting them.
argument-hint: [preview]
allowed-tools: [Read, Bash, PowerShell]
disable-model-invocation: true
---

# Cleanup Worktrees

Clean retained Claude Code and Codex CLI worktrees whose folders are 48+ hours
old. Manual invocation authorizes removal of every wrapper-root worktree past
that age — including dirty or unlanded ones — with no second
confirmation.

## Run

1. Accept either no argument or `preview`. Stop on any other argument.
2. Start in the primary checkout, never a session worktree. Resolve that root
   with `git rev-parse --show-toplevel`. The script blocks if the checkout's Git
   directory differs from its common Git directory; do not work around it.
3. Run the repository-owned script with no switch for cleanup or `-Preview` for
   preview. In Codex's PowerShell 7 terminal:

   ```powershell
   $RepositoryRoot = (git rev-parse --show-toplevel).Trim()
   $Script = Join-Path $RepositoryRoot '.agents/skills/cleanup-worktrees/scripts/cleanup-worktrees.ps1'
   $CleanupMode = @() # Use @('-Preview') for preview.
   pwsh -NoProfile -ExecutionPolicy Bypass -File $Script @CleanupMode
   ```

   In Claude Code's Git Bash terminal, convert the script path first:

   ```bash
   repository_root="$(git rev-parse --show-toplevel)"
   script="$(cygpath -w "$repository_root/.agents/skills/cleanup-worktrees/scripts/cleanup-worktrees.ps1")"
   cleanup_mode=() # Use cleanup_mode=(-Preview) for preview.
   pwsh -NoProfile -ExecutionPolicy Bypass -File "$script" "${cleanup_mode[@]}"
   ```

Do not recreate failed commands with broader Git or filesystem operations. The
script deliberately refuses cleanup outside the two wrapper roots, saved-ref
deletion, and global pruning. Within those roots it deliberately force-removes
worktrees and force-deletes their `claude/`/`codex/` branches with
`git branch -D`.

A wrapper session still running after 48 hours will have its worktree removed —
close or land long-lived sessions before running cleanup.

## Report

Return the script's complete report, including:

- cleanup status and primary checkout identity
- removed worktrees and branches
- retained worktrees with exact reasons
- reported saved `refs/codex/snapshots/*` refs
- residuals as the final line, with distinct errors and one retained-worktree
  count instead of duplicated retained entries
