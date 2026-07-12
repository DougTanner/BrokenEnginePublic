---
name: finalize-changes
description: >-
  Finalize verified repository changes at C++ Code Change Process step 12. Use
  after reviews, builds, runtime checks, and residual routing to leave primary
  checkout edits uncommitted or safely commit, reconcile, land, and clean up an
  existing session worktree under the PC-global landing lock.
allowed-tools: [Read, Bash, AskUserQuestion]
---

# Finalize Changes

Finalize only the verified change set. Preserve unrelated user changes and never create a worktree at this late stage.

## Rebase-only history

Keep repository history linear. Reconciliation means running `git rebase <primary-branch>` from the session worktree so verified session commits are replayed onto current primary; resolve conflicts with the normal rebase continue/abort flow. Landing means advancing clean primary to that already-rebased session tip with `git rebase <session-branch>` after proving primary is its ancestor. The landing graph operation is a fast-forward, but the command remains `git rebase`.

Never run `git merge`, `git merge --ff-only`, `git pull` without `--rebase`, or `git rebase --rebase-merges`. Never create or preserve a multi-parent commit. Read-only ancestry inspection with `git merge-base` is not history integration and remains allowed.

## Inputs

- Session changed-file list and verification result
- Current checkout path, branch, and commit
- Intended primary checkout path and branch plus session-start/base commit; stop before worktree landing if any is unavailable or ambiguous
- User request for a commit or landing, if any

## Workflow

1. Resolve the canonical repository root, Git directory, common directory, current branch, and `HEAD`. Use `git worktree list --porcelain` to distinguish the primary checkout from linked worktrees and verify the intended primary path and branch.
2. If running in the primary checkout, leave verified changes uncommitted and stop unless the user explicitly requested a commit. For a requested commit, identify the exact verified paths and continue without staging. If running in an existing session worktree, inspect staged, unstaged, and untracked state; stop if a path mixes unrelated edits with the session change. Preserve unrelated index entries, stage only new session files when needed, commit with `git commit --only -- <verified paths>`, and verify the commit diff exactly matches the verified change set. Never push, force-update, destructively reset, or mutate another session's branch or worktree.
3. Use installed `%LOCALAPPDATA%\BrokenEngine\AgentCli\v2\AgentCli.exe`; if missing or `--version` is not exactly `2`, bootstrap it through `/compile`. Generate an owner with `lock token`, resolve `git rev-parse --git-common-dir` to a canonical absolute path, and run `lock claim --domain landing --repo <git-common-dir> --owner <token> --session <label> --worktree <session-worktree>`. If claim returns held, report `lock status --domain landing --repo <git-common-dir>`. Age is warning-only. Takeover requires explicit user approval followed by `lock steal --domain landing --repo <git-common-dir> --expect <reported-owner> --owner <token> --session <label> --worktree <session-worktree>`; stop if the reported owner changed.
4. Require `lock status --domain landing --repo <git-common-dir>` to report this owner. Reverify the primary checkout path, target branch, and `HEAD`. For a session-worktree landing, require clean staged, unstaged, and untracked state in primary. For a requested primary-checkout commit, require all dirt to belong to the verified path set. On any identity or cleanliness failure, leave the claim held and stop; never auto-stash, auto-commit, reset, overwrite, or remove coordination state.
5. For a requested primary-checkout commit, preserve unrelated index entries, stage only new session files when needed, commit with `git commit --only -- <verified paths>`, verify the commit diff exactly matches the verified set and the unrelated index is unchanged, reverify this owner with `lock status`, run `lock release --domain landing --repo <git-common-dir> --owner <token>`, report `COMMITTED`, and stop.
6. For a session-worktree landing, rebase the session branch onto the current primary-branch `HEAD`, resolve conflicts without discarding either side, and rerun every affected review, build, runtime check, documentation update, and project-membership check. Commit only the reconciled session changes. If the user cleaned primary by committing, that commit is the rebase base. If they used a stash, leave reapplication until after landing. Never merge primary into the session branch.
7. Immediately before landing, require `lock status` to report this owner, then re-read the primary checkout path, branch, `HEAD`, in-progress Git state, and porcelain status; record that current primary commit as `<landing-base-head>`. Run `git merge-base --is-ancestor <landing-base-head> <session-branch>` and require success; run `git rev-list --min-parents=2 <landing-base-head>..<session-branch>` and require empty output. If clean primary advanced after recording the landing base, return to step 6 and rebase/reverify. If primary became dirty or changed identity, leave the claim held and stop. From clean primary run `git rebase <session-branch>`, then verify primary and session branches resolve to the verified session commit.
8. Reverify `lock status` reports this owner, then run `lock release --domain landing --repo <git-common-dir> --owner <token>`.
9. If the caller workflow holds another session-scoped claim that may release only after the landed commit is verified, such as a plan claim, use its AgentCli domain and locator to owner-check it with `lock status` and release it with `lock release ... --owner <caller-token>`.
10. Verify the session worktree is clean and its tip is fully contained in primary, change away from its directory, and remove only that worktree and branch. Never force cleanup to bypass a failed check.

Stop and report the exact blocker before any operation that would require broader authority, disturb unrelated changes, bypass a failed verification, or violate lock ownership.

## Output

Report:

- `Finalization: LEFT UNCOMMITTED | COMMITTED | LANDED`
- Checkout, branch, and resulting commit
- Landing-lock and cleanup status
- Verification rerun after reconciliation, if any
- Files changed during finalization, or `none`
- Functions/regions touched during finalization, or `none`
- Residuals: blocker or `none` (always last)
