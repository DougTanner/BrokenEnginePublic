---
name: finalize-changes
description: >-
  Finalize verified repository changes at C++ Code Change Process step 12. Use
  only after the step-9 verification ledger proves every testable change, then
  leave primary checkout edits uncommitted or safely commit, reconcile, and
  land an existing session worktree under the PC-global landing lock. Before
  rebasing onto the primary branch, present a detailed change summary and
  require explicit user sign-off. Retain the worktree and branch for
  user-managed cleanup.
allowed-tools: [Read, Bash, AskUserQuestion]
---

# Finalize Changes

Finalize only the verified change set. Preserve unrelated user changes, never create a worktree at this late stage, and never remove a session worktree or branch.

## Rebase-only history

Keep repository history linear. Reconciliation means running `git rebase <primary-branch>` from the session worktree so verified session commits are replayed onto current primary; resolve conflicts with the normal rebase continue/abort flow. Landing means advancing clean primary to that already-rebased session tip with `git rebase <session-branch>` after proving primary is its ancestor. The landing graph operation is a fast-forward, but the command remains `git rebase`.

Never run `git merge`, `git merge --ff-only`, `git pull` without `--rebase`, or `git rebase --rebase-merges`. Never create or preserve a multi-parent commit. Read-only ancestry inspection with `git merge-base` is not history integration and remains allowed.

## Inputs

- Session changed-file list and the final-tree step-9 verification ledger: it must contain the final changed-file manifest (normalized paths plus content hashes/deletion markers) and give every in-scope testable item exact `PASS` evidence
- Current checkout path, branch, and commit
- Intended primary checkout path and branch plus session-start/base commit; stop before worktree landing if any is unavailable or ambiguous
- User request for a commit or landing, if any

## Workflow

1. Resolve the canonical repository root, Git directory, common directory, current branch, and `HEAD`. Use `git worktree list --porcelain` to distinguish the primary checkout from linked worktrees and verify the intended primary path and branch. Recompute the changed-file manifest from the current worktree and require an exact match with the final-tree step-9 ledger; stop if any path/hash/deletion marker differs or any in-scope testable item is failed, skipped, blocked, or unverified. A user decision changes scope/acceptance before verification—it does not waive this gate.
2. If running in the primary checkout, leave verified changes uncommitted and stop unless the user explicitly requested a commit. For a requested commit, identify the exact verified paths and continue without staging. If running in an existing session worktree, inspect staged, unstaged, and untracked state; stop if a path mixes unrelated edits with the session change. Preserve unrelated index entries, stage only new session files when needed, commit with `git commit --only -- <verified paths>`, and verify the commit diff exactly matches the verified change set and the committed file contents/deletions are byte-identical to the ledger manifest. A commit that only binds the already-verified manifest to a Git tree does not invalidate the ledger. Never push, force-update, destructively reset, or mutate another session's branch or worktree.
3. Before any session-side rebase, stop and request explicit user sign-off. Present a detailed summary derived from the verified final tree, not a generic completion message. Include:
   - user-visible behavior and implementation changes, grouped by subsystem or plan objective
   - every changed, added, renamed, and deleted file, with each file's purpose and material regions changed
   - review findings fixed or rejected, including the evidence for rejection
   - build, runtime, determinism, documentation, project-membership, and other verification performed, with exact pass/fail/skip status
   - unresolved residuals, follow-up plans, known limitations, and unverified items; state `none` explicitly where applicable
   - session branch and commit, primary branch and current commit, and the exact next operations: rebase session onto primary, rerun affected verification, then advance primary to the verified session tip
   End with a direct confirmation question that makes clear no rebase or primary-branch update has occurred yet. Do not treat earlier plan approval, implementation approval, or general instructions to finish as sign-off. Proceed only after an explicit affirmative response to this exact summary. If the user declines, requests changes, or does not answer, stop with both branches unchanged. If the verified manifest, session tip, primary identity, primary tip, scope, residuals, or verification status changes after approval and before the rebase begins, invalidate the approval and present a refreshed summary for new confirmation.
4. Use installed `%LOCALAPPDATA%\BrokenEngine\AgentCli\v2\AgentCli.exe`; if missing or `--version` is not exactly `2`, bootstrap it through `/compile`. Generate an owner with `lock token`, resolve `git rev-parse --git-common-dir` to a canonical absolute path, and run `lock claim --domain landing --repo <git-common-dir> --owner <token> --session <label> --worktree <session-worktree>`. If claim returns held, report `lock status --domain landing --repo <git-common-dir>`. Age is warning-only. Takeover requires explicit user approval followed by `lock steal --domain landing --repo <git-common-dir> --expect <reported-owner> --owner <token> --session <label> --worktree <session-worktree>`; stop if the reported owner changed.
5. Require `lock status --domain landing --repo <git-common-dir>` to report this owner. Reverify the primary checkout path, target branch, and `HEAD`. For a session-worktree landing, require clean staged, unstaged, and untracked state in primary. For a requested primary-checkout commit, require all dirt to belong to the verified path set. On any identity or cleanliness failure, leave the claim held and stop; never auto-stash, auto-commit, reset, overwrite, or remove coordination state.
6. For a requested primary-checkout commit, preserve unrelated index entries, stage only new session files when needed, commit with `git commit --only -- <verified paths>`, verify the commit diff exactly matches the verified set and the unrelated index is unchanged, reverify this owner with `lock status`, run `lock release --domain landing --repo <git-common-dir> --owner <token>`, report `COMMITTED`, and stop.
7. For a session-worktree landing, rebase the session branch onto the approved primary-branch `HEAD` and resolve conflicts without discarding either side. After every session-side reconciliation rebase, including a conflict-free one, rerun every affected review, build, runtime check, documentation update, and project-membership check, then regenerate the final-tree ledger and changed-file manifest for the rebased commit. Feed any failure back through root step 9's test -> fix -> retest loop; do not land until every affected testable item passes again. Commit only the reconciled session changes. If reconciliation, conflict resolution, or subsequent fixes change any fact covered by the approved summary, present the complete refreshed step-3 summary and require renewed explicit confirmation before advancing primary. If the user declines or does not answer, release this session's landing lock and stop with primary unchanged. If the user cleaned primary by committing, that commit is the rebase base. If they used a stash, leave reapplication until after landing. Never merge primary into the session branch.
8. Immediately before landing, require `lock status` to report this owner, record the exact rebased session tip as `<verified-session-commit>`, and require the refreshed ledger's manifest to match that commit. Re-read the primary checkout path, branch, `HEAD`, in-progress Git state, and porcelain status; record that clean current primary commit as `<landing-base-head>`. Run `git merge-base --is-ancestor <landing-base-head> <verified-session-commit>` and require success, require `git rev-list <verified-session-commit>..<landing-base-head>` and `git rev-list --min-parents=2 <landing-base-head>..<verified-session-commit>` both produce empty output, then re-read primary identity, `HEAD`, in-progress state, and status and require the session branch still resolves to `<verified-session-commit>`. If clean primary advanced after recording the landing base or the session branch moved, return to step 7 and rebase/reverify; if primary became dirty or changed identity, leave the claim held and stop. These checks must prove the final primary-side `git rebase <verified-session-commit>` has no primary commits to replay and can only advance the primary ref to the exact already-verified session commit without creating or rewriting commits or content. If that cannot be proven, do not run the landing command; return to step 7 for reconciliation and verification. From the clean, unchanged primary run `git rebase <verified-session-commit>`, then require primary and session branches to resolve to `<verified-session-commit>` and require the primary-tree manifest to match the refreshed ledger exactly. This pure ref advance does not require another test rerun after primary is updated. If either post-landing identity or manifest check fails, leave the claim held and stop.
9. Reverify `lock status` reports this owner, then run `lock release --domain landing --repo <git-common-dir> --owner <token>`.
10. If the caller workflow holds another session-scoped claim that may release only after the landed commit is verified, such as a plan claim, use its AgentCli domain and locator to owner-check it with `lock status` and release it with `lock release ... --owner <caller-token>`.
11. Verify the session worktree is clean, remains registered at the recorded path on the recorded branch, and its tip is fully contained in primary. Retain the worktree and branch unchanged for explicit user-managed cleanup; do not run `git worktree remove`, delete the directory, or delete the branch.

Stop and report the exact blocker before any operation that would require broader authority, disturb unrelated changes, bypass a failed verification, or violate lock ownership.

## Output

Report:

- `Finalization: LEFT UNCOMMITTED | COMMITTED | LANDED`
- Checkout, branch, and resulting commit
- Landing-lock status and retained session worktree/branch
- User sign-off status and the commit pair covered by that approval
- Verification rerun after reconciliation, if any
- Files changed during finalization, or `none`
- Functions/regions touched during finalization, or `none`
- Residuals: blocker or `none` (always last)
