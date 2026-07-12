---
name: prepare-session-worktree
description: >-
  Prepare the isolated Git worktree and fixed baseline for a Broken Engine
  repository-changing session. Use at the start of a plan lifecycle or before
  delegated implementation when session checkout identity has not been
  recorded. Adopt the current orchestrator-managed or valid session worktree
  when possible; otherwise create one unique worktree from the clean primary
  checkout's current branch and HEAD. Report canonical checkout, branch, and
  baseline metadata for every later subagent.
allowed-tools: [Bash]
---

# Prepare Session Worktree

Establish one isolated checkout before repository edits begin. This skill owns
initial discovery, adoption, or creation only; `/finalize-changes` owns commits,
reconciliation, landing, and locking, then retains the landed worktree and
branch for explicit user-managed cleanup.

## Inputs

- Current working directory and repository-changing task
- Current-session provenance: orchestrator metadata, a previously recorded
  handoff, or none when starting from the canonical primary checkout
- Optional caller-provided session label and absolute external worktree root
- Any previously recorded session checkout and fixed baseline

Treat previously recorded metadata as a constraint to verify, not a hint to
replace. Stop if it conflicts with current Git state.

## Discover Repository Identity

1. Resolve canonical absolute paths for the current repository root, Git
   directory, and Git common directory. Read `HEAD`, the attached branch, and
   `git worktree list --porcelain`.
2. Parse complete worktree records rather than inferring identity from folder
   names. Identify the primary checkout as the registered worktree whose Git
   directory is the common directory; require exactly one match. Identify the
   current checkout by exact canonical path; require exactly one match.
3. Resolve the primary checkout's current attached branch and `HEAD`. Those are
   the target branch and source commit. Never substitute `main`, a release
   branch, a remote default, or a remembered branch name.
4. Check porcelain status including untracked files in the current and primary
   checkouts. Detect an in-progress operation by resolving and checking
   `MERGE_HEAD`, `rebase-merge`, `rebase-apply`, `CHERRY_PICK_HEAD`,
   `REVERT_HEAD`, `BISECT_LOG`, and `sequencer` through `git rev-parse
   --git-path`. A dirty checkout, detached current/primary branch, duplicate
   current/primary identity, unresolved operation, or metadata mismatch is a
   blocker. Unrelated worktree records may be detached, dirty, locked, or
   prunable; use them only for branch/path containment and collision checks.
   Report evidence; do not clean, stash, reset, switch, commit, or repair it.

## Adopt the Current Worktree

Adopt instead of creating only when current-session provenance identifies the
current registered linked worktree as orchestrator-managed or as the previously
recorded session worktree. Git shape, branch names, directory names, and
ancestry do not prove ownership; block a linked checkout with no affirmative
provenance rather than risk adopting another session's worktree.

1. Require an attached session branch, clean status, no in-progress Git
   operation, and no other registered worktree using that branch.
2. Require the primary checkout path, target branch, and primary `HEAD` to be
   unambiguous, clean, attached, and free of the in-progress-operation markers
   listed above.
3. If prior session metadata was supplied, require its canonical session path,
   session branch, target branch, and fixed start commit to match. Verify the
   fixed commit exists and is an ancestor of current `HEAD`; target-branch
   advancement after session creation does not invalidate that fixed baseline.
4. For orchestrator provenance without a prior fixed baseline, require the
   current clean `HEAD` to equal the primary `HEAD`, then record it as the fixed
   session-start commit. A divergent, behind, or ahead checkout is not a
   provably fresh session. Do not create another worktree around an adopted one.

If the current checkout is the primary, continue to creation. Never discover
some other linked worktree and adopt or modify it merely because its name looks
related.

## Create a Session Worktree

1. Require the current checkout to be the canonical primary checkout, clean and
   on its attached current branch with no in-progress Git operation. Re-read
   its branch and `HEAD` immediately before creation.
2. Resolve the external worktree root in this order: caller-provided absolute
   root; `$CODEX_HOME/worktrees/<repo-name>`; `$HOME/.codex/worktrees/<repo-name>`
   when `$HOME/.codex` exists; otherwise
   `$HOME/.broken-engine-worktrees/<repo-name>`. Require an absolute canonical
   path outside the repository and every registered worktree. If its safe parent
   exists but the selected root does not, create only that root; otherwise stop.
   Derive a filesystem-safe slug from the task or session label and append a UTC
   timestamp plus a short random suffix. Use it for both a unique session branch
   and a unique worktree directory beneath the resolved root.
3. Prove the candidate branch does not exist locally, the path is absent from
   `git worktree list --porcelain`, and the filesystem path does not exist.
   Generate another unique candidate on collision; never delete or reuse an
   existing branch or directory.
4. Run `git worktree add -b <session-branch> <session-path> <primary-head>`.
   This is the only Git mutation this preparation workflow performs.
5. Verify the new worktree is registered at the exact canonical path, has the
   intended attached branch, is clean, and resolves to the recorded primary
   `HEAD`. Re-read the primary checkout and require its branch, `HEAD`, worktree
   files, and index to remain unchanged.
6. Record the source commit as the fixed session-start commit. All later change
   discovery compares against this commit even if the target branch advances or
   the session is rebased during finalization.

Never push, force-update, create a nested worktree, touch another worktree,
remove an existing worktree or branch, or mutate primary checkout contents.
Leave partial artifacts from a failed creation untouched and report their exact
paths and refs so a user can decide how to recover.

## Handoff

Return this concise block for the main session to pass unchanged to every
subagent:

```text
Session worktree:
- Primary checkout: <canonical absolute path>
- Target branch: <branch>
- Session worktree: <canonical absolute path>
- Session branch: <branch>
- Session-start commit: <full commit>
- Preparation: ADOPTED | CREATED
- Residuals: none | <exact blocker or partial artifact>
```

On a blocker, use `Preparation: BLOCKED`, write `<unproven>` for every field not
established by evidence, and do not begin repository edits.
