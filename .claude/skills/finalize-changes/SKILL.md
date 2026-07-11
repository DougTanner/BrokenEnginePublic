---
name: finalize-changes
description: >-
  Finalize verified repository changes at C++ Code Change Process step 13. Use
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
2. If running in the primary checkout, leave verified changes uncommitted and stop unless the user explicitly requested a commit. For a requested commit, identify the exact verified paths and continue to the locked primary-commit branch in step 5; do not stage yet or simulate a landing.
3. If running in an existing session worktree, inspect staged, unstaged, and untracked state; stop if a path mixes unrelated edits with the session change. Preserve unrelated index entries, stage only new session files when needed, commit with `git commit --only -- <verified paths>`, and verify the commit diff exactly matches the verified change set. Never push, force-update, destructively reset, or mutate another session's branch or worktree.
4. Serialize landing with an atomically created lock at `%LOCALAPPDATA%\BrokenEngineLandingLocks\<repository-id>.lock`:
   - Resolve `git rev-parse --git-common-dir` to a canonical absolute Windows path, convert `/` to `\`, remove trailing separators unless the path is a filesystem root, lowercase invariantly, hash its UTF-8 bytes with SHA-256, and use the 64-character lowercase digest as `<repository-id>`.
   - Create the lock directory idempotently, then create the lock file with an atomic create-new operation such as .NET `FileMode.CreateNew`. Store an unguessable owner token plus session, process, worktree, branch, and timestamp metadata. Re-verify the token before every critical operation and before releasing the lock.
   - If the lock exists, report its owner metadata. Age is warning-only. Takeover requires explicit user approval, a re-read proving the owner token is unchanged, and atomic owner replacement.
5. For a requested primary-checkout commit, re-verify the checkout identity, branch, `HEAD`, staged/unstaged/untracked state, and verified path ownership while holding the lock. Stop if a path mixes unrelated edits with the session change. Preserve unrelated index entries, stage only new session files when needed, commit with `git commit --only -- <verified paths>`, verify the commit diff exactly matches the verified set and the unrelated index is unchanged, release the owned lock, report `COMMITTED`, and stop.
6. For a session-worktree landing, verify the primary checkout identity and cleanliness while holding the lock. If it has modified or untracked files, list their overlap with the session change set, release the owned landing lock, and stop: uncommitted files are not branch history and cannot participate in rebase reconciliation. Never auto-stash, auto-commit, reset, or overwrite them. The user must first commit them, stash them explicitly, or authorize their removal; retry from lock acquisition afterward.
7. Rebase the session branch onto the current primary-branch `HEAD`, resolve conflicts without discarding either side, and rerun every affected review, build, or runtime check before landing. If the user cleaned the primary checkout by committing, that new commit becomes the rebase base. If they used a stash, leave stash reapplication until after landing; any resulting conflict is outside the verified session change. **Never merge primary into the session branch:** merge commits and `git merge`-based reconciliation are prohibited.
8. Immediately before landing, re-read lock ownership, primary checkout path and branch, `HEAD`, in-progress Git state, and porcelain status. Run `git merge-base --is-ancestor <recorded-primary-head> <session-branch>` and require success; run `git rev-list --min-parents=2 <recorded-primary-head>..<session-branch>` and require empty output. If the clean primary `HEAD` advanced, return to step 7 and rebase/reverify; if it became dirty or changed identity, release the owned lock and stop. From the clean primary checkout, advance it with `git rebase <session-branch>`; because primary is already an ancestor, this operation only advances to the session tip and must not replay or rewrite primary commits. Verify the primary checkout and both branches resolve to the verified session commit. The later lander owns reconciliation.
9. Release the landing lock only after re-verifying ownership. Then verify the session worktree is clean and its tip is fully contained in primary, change away from its directory, and remove only that worktree and branch. Never force removal to bypass a failed check.

Stop and report the exact blocker before any operation that would require broader authority, disturb unrelated changes, bypass a failed verification, or violate lock ownership.

## Output

Report:

- `Finalization: LEFT UNCOMMITTED | COMMITTED | LANDED`
- Checkout, branch, and resulting commit
- Landing-lock and cleanup status
- Verification rerun after reconciliation, if any
- Residual blocker or `none`
