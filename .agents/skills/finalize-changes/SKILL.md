---
name: finalize-changes
description: >-
  Reconcile and finalize verified repository changes:
  commit on primary only when explicitly requested, or rebase and land a
  session worktree under the PC-global landing lock, always with explicit user
  sign-off on the landing summary before primary history changes; release a
  receipt-bound terminal Plans claim after the landed state is proven.
allowed-tools: [Read, Bash, PowerShell, AskUserQuestion]
---

# Finalize Changes

Main dispatches one `implementer` to finalize only the verified change set. The
worker preserves unrelated user changes, never creates or removes a session
worktree or branch, never pushes, and never delegates. Use this workflow only
for a requested commit or primary-branch landing, whose commit, reconciliation
rebase, landing lock, parent-branch update, and terminal Plan claim release the
finalization worker exclusively owns. Main only adjudicates its concise
handoffs, presents the exact summary and confirmation question, and resumes the
worker after the user's answer. Only main prompts the user; the finalization
worker never does.

Approval and confirmation semantics — what authorizes a primary mutation and when a refreshed summary is required — are owned by the canonical execution-gate contract (`../next-plan/references/execution-gates.md`, state 4); do not restate or redefine them. The confirmation binds the session diff, not exact tips: a primary advance after confirmation is handled by re-rebasing and landing without returning to the user.

## Rebase-only history

Keep history linear. Reconciliation runs `git rebase <primary-branch>` from the session worktree; landing advances clean primary to the already-rebased session tip with `git rebase <session-branch>` after proving primary is its ancestor. Never `git merge`, `git pull` without `--rebase`, or `--rebase-merges`; never create a multi-parent commit.

## Inputs

- The `/verify-changes` completion summary: `Verification: PASS`, its fixed
  baseline, exact reconciled candidate commit/tree and sole parent, Git-derived
  changed-file inventory, and every in-scope acceptance check passed
- Current and primary checkout identities; for a session landing, the durable session identity resolved by `Get-AgentWorktreeSessionProvenance` from the in-worktree receipt
- When a terminal Plan claim is held: its internally resolved state and
  disposition. Sidecars discover the deterministic local receipt; its identity
  never enters a handoff. A deferred or absent claim is ordinary no-claim work.

## Sidecars

These scripts own the mechanics; never reconstruct their Git, lock, WorktreeCli, or SmartGit commands inline. Each documents its contract and returns one JSON object. `Test-FinalizePreflight.ps1`, `Invoke-FinalizeApprovalPreparation.ps1`, `Test-AgentToolsCandidateCertification.ps1`, and `Invoke-AgentToolsPromotion.ps1` succeed only on exit `0`, `status: pass`, `code: ok`; `Invoke-FinalizeLanding.ps1` succeeds only on exit `0`, `status: landed`, `code: ok`. Exit `2` is a deterministic blocker. `Show-FinalizeApprovalReview.ps1` accepts exit `0` with `opened`, `unavailable`, or `failed`; only its invalid-input exit `1` blocks. Coordination results carry top-level `disposition`, `requiresUserAuthority`, and retry timing: `retryable-wait` and `shared-quiescence` never require external repair/decision authority; `authority-required` requires it; input/internal failures are `terminal`. Keep plumbing output out of the user transcript: print only `status`, `code`, and decision-relevant fields.

- `scripts/Test-FinalizePreflight.ps1` — canonical read-only structural preflight (identity, clean Git state, WorktreeCli capability, session-landing receipt, and optional terminal Plan receipt). Approval preparation and landing both invoke it. During the one-shot scheduler cutover, an approval-bound v2 candidate receipt may supply the current WorktreeCli when the canonical executable lacks required scheduler capabilities; certification binds that executable to the approved source. All other paths remain canonical and strict.
- `scripts/Invoke-FinalizeCandidateCommit.ps1` — creates the candidate only
  after terminal Plan preparation. It stages only the authorized-path union,
  preserves disjoint staged, unstaged, and untracked state, and blocks a mixed
  owned path. On a session route it produces the pre-reconciliation candidate;
  on a primary-commit route it uses a temporary index and does not advance the
  real primary ref or alter its real index until its later exact-candidate CAS.
- `scripts/Invoke-FinalizeApprovalPreparation.ps1` — reconciles the session
  candidate and squashes it to one tree-identical commit whose sole parent is
  the current primary tip. Its returned candidate commit/tree is the only
  session landing candidate passed to `/verify-changes`. A later primary
  advance rebases that verified candidate directly; it does not rebuild an
  equivalent commit from mutable worktree bytes.
- `scripts/Invoke-FinalizeLockClaim.ps1` — bounded pre-confirmation reconcile-lease claim. It exposes the common claim/status/exact-expiry/recover policy also used by post-confirmation landing: live contention is retryable, exactly validated expired metadata recovers automatically only through WorktreeCli's owner-CAS/all-worktrees-clear primitive, and unverifiable metadata requires external repair/decision authority and is never overridden or bypassed.
- `scripts/Wait-AgentToolsQuiescence.ps1` — required bounded read-only shared-artifact gate. Invoke it only for a canonical shared-artifact mutation:

  ```powershell
  & "$PRIMARY\.agents\skills\finalize-changes\scripts\Wait-AgentToolsQuiescence.ps1" `
      -RepositoryRoot $PRIMARY `
      -CooperatingSessionOwner $env:BROKEN_ENGINE_SESSION_OWNER `
      -WaitSeconds 55
  ```

  The durable session owner holds no WorktreeCli ledger claim; this gate waits only on in-flight transient operation claims. After a client restart `$env:BROKEN_ENGINE_SESSION_OWNER` is unset; resolve the durable owner from the in-worktree receipt (`Get-AgentWorktreeSessionProvenance`) instead. It emits one `broken-engine-shared-quiescence/v1` JSON result. Exit `0` reports `quiescent` or `shared-quiescence`, always with `requiresUserAuthority:false`, `retryAfterSeconds`, `waitedMilliseconds`, and `liveBlockers`; an unreadable, malformed, or invalid coordination ledger exits `2` as `authority-required` with `requiresUserAuthority:true`. Other input/internal failures exit `1` as `terminal` with no-authority. Reinvoke only after `retryAfterSeconds` when it reports `shared-quiescence`.
- `scripts/Show-FinalizeApprovalReview.ps1` — sole owner of the SmartGit review-window launch, invoked last in step 5 immediately before the landing summary so the user returns from their review to a finished confirmation question. `unavailable`/`failed` is non-blocking: surface its message and exact `manualCommand` in the approval response. Not re-invoked after a post-confirmation re-rebase.
- `scripts/Invoke-FinalizeLanding.ps1` — exclusive owner of the post-approval
  landing transaction (structural preflight, Plan validation, locks, ancestry
  proofs, guarded primary ref advance to the exact verified candidate, and
  receipt-bound terminal claim release). It never reconstructs a primary commit
  from the mutable worktree or uses `git commit --only` to recreate evidence.
- `scripts/Test-SessionAuditRequirement.ps1` — read-only post-reconciliation
  evaluator. Supply one complete `broken-engine-session-audit-input/v2` JSON
  object containing repository identity, exact pre/post-reconciliation candidate
  commits, and Git-native deltas derived from their parents; exact
  dependency-overlap evidence; conflict/domain/adversarial
  coverage dispositions; late-fix/manual-resolution/invalidated-assumption/
  unseen-region flags; and explicit-user-request flag. It emits one
  `broken-engine-session-audit-decision/v2` object; only exit `0` with
  well-formed `required:false` permits automatic skip. Missing or malformed
  evidence, a decision mismatch, or `required:true` requires `/session-audit`.
- `scripts/Test-AgentToolsCandidateCertification.ps1` — read-only v2 receipt certification against immutable executables, stable before/after manifests, same-checkout source bytes, and expected-commit clean-filter blobs, including primary-resolved `json.hpp`. Any exit `2` mismatch requires a rebuilt candidate.
- `scripts/Invoke-AgentToolsPromotion.ps1` — guarded AgentTools promotion, invoked only by workflow step 6b after a landing whose diff requires it; never run it for any other purpose.

## Terminal Plan claims

Preflight, approval preparation, and landing rediscover the deterministic
receipt beneath session `Temp`. A changed, missing, reparse, foreign, or
disposition-mismatched receipt blocks before primary mutation.

Terminal preparation writes a recoverable manifest, deletes the selected Plan,
and removes only direct child dependency edges. A recovery that changes child
metadata after landing confirmation invalidates that confirmation; revalidate
and present the refreshed candidate. Release requires the actual primary tip to
contain the terminal state. Never recreate a terminal claim after primary
advances.

## Fixtures

Fixtures are assertion-driven developer checks, not sidecars. They emit deterministic pass/fail lines and exit `0` only when every assertion passes; any assertion or setup/cleanup failure exits nonzero.

- `scripts/Test-LandingLockStatusFixtures.ps1` — deterministic scratch-repository coverage for the public landing-lock response contract; run it against a candidate WorktreeCli whenever that contract changes.
- `scripts/Test-AgentToolsPromotionFixtures.ps1` — first checks both supplied executables against the current capability contract, then runs scratch-only v2 certification, coordination, promotion, rollback, source-change, and binary-tamper coverage; never point it at primary outputs.
- `scripts/Test-FinalizeWorkflowFixtures.ps1` — scratch-only receipt binding, shared reconcile/landing claim behavior, terminal Plan preparation and release, session landing, and post-advance recovery coverage.
- `scripts/Test-SessionAuditRequirementFixtures.ps1` — scratch-only coverage for unchanged-delta skip and every fail-closed audit trigger.

## Workflow

Required order: terminal preparation -> candidate creation -> reconciliation/single-parent squash -> exact candidate verification -> finalization summary and explicit confirmation -> primary mutation.

1. Prepare terminal Plans before candidate creation. The terminal sidecar discovers its
   receipt internally, writes only its recoverable scheduler state, deletes the
   selected Plan and direct child edges, and leaves an `awaiting-landing` claim.
   It persists its original receipt-bound result and manifest digest at
   `Temp/next-plan-terminal-result.json`; candidate creation validates that proof
   against the current receipt-bound claim state instead of replaying preparation.
   Its receipt-proven `changedPaths` join the declared caller-owned paths as the
   exact authorization union. A receipt, proof, digest, recovery, or mixed-path failure
   blocks before candidate creation; no terminal state is recreated after a
   primary advance.
2. Create and reconcile the candidate before verification, in this mandatory
   order: terminal preparation -> candidate creation -> reconciliation/
   single-parent squash -> exact candidate verification -> finalization summary
   and explicit confirmation -> primary mutation. `Invoke-FinalizeCandidateCommit.ps1`
   constructs the candidate from only the authorization union. It preserves
   disjoint staged, unstaged, and untracked state, but blocks any owned path
   containing mixed unrelated edits. For a session, claim the reconcile lease
   and rebase/squash to one exact candidate whose sole parent is the current
   primary tip. For a primary commit, use a temporary index to construct that
   candidate without moving primary. Never continue with a missing, non-commit,
   wrong-parent, wrong-tree, or changed-tip candidate.

   Reconcile lock, overlap, and conflicts. `Invoke-FinalizeLockClaim.ps1` owns
   the bounded lease policy: foreign live contention is retryable; only exactly
   validated expiry recovers through WorktreeCli; unverifiable state needs
   authority and is never bypassed. Refresh only during agent-driven work and
   release before a user wait. Inspect dependency overlap and semantic merge
   hazards after a rebase; resolve conflicts according to the existing
   determinism-counter rule. A conflict resolution or late fix creates a
   replacement candidate and routes only the checks invalidated by its
   Git-native delta. Reconciliation never advances primary.
3. Dispatch `/verify-changes` only after step 2 returns the exact candidate.
   It derives the reviewed inventory from Git and returns `Verification: PASS`
   bound to the fixed baseline and candidate commit/tree. Do not present a
   summary or request confirmation until that pass exists. A later candidate,
   parent, tree, or tip change requires a replacement-candidate verification.
4. After the verifier passes, assemble and evaluate the v2 session-audit input
   from the pre/post-reconciliation candidate parent deltas, conflict and
   overlap evidence, coverage, late-fix/manual-resolution/unseen-region flags,
   and the explicit-request flag. Evidence may carry forward only for an
   identical conflict-free delta with no dependency overlap. A changed delta,
   conflict resolution, overlap, late fix, or required audit routes affected
   checks/audit before finalization continues. Missing or malformed evidence
   fails closed. When the evaluator requires an audit, return its brief and
   decision to main; accepted fixes create a replacement candidate and restart
   from the affected verification.
5. Apply the canonical shared-artifact definition (`../next-plan/references/execution-gates.md#canonical-shared-artifacts`) after exact candidate verification and before the summary. Only a listed mutation runs `Wait-AgentToolsQuiescence.ps1`; release leases on `shared-quiescence`, wait, then reconcile again because primary may have advanced. A changed reconciliation result is a replacement candidate and must be verified before proceeding. Run `Show-FinalizeApprovalReview.ps1` last, then return these landing-summary fields to main before any primary mutation:
   - `## Context` — the original problem, its concrete consequences, and the intended outcome.
   - `## What landed` — the final user-visible or workflow-visible outcome, grouped by plan objective, without internal algorithms, command plumbing, or test procedure.
   - Session route: `## Landing` — state `Primary has not advanced.`, then the primary branch, session branch, disposition of every remaining objective stage, and the exact remaining operation.
   - Primary route: `## Primary commit` — state `Primary is still uncommitted.`, then the primary branch, disposition of every remaining objective stage, and the exact remaining operation.
   - Shared-artifact clause — required exactly when step 6b's trigger fires (the landed diff changes any non-Markdown path under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/`): state that landing replaces the canonical AgentTools binaries every linked worktree consumes, and name any changed CLI contract. Disclosure only; omit it on any other landing.

   Main renders the returned summary as the last content before the confirmation question with no tool invocation between them. Because a terminal transcript may scroll the summary away, the confirmation prompt itself restates the essentials so the approval is self-contained: what the change is in one sentence, the changed-file count and kind (code vs docs/plans), and the route branch. On a session route, end with exactly: `Confirm landing this change from <session-branch> onto primary branch <primary-branch>?` On a primary route, end with exactly: `Confirm commit of this change on primary branch <primary-branch>?` Only an explicit affirmative response is primary-mutation sign-off; a decline or non-answer leaves primary unchanged.
6. Only that response permits main to resume the finalization worker. For a
   session route, it invokes `Invoke-FinalizeLanding.ps1` with the exact
   verified candidate and expected old primary ref. For a primary-commit route,
   it resumes `Invoke-FinalizeCandidateCommit.ps1 -Route primary-commit` with
   `-AdvancePrimary` and the exact verified candidate commit/tree; that sidecar
   advances the named primary ref by guarded CAS (with guarded rollback if its
   postcondition fails). Never reconstruct the candidate with `git commit --only`
   from mutable worktree state. If primary advanced after
   confirmation, rebase the verified candidate, repeat the required reconciliation,
   candidate verification, and v2 audit decision, then land without a new user
   response only when the semantic delta is retained. Return for a refreshed
   summary only when a conflict needs user judgment or the session bytes change.

   Post-landing terminal release. Newly tracked Plans require no publication step. For a completed or rejected Plan, terminal preparation already occurred before candidate creation; `Invoke-FinalizeLanding.ps1` requires the receipt to remain `awaiting-landing`, proves the landed primary contains that exact terminal candidate tree, and only then calls `plan release-after-landing`. A `recovery-conflict` discovered before candidate creation blocks as `plan.recovery-conflict` naming the conflicting Plan; resolve it under the terminal preparation conflict rule in `../next-plan/references/execution-gates.md`. If the process dies after primary advances, reinvoke the same sidecar with the verified candidate and current-tip inputs: it rediscovers the deterministic Plan receipt, then proves containment and terminality before idempotent release without replaying the advance. A session that never lands leaves its claim untouched.

   6b — AgentTools promotion (conditional). Required exactly when the landed diff changes any non-Markdown path under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/`. Accept only a `broken-engine-agenttools-candidate/v2` receipt. After landing, promotion reruns certification against the landed commit; unchanged source identities survive a content-preserving rebase, while any membership, bytes, clean-filter blob, receipt, or executable mismatch requires a rebuild. Promotion writes the source stamp from the certification's commit identities. Run:

   ```powershell
   & "$ROOT\.agents\skills\finalize-changes\scripts\Invoke-AgentToolsPromotion.ps1" `
       -PrimaryRoot $PRIMARY -CandidateReceiptPath $CandidateReceipt -CandidateReceiptSha256 $CandidateReceiptSha256 `
       -LandedCommit $LandedCommit -CooperatingSessionOwner $env:BROKEN_ENGINE_SESSION_OWNER
   ```

   The durable session owner holds no standing ledger claim; `-CooperatingSessionOwner` exempts only a same-session in-flight operation claim. When `$env:BROKEN_ENGINE_SESSION_OWNER` is unset after a client restart, take the durable owner from the in-worktree receipt (`Get-AgentWorktreeSessionProvenance`). Run `scripts/Test-AgentToolsPromotionFixtures.ps1` only when this contract changes. The landing always stands after a promotion blocker: certification mismatches require rebuild; `promotion.rolled-back` restored the prior pair; `promotion.receipt-failed` leaves a promoted, verified, stamped pair; `promotion.shared-quiescence` is retryable and never asks for authority; `promotion.rollback-failed` is an authority-required hard stop with the named backup. Non-triggering landings skip promotion.
7. Verify the session worktree is clean, still registered, and contained in primary. Check registration with `Test-FinalizeWorktreeRegistration` from `../../scripts/FinalizeWorkflowCommon.psm1`, which compares canonical Windows path identity — never string-compare `git worktree list` paths (forward slashes) against local paths (backslashes). Retain worktree and branch for user-managed cleanup. Repository success completes only the current stage. If another stage remains active, continue it in this session; if its next action needs approval, present that gate. When every stage is complete or explicitly user-deferred to an unclaimed tracked Plan, end with:

   ```text
   SESSION COMPLETE
   All verified worktree changes have landed on the parent branch, and the landing lock and any Plan claim are released.
   It is safe to close this session tab.
   ```

   followed (when anything was deferred) by a plain-language list of the deferred stages and their tracked Plan paths. Never emit that block for `LEFT UNCOMMITTED`, `COMMITTED`, a blocked landing, a retained claim, a dirty checkout, an active or non-deferred stage, or any result still requiring user action; end with the blocker or next required action.

Stop and report the exact blocker before any operation that would require broader authority, disturb unrelated changes, bypass a failed verification, or violate lock ownership. On any failure, release an exactly-owned landing claim only after every registered worktree proves inspectable and free of in-progress Git markers (the shared `Test-FinalizeAllWorktreesClear` check); otherwise retain and report it. Never hold a clear-worktree lease across an open-ended user wait.

## Output

- `Finalization: LEFT UNCOMMITTED | COMMITTED | LANDED`
- `Objective status: COMPLETE | CONTINUING | DEFERRED TO TRACKED PLAN`
- Checkout, branch, resulting commit; lock status; retained worktree/branch
- Reconcile mode, sign-off status, landed commit
- SmartGit status and manual command when launch failed
- Files/regions touched during finalization (conflict resolutions), or `none`
- Residuals: blocker or `none` (always last)
- For `LANDED` with step 7 fully satisfied, the exact `SESSION COMPLETE` block last.
