# Bundled Scripts

Use the bundled scripts; never reconstruct their Git, lock, or WorktreeCli
operations. Parse their single fixed-shape JSON result and show only status, code,
message, next-stage state, short counts/paths, and retry or authority
outcome when applicable. Never return a nested tool response or
file/XML/log body. Exit/result/schema mismatches block.

- `../scripts/Invoke-FinalizeCandidateCommit.ps1` stages only the authorized caller
  paths, preserves disjoint state, and blocks mixed owned paths. `pwsh -File`
  hands every argument over as one literal string, so several owned paths can
  only travel as one comma-separated `-OwnedPaths` token.
- `../scripts/Invoke-FinalizeApprovalPreparation.ps1` squashes the session work to
  one commit on the current primary tip, and blocks with
  `git.primary-not-ancestor` when the session tip does not already contain that
  primary tip; the caller rebases and re-invokes. It returns the only
  landing commit sent to verification.
- `../scripts/Invoke-FinalizeLockClaim.ps1` makes one blocking lease claim —
  WorktreeCli owns the bounded wait and the guarded expiry recovery — and
  separately performs standalone release through `-Release` with the held
  lease's owner token.
  Invoke it successfully before approval preparation begins
  reconciliation, retain or refresh the lease throughout agent-driven
  reconciliation, and release it with `-Release` before any user wait, in the
  order `SKILL.md` `## Bundled scripts` states; a release of an already-absent
  lease passes. The post-confirmation landing claim uses the landing lease
  duration — `-LeaseSeconds 3600`, its default, so omitting the parameter is
  correct; a refresh keeps a lease's original duration, so landing refuses to
  continue a shorter one. Live contention is
  retryable; only validated expiry recovers through WorktreeCli's
  compare-and-swap against the recorded owner, run only when no registered
  worktree has a Git operation in progress; unverifiable state requires user
  authority and is never overridden.
- `../scripts/Invoke-FinalizeLanding.ps1` exclusively advances primary by
  compare-and-swap under the landing lock, rolls back on postcondition failure,
  and releases the lock. Pass the post-confirmation claim's owner token as
  `-OwnerToken` so landing continues under that same lease, which it accepts only
  as a same-actor continuation under the `SKILL.md` `## Bundled scripts` ownership
  rule, preserving the raw `$SessionLabel`. Without `-OwnerToken`, it derives
  `$SessionLabel/landing`, first inspects the lock, and adopts a live owner only
  when that exact derived session and the same canonical worktree match and the
  recorded integer lease duration is at least the 3600-second landing duration; it
  immediately refreshes a matching retained lease before proceeding, preserving
  ownership across a possible rebase; otherwise
  it mints its own token through WorktreeCli `lock token` and claims under the
  derived identity. A recovery invocation applies the identity matching rule
  without the duration gate to release a live retained claim because it performs
  no rebase or advance; foreign, mismatched, and unverifiable claims are untouched.
  When primary advanced first it makes at most one internal rebase
  and lands only a provably byte-identical patch, so report the commit from the
  result's `landed` block rather than `candidate`. A blocked result reports its
  `disposition` and a `lock` projection; act on those, never a memorized code
  list. A `retryable-wait` result may be re-invoked with the approval-bound
  arguments after its reported `retryAfterMilliseconds`. When it acquired no lock
  (`lock.claimed` false), simply re-invoke — on the caller-token route claim the
  landing lock anew through `Invoke-FinalizeLockClaim.ps1` first and pass the new
  owner token. When it acquired but did not release the lock (`lock.claimed`
  true, `lock.released` false), the claim is retained: handle it per the release
  paragraph below before claiming anew. Blocked
  `rebase.patch-not-identical` means the clean rebase changed the patch, which
  returns for re-review and a refreshed confirmation; `rebase.conflicted` leaves
  the session branch restored; `rebase.abort-failed` leaves restoration unproven
  and always retains the lease; `landing.retry-exhausted` is retryable and leaves
  the confirmed session commit restored. Re-invoking it with the original approved
  arguments after a crash is idempotent, including against a tip its own internal
  rebase produced.
  Pass `-ReleasePlanClaim` when a claimed Plan reached final preparation; the
  script then deletes the claim best-effort. Without the switch it invokes no
  `plan` command at all.

Release every caller-owned lease with `../scripts/Invoke-FinalizeLockClaim.ps1 -Release`
before an open-ended user wait. After a failed landing the lock is released once
every registered worktree is inspectable and provably free of Git operation
markers, on both the supplied-token and omitted-token routes. When that cannot be
proven the claim is retained and reported: a supplied token's retained claim
stays the caller's lease, released the same way once the worktrees are provably
clear; an omitted-token claim is discoverable by a later invocation only when
its derived session and canonical worktree match, and every foreign or
unverifiable claim stays untouched until its normal expiry or external repair.
