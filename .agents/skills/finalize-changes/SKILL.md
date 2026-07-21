---
name: finalize-changes
description: >-
  Reconcile and finalize verified repository changes:
  commit on primary only when explicitly requested, or rebase and land a
  session worktree under the PC-global landing lock, always with explicit user
  sign-off on the landing summary before primary history changes.
allowed-tools: [Read, Bash, PowerShell, AskUserQuestion]
---

# Finalize Changes

Finalize only the verified change set. Preserve unrelated user changes, never create or remove a session worktree or branch, and never push. Use this workflow only for a requested commit or primary-branch landing, whose commit, reconciliation rebase, landing lock, parent-branch update, and claim release it exclusively owns.

Approval and confirmation semantics — what authorizes a primary mutation and when a refreshed summary is required — are owned by the [canonical execution-gate contract](../next-plan/references/execution-gates.md) (state 4); do not restate or redefine them. The confirmation binds the session diff, not exact tips: a primary advance after confirmation is handled by re-rebasing and landing without returning to the user.

## Rebase-only history

Keep history linear. Reconciliation runs `git rebase <primary-branch>` from the session worktree; landing advances clean primary to the already-rebased session tip with `git rebase <session-branch>` after proving primary is its ancestor. Never `git merge`, `git pull` without `--rebase`, or `--rebase-merges`; never create a multi-parent commit.

## Inputs

- The `/verify-changes` completion summary: every in-scope acceptance check passed
- Current and primary checkout identities; for a session landing, the live wrapper claim and its five provenance variables
- When a plan-row claim is held: plan identity and owner/session token
- An explicit staged-request disposition: `none`, or the complete list of `Temp/` `plan order add` request paths. Approval preparation records each ordinary file's canonical identity, size, and SHA-256; landing receives the same paths and returned hashes. Never infer an empty list or omit a staged request.

## Sidecars

These scripts own the mechanics; never reconstruct their Git, lock, WorktreeCli, or SmartGit commands inline. Each documents its contract and returns one JSON object. `Test-FinalizePreflight.ps1`, `Invoke-FinalizeApprovalPreparation.ps1`, `Test-AgentToolsCandidateCertification.ps1`, and `Invoke-AgentToolsPromotion.ps1` succeed only on exit `0`, `status: pass`, `code: ok`; `Invoke-FinalizeLanding.ps1` succeeds only on exit `0`, `status: landed`, `code: ok`. Exit `2` is a deterministic blocker. `Show-FinalizeApprovalReview.ps1` accepts exit `0` with `opened`, `unavailable`, or `failed`; only its invalid-input exit `1` blocks. Keep plumbing output out of the user transcript: print only `status`, `code`, and decision-relevant fields.

- [`scripts/Test-FinalizePreflight.ps1`](scripts/Test-FinalizePreflight.ps1) — canonical read-only structural preflight (identity, clean Git state, WorktreeCli capability, wrapper claim, and approval-bound staged requests). Approval preparation and landing both invoke it.
- [`scripts/Invoke-FinalizeApprovalPreparation.ps1`](scripts/Invoke-FinalizeApprovalPreparation.ps1) — invoked once per landing on the final clean session tree; squashes the linear session range to one tree-identical commit whose sole parent is the current primary tip. Its returned `approvedSession` tip is the landing candidate. A later primary advance rebases that candidate directly; it does not rerun this script.
- [`scripts/Show-FinalizeApprovalReview.ps1`](scripts/Show-FinalizeApprovalReview.ps1) — sole owner of the SmartGit review-window launch, invoked last in step 4 immediately before the landing summary so the user returns from their review to a finished confirmation question. `unavailable`/`failed` is non-blocking: surface its message and exact `manualCommand` in the approval response. Not re-invoked after a post-confirmation re-rebase.
- [`scripts/Invoke-FinalizeLanding.ps1`](scripts/Invoke-FinalizeLanding.ps1) — exclusive owner of the post-approval landing transaction (structural preflight, locks, ancestry proofs, primary ref advance, post-landing queue publication, row release).
- [`scripts/Test-AgentToolsCandidateCertification.ps1`](scripts/Test-AgentToolsCandidateCertification.ps1) — read-only v2 receipt certification against immutable executables, stable before/after manifests, same-checkout source bytes, and expected-commit clean-filter blobs, including primary-resolved `json.hpp`. Any exit `2` mismatch requires a rebuilt candidate.
- [`scripts/Invoke-AgentToolsPromotion.ps1`](scripts/Invoke-AgentToolsPromotion.ps1) — guarded AgentTools promotion, invoked only by workflow step 5b after a landing whose diff requires it; never run it for any other purpose.

## Fixtures

Fixtures are assertion-driven developer checks, not sidecars. They emit deterministic pass/fail lines and exit `0` only when every assertion passes; any assertion or setup/cleanup failure exits nonzero.

- [`scripts/Test-LandingLockStatusFixtures.ps1`](scripts/Test-LandingLockStatusFixtures.ps1) — deterministic scratch-repository coverage for the public landing-lock response contract; run it against a candidate WorktreeCli whenever that contract changes.
- [`scripts/Test-AgentToolsPromotionFixtures.ps1`](scripts/Test-AgentToolsPromotionFixtures.ps1) — scratch-only v2 certification, coordination, promotion, rollback, source-change, and binary-tamper coverage; never point it at primary outputs.
- [`scripts/Test-FinalizeWorkflowFixtures.ps1`](scripts/Test-FinalizeWorkflowFixtures.ps1) — scratch-only request binding, session landing, and post-advance recovery coverage.

## Workflow

1. Confirm `/verify-changes` reported every in-scope check passed; stop on any failed, blocked, or unverified item.
2. **Primary checkout:** leave verified changes uncommitted and stop unless the user explicitly requested a commit. For a requested commit, render the landing summary (`## Context`, `## What landed`, `## Primary commit`, stating `Primary has not been committed.`) and ask `Confirm commit of this change on primary branch <primary-branch>?`. Only after an explicit affirmative response: claim the landing lock (a held or expired foreign lease may be recovered with `lock recover`, only with explicit user approval; unverifiable records block), run the structural preflight, `git commit -F <message-file> --only -- <verified paths>` preserving unrelated index entries (write the message to a scratch file first; never place `-m` or any flag after `--`, where Git parses it as a pathspec), validate committed primary with `plan order validate`, owner-unclaim any completed-plan row, release the lock, and report `COMMITTED`.
3. **Session worktree:** start from the verified pre-reconcile acceptance matrix. Bind only that verified set to the session branch: inspect staged, unstaged, and untracked state; stop if a path mixes unrelated edits with the session change; stage only new session files; `git commit -F <message-file> --only -- <verified paths>` preserving unrelated index entries. Any remaining unrelated dirty status blocks reconciliation. **Then claim the reconcile lock and reconcile automatically before landing approval.** When primary has advanced, rebase the session onto it. A conflict-free rebase carries the verified evidence forward unchanged. Reconciliation mutates only the session worktree; it never advances primary.

   **Reconcile lock.** Before checking primary or rebasing, claim the PC-global landing lock under step 2's claim and recovery rule, so primary cannot advance while reconciliation, conflict re-verification, a triggered `/session-audit`, and approval preparation run. A foreign lease means another session is mid-landing or mid-reconcile: the claim fails fast, and exit `2` is a retryable state conflict, not an error — poll `lock status` and retry, bounded by the holder's lease expiry, then rebase once onto the settled tip. `claimantPid` is the PID of the one-shot WorktreeCli command and normally exits after producing its result; never use PID existence as session or lease liveness evidence. Authoritative evidence is the command exit/result plus `leaseState`, `heartbeatAt`, and `expiresAt`. Hold the lease only across agent-driven work, refreshing it (`lock refresh --owner`) at each long-stage boundary (conflict re-verification, a triggered audit, approval preparation). Release it before any user-facing wait — a conflict needing user judgment, the SmartGit review window, the landing summary and confirmation — and before step 5, whose `Invoke-FinalizeLanding.ps1` claims its own lease post-approval and would block on a still-held reconcile lease. A session that dies holding the lease blocks landings until expiry; recovery follows step 2's `lock recover` rule (explicit user approval only).

   **Overlap check.** After (or instead of a conflicted) rebase, intersect primary's changed paths since the session base (`git diff --name-only <session-base>..<primary-tip>`) with the session's changed files and the files they directly depend on. An empty intersection with a clean rebase proceeds silently. A non-empty intersection: inspect only the overlapping files for semantic breaks a textual merge hides — an API or symbol the session calls, a data/`.pack`/save layout the session reads or writes, or a CRC/SOA/version invariant the session touches.

   **Determinism counters.** When BOTH sides changed the same version/compatibility summand (`Frame::kiVersion` component summands, `kuiProtocolVersion`, DataPacker `.pack`/`DataHeader::kiVersion`/`Export*::GetVersion`), key the resolution on the individual summand, never the computed total: same logical change → keep the textual value; independent changes → `common-base + (number of independent bumps)`. Never stack a summand only one side touched. When only one side changed a summand, git merged it correctly — leave it.

   **Conflicts.** The queue is machine-local state, never in the tree. Resolve code or plan-file conflicts by hand, identify which acceptance rows the resolved bytes invalidate, and rerun only those rows; unchanged rows retain their evidence. Run `/session-audit` on the reconciled tree when its workflow trigger applies, before approval preparation. Resolve accepted audit findings, then rerun only checks invalidated by those fixes. Stop only when a conflict genuinely needs user judgment; release the reconcile lease first and re-claim on resume.
4. Apply the [canonical shared-artifact definition](../next-plan/references/execution-gates.md#canonical-shared-artifacts) before approval preparation. Only a landing with a listed shared-artifact mutation performs the ledger gate: load `.agents/scripts/WorktreeCliSessionExclusion.psm1` by absolute or explicitly relative path and use its read-only `Get-WorktreeCliExclusionStatus` check. Proceed only when it reports zero live sessions other than the current cooperating owner and no maintenance claim. Never infer this gate from an ordinary tracked path or from code shared by client and server. If either ledger condition fails, release any owned reconcile or landing lease, retain the candidate and claims, tell the user to wait until every other active wrapper session has ended and any maintenance claim has cleared, and do not run approval preparation or present landing confirmation. After the user reports that the sessions have ended, recheck the canonical ledger; their report clears only the safety blocker and does not authorize landing. When the recheck passes, re-claim the reconcile lease, reconcile again if primary advanced during the wait, and resume the existing confirmation flow.

   Before approval preparation, pass the explicit staged-request disposition. When AgentTools promotion is triggered, run `Test-AgentToolsCandidateCertification.ps1` against the reconciled tip; a mismatch blocks approval and requires a rebuild. Run `Invoke-FinalizeApprovalPreparation.ps1` once on the final clean tree and retain its returned request identities/hashes and `approvedSession` tip. Release the reconcile lease here. Run `Show-FinalizeApprovalReview.ps1` last, then render the landing summary before any primary mutation:
   - `## Context` — the original problem, its concrete consequences, and the intended outcome.
   - `## What landed` — the final user-visible or workflow-visible outcome, grouped by plan objective, without internal algorithms, command plumbing, or test procedure.
   - `## Landing` — state `Primary has not advanced.`, then the primary branch, session branch, disposition of every remaining objective stage, and the exact remaining operation.

   Render the summary as the last content before the confirmation question with no tool invocation between them. Because a terminal transcript may scroll the summary away, the confirmation prompt itself restates the essentials so the approval is self-contained: what the change is in one sentence, the changed-file count and kind (code vs docs/plans), and the session branch. End with exactly: `Confirm landing this change from <session-branch> onto primary branch <primary-branch>?` Only an explicit affirmative response is landing sign-off; a decline or non-answer leaves primary unchanged.
5. Only that response may invoke `Invoke-FinalizeLanding.ps1` with the approved candidate and current tips. If primary advanced after confirmation, do not return to the user: rebase the approved candidate onto the new tip (repeating step 3's conflict handling if needed), then invoke the landing with the refreshed tips. A `landing-lock.claim-failed` blocker can now also mean another session's reconcile lease: exit `2` is a retryable state conflict — poll `lock status` and re-invoke when the lease clears. Return for a refreshed summary only when a conflict needs user judgment or the session's own bytes changed.

   **Post-landing queue publication.** Queue rows publish only after primary advances. `Invoke-FinalizeLanding.ps1` completes the finished plan, adds every approval-bound request, validates, and releases the row claim. A session that never lands leaves queue and claim untouched. If the process dies after primary advances, reinvoke the same sidecar with the same paths and hashes: when the session remains at `ApprovedSessionCommit` and primary contains it, post-advance recovery resumes retry-idempotent publication and owner-checked release without replaying the advance.

   **5b — AgentTools promotion (conditional).** Required exactly when the landed diff changes any non-Markdown path under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/`. Accept only a `broken-engine-agenttools-candidate/v2` receipt. After landing, promotion reruns certification against the landed commit; unchanged manifest identities survive a content-preserving rebase, while any membership, bytes, clean-filter blob, receipt, or executable mismatch requires a rebuild. Promotion writes the source stamp from the certification's commit identities. Run:

   ```powershell
   & "$ROOT\.agents\skills\finalize-changes\scripts\Invoke-AgentToolsPromotion.ps1" `
   	-PrimaryRoot $PRIMARY -CandidateReceiptPath $CandidateReceipt -CandidateReceiptSha256 $CandidateReceiptSha256 `
   	-LandedCommit $LandedCommit -CooperatingSessionOwner $env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER
   ```

   Run [`scripts/Test-AgentToolsPromotionFixtures.ps1`](scripts/Test-AgentToolsPromotionFixtures.ps1) only when this contract changes. The landing always stands after a promotion blocker: certification mismatches require rebuild; `promotion.rolled-back` restored the prior pair; `promotion.receipt-failed` leaves a promoted, verified, stamped pair; coordination blockers retry when clear; `promotion.rollback-failed` is a hard stop with the named backup. Non-triggering landings skip promotion.
6. Verify the session worktree is clean, still registered, and contained in primary. Check registration with `Test-FinalizeWorktreeRegistration` from [`FinalizeWorkflowCommon.psm1`](../../scripts/FinalizeWorkflowCommon.psm1), which compares canonical Windows path identity — never string-compare `git worktree list` paths (forward slashes) against local paths (backslashes). Retain worktree and branch for user-managed cleanup. Repository success completes only the current stage. If another stage remains active, continue it in this session; if its next action needs approval, present that gate. When every stage is complete or explicitly user-deferred to the live queue, end with:

   ```text
   SESSION COMPLETE
   All verified worktree changes have landed on the parent branch, and all session claims are released.
   It is safe to close this session tab.
   ```

   followed (when anything was deferred) by a plain-language list of the deferred stages and their queue rows. Never emit that block for `LEFT UNCOMMITTED`, `COMMITTED`, a blocked landing, a retained claim, a dirty checkout, an active or non-deferred stage, or any result still requiring user action; end with the blocker or next required action.

Stop and report the exact blocker before any operation that would require broader authority, disturb unrelated changes, bypass a failed verification, or violate lock ownership. On any failure, release an exactly-owned landing claim only after every registered worktree proves inspectable and free of in-progress Git markers (the shared `Test-FinalizeAllWorktreesClear` check); otherwise retain and report it. Never hold a clear-worktree lease across an open-ended user wait.

## Output

- `Finalization: LEFT UNCOMMITTED | COMMITTED | LANDED`
- `Objective status: COMPLETE | CONTINUING | DEFERRED TO LIVE QUEUE`
- Checkout, branch, resulting commit; lock status; retained worktree/branch
- Reconcile mode, sign-off status, landed commit
- SmartGit status and manual command when launch failed
- Files/regions touched during finalization (conflict resolutions), or `none`
- Residuals: blocker or `none` (always last)
- For `LANDED` with step 6 fully satisfied, the exact `SESSION COMPLETE` block last.
