---
name: finalize-changes
description: >-
  Squash, rebase, summarize, and land a verified session change onto the
  primary branch under the global landing lock, then delete the machine-local
  Plan claim. Also use for an explicitly requested commit directly on primary.
allowed-tools: [Read, Bash, PowerShell, AskUserQuestion]
---

# Finalize Changes

Main dispatches one `implementer` for the normal session route and resumes that
worker after main obtains the authoritative confirmation in `## Landing
confirmation`. The worker never prompts, delegates, pushes, creates/removes
worktrees, or disturbs unrelated changes. Keep history linear: reconcile with
`git rebase`, never merge or use `--rebase-merges`.

## Inputs and ownership

Require the approved objective, its stage decisions, and the caller-owned
changed paths. The single landing `/verify-changes` pass runs inside the
workflow below, after final preparation and reconciliation have produced the
final diff — do not require or reuse an earlier PASS. Resolve checkout,
primary, and session identity from `Get-AgentWorktreeSessionContext`.

This skill owns final Plan preparation, landing-commit creation, reconciliation,
the landing summary, the landing confirmation, locked primary change, claim
deletion, and recovery. `/verify-changes` alone owns acceptance.

If the route is a separately requested commit directly on primary, load
`references/primary-commit.md`; this is that exceptional reference's sole
trigger.

## Bundled scripts

Every script this skill runs lives under `scripts/`; load `references/scripts.md`
for their contracts, result handling, and lease rules.

Invoke them directly, exactly as documented; never write a wrapper,
orchestrator, or replacement around them, and never reconstruct their steps by
hand. An improvised invocation can send wrong arguments into an operation that
changes primary. A script that cannot be run as documented is a bug: stop and
report it.

The caller must release its reconciliation lease through
`scripts/Invoke-FinalizeLockClaim.ps1 -Release` before landing is invoked,
because `scripts/Invoke-FinalizeLanding.ps1` mints its own owner token through
WorktreeCli `lock token` and sees a still-held caller lease as foreign
contention, which fails the claim.

## Normal workflow

1. When a claimed Plan finished, run final preparation first: `plan complete`,
   or `plan reject --user-authorized-rejection` after explicit user-authorized
   rejection. It rewrites direct dependency-children markers and deletes the
   target Plan in the worktree, and returns the `changedPaths` the landing commit must
   contain. The claim stays held. Final preparation is not completion.
2. Create the authorized landing commit, then squash and rebase it onto the current
   primary tip. Reconciliation never advances primary. Inspect dependency
   overlap and any place the rebase merged cleanly but changed the code's
   meaning.
3. Dispatch `/verify-changes` on the resulting diff. A meaningful change to that
   diff re-runs review of the changed regions only.
4. Invoke `scripts/Show-FinalizeApprovalReview.ps1 -LaunchSmartGit` last, once
   the approved tip is bound, so the launch is always attempted on the landing commit
   before the user is asked anything and the review window is open whenever
   SmartGit is available. Exit-0 statuses `opened`, `unavailable`, and `failed`
   are non-blocking: surface status, the authoritative `manualCommand`, and the
   message for `unavailable`/`failed`. Any exit `1`, `error` status, malformed
   result, or schema mismatch blocks. Do not reinvoke after a clean identical
   post-confirmation rebase that preserves the existing confirmation; a meaningful
   change requiring a refreshed confirmation launches it again against the newly
   reviewed landing commit before that refreshed confirmation. Then return a landing
   summary: `## Context`, outcome-focused `## What landed`, and `## Landing`
   stating `Primary has not advanced.`, both branches, objective decisions,
   changed-file count/kind, and the exact remaining operation. Main presents it
   immediately before the authoritative confirmation question.
5. Only that affirmative response permits the same worker to invoke landing, and
   only once the reconciliation lease is released through
   `scripts/Invoke-FinalizeLockClaim.ps1 -Release`, in the order
   `## Bundled scripts` states. Landing takes the lock lease, advances primary by
   compare-and-swap with rollback on failure, and releases the lock. For a claimed Plan pass
   `-ReleasePlanClaim` so the machine-local claim is deleted best-effort; a
   failed claim delete is a reported residual, never a landing blocker.
6. If primary advanced before the advance succeeds, rebase and repeat from the
   step the change requires under `## Landing confirmation`.
7. Retain the session branch and worktree; only `/cleanup-worktrees` or explicit
   user direction removes them.

`/session-audit` runs only on explicit user request.

## Landing confirmation

Main presents the self-contained summary immediately before the question:
one-sentence change, changed-file count and kind, session and primary branches,
all objective-stage decisions, and the exact remaining operation, plus two lines
main answers itself from the whole session record (worker handoffs, rejected
review findings, residuals) — the finalizer worker does not produce them:

- **Least confident:** the one part of the change main trusts least, and why.
- **Likely blind spot:** the consequence or consideration the user has not raised
  and most likely would want to know.

Both lines need a real answer in plain words the user can act on, with no
repository jargon; `none` is allowed only with a stated reason. Then ask
exactly:

- session: `Confirm landing this change from <session-branch> onto primary branch <primary-branch>?`
- separately requested primary commit: `Confirm commit of this change on primary branch <primary-branch>?`

Only a current explicit affirmative response authorizes primary change. Plan
or implementation approval, a request to finish or land, or reconciliation
consent is not a substitute. Main resumes the same finalizer after
confirmation. A decline or non-answer leaves primary unchanged. `/save-plan` is
the sole standing exception, and only when the change contains exactly the
saved Plan file.

Confirmation binds the reviewed diff, not commit hashes. A clean identical
rebase onto an advanced primary lands without re-asking. A conflict resolution,
a change to the session bytes, or a meaningful semantic change re-runs review of
the affected regions and requires a refreshed summary and a fresh confirmation.

## Recovery

- Reconciliation conflict: resolve under the approved invariants, then re-review
  the affected regions and re-ask the confirmation. Resolve hunk by hunk, tracing
  each side's intent to its originating commit or Plan before choosing, and
  preserve both intents where they are compatible. Keep the resolution to
  behavior one side already had; never invent new behavior in a resolution.
  Abort the rebase only to return a blocker when the approved decisions do not
  determine a valid resolution; otherwise resolve rather than abort.
- Process died after primary advanced: re-invoke
  `scripts/Invoke-FinalizeLanding.ps1`, which is idempotent against an
  already-advanced tip, then delete the claim.
- Primary history rewritten under the session: rebase the session branch with
  `git rebase --onto <new-primary-tip> <old-fork-point>` and re-export
  `BROKEN_ENGINE_BASELINE` to the new tip so attribution stays correct.

## Exceptional references

When the landing-commit/landed diff contains a non-Markdown path under
`Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/`, load
`references/agenttools.md` for shared-artifact idle waiting, disclosure,
promotion, and bootstrap coordination. This is the sole AgentTools trigger;
non-triggering routes do not load it.

## Completion and output

Return finalization state, objective state, checkout/branches/resulting commit,
lock/reconcile/sign-off/landing status, files changed during reconciliation,
and `Residuals` last. Emit the repository `SESSION COMPLETE` block only after
landing succeeded, the claim was released, the worktree is clean, and every
objective stage is complete or explicitly deferred to a named unclaimed Plan.
Never emit it for uncommitted, blocked, dirty, retained-claim, or still-active
work.
