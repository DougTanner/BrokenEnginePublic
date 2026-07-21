---
name: cleanup-worktrees
description: Remove prior-day Broken Engine session worktrees created by the Claude Code and Codex CLI wrapper scripts. Use this skill manually each morning before creating new sessions; it deletes only clean, unlocked, fully landed wrapper-root worktrees, leaves current-day or unsafe worktrees untouched, and reports Codex snapshot refs without deleting them.
argument-hint: [preview]
allowed-tools: [Read, Bash, PowerShell]
disable-model-invocation: true
---

# Cleanup Worktrees

Clean retained Claude Code and Codex CLI worktrees from earlier local calendar
days. Manual invocation authorizes removal of every candidate that passes the
script's safety checks; no second confirmation is required.

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
script deliberately refuses force removal, branch force-deletion, stale-lock
recovery, global pruning, snapshot-ref deletion, and cleanup outside the two
wrapper roots.

A retained worktree reported as `locked: Live claude/codex session ...` belongs
to a running wrapper session — its lock is placed at session creation and
released when the wrapper exits. Never unlock or remove it while that session
may be alive; if a crash orphaned the lock, only the user confirms the session
is dead and runs `git worktree unlock` before the next cleanup pass.

## Report

Return the script's complete report, including:

- cleanup status and primary checkout identity
- removed worktrees and branches
- retained worktrees with exact reasons
- reported `refs/codex/snapshots/*` refs
- residuals as the final line, with distinct errors and one retained-worktree
  count instead of duplicated retained entries
