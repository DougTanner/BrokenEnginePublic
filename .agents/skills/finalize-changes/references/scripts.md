# Bundled Scripts

Use the bundled scripts; never reconstruct their Git, lock, or WorktreeCli
operations. Parse their single fixed-shape JSON result and show only status, code,
message, next-stage state, short counts/paths, and retry or authority
outcome when applicable. Never return a nested tool response or
file/XML/log body. Exit/result/schema mismatches block.

- `../scripts/Invoke-FinalizeCandidateCommit.ps1` stages only the authorized caller
  paths, preserves disjoint state, and blocks mixed owned paths.
- `../scripts/Invoke-FinalizeApprovalPreparation.ps1` squashes the session work to
  one commit on the current primary tip, and blocks with
  `git.primary-not-ancestor` when the session tip does not already contain that
  primary tip; the caller rebases and re-invokes. It returns the only
  landing commit sent to verification.
- `../scripts/Invoke-FinalizeLockClaim.ps1` owns lease claim/status/expiry
  recovery, and standalone release through `-Release` with the held lease's
  owner token. Invoke it successfully before approval preparation begins
  reconciliation, retain or refresh the lease throughout agent-driven
  reconciliation, and release it with `-Release` before any user wait; a
  release of an already-absent lease passes. Live contention is
  retryable; only validated expiry recovers through WorktreeCli's
  compare-and-swap against the recorded owner, run only when no registered
  worktree has a Git operation in progress; unverifiable state requires user
  authority and is never overridden.
- `../scripts/Invoke-FinalizeLanding.ps1` exclusively advances primary by
  compare-and-swap under the landing lock, rolls back on postcondition failure,
  and releases the lock. It claims that lock under a fresh owner token it mints
  itself through WorktreeCli `lock token`, so the caller must already have
  released its reconciliation lease as `SKILL.md` `## Bundled scripts` requires.
  Pass `-ReleasePlanClaim` when a claimed Plan reached final preparation; the
  script then deletes the claim best-effort. Without the switch it invokes no
  `plan` command at all.

Release every caller-owned lease with `../scripts/Invoke-FinalizeLockClaim.ps1 -Release`
before an open-ended user wait. On failure, release a caller-owned lease the
same way only after all registered worktrees are inspectable and free of Git
operation markers; otherwise retain and report it. The landing script's own
lock uses an owner token it never returns; a retained landing claim is cleaned
up only when that lease expires on its own.
