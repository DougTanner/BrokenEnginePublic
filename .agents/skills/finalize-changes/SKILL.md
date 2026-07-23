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

Finalize only the verified change set. Preserve unrelated user changes, never create or remove a session worktree or branch, and never push. Use this workflow only for a requested commit or primary-branch landing, whose commit, reconciliation rebase, landing lock, parent-branch update, and terminal Plan claim release it exclusively owns.

Approval and confirmation semantics — what authorizes a primary mutation and when a refreshed summary is required — are owned by the [canonical execution-gate contract](../next-plan/references/execution-gates.md) (state 4); do not restate or redefine them. The confirmation binds the session diff, not exact tips: a primary advance after confirmation is handled by re-rebasing and landing without returning to the user.

## Rebase-only history

Keep history linear. Reconciliation runs `git rebase <primary-branch>` from the session worktree; landing advances clean primary to the already-rebased session tip with `git rebase <session-branch>` after proving primary is its ancestor. Never `git merge`, `git pull` without `--rebase`, or `--rebase-merges`; never create a multi-parent commit.

## Inputs

- The `/verify-changes` completion summary: every in-scope acceptance check passed
- Current and primary checkout identities; for a session landing, the live wrapper claim and its five provenance variables
- When a terminal Plan claim is held: Plan identity, receipt path and SHA-256,
  and approved completion or rejection disposition. Supply no Plan receipt for
  ordinary no-claim work or a deferral already released through `plan unclaim`.

## Sidecars

These scripts own the mechanics; never reconstruct their Git, lock, WorktreeCli, or SmartGit commands inline. Each documents its contract and returns one JSON object. `Test-FinalizePreflight.ps1`, `Invoke-FinalizeApprovalPreparation.ps1`, `Test-AgentToolsCandidateCertification.ps1`, and `Invoke-AgentToolsPromotion.ps1` succeed only on exit `0`, `status: pass`, `code: ok`; `Invoke-FinalizeLanding.ps1` succeeds only on exit `0`, `status: landed`, `code: ok`. Exit `2` is a deterministic blocker. `Show-FinalizeApprovalReview.ps1` accepts exit `0` with `opened`, `unavailable`, or `failed`; only its invalid-input exit `1` blocks. Coordination results carry top-level `disposition`, `requiresUserAuthority`, and retry timing: `retryable-wait` and `shared-quiescence` never require external repair/decision authority; `authority-required` requires it; input/internal failures are `terminal`. Keep plumbing output out of the user transcript: print only `status`, `code`, and decision-relevant fields.

- [`scripts/Test-FinalizePreflight.ps1`](scripts/Test-FinalizePreflight.ps1) — canonical read-only structural preflight (identity, clean Git state, WorktreeCli capability, wrapper claim, and optional terminal Plan receipt). Approval preparation and landing both invoke it. During the one-shot scheduler cutover, an approval-bound v2 candidate receipt may supply the current WorktreeCli when the canonical executable lacks required scheduler capabilities; certification binds that executable to the approved source. All other paths remain canonical and strict.
- [`scripts/Invoke-FinalizeApprovalPreparation.ps1`](scripts/Invoke-FinalizeApprovalPreparation.ps1) — invoked once per landing on the final clean session tree; squashes the linear session range to one tree-identical commit whose sole parent is the current primary tip. Its returned `approvedSession` tip is the landing candidate. A later primary advance rebases that candidate directly; it does not rerun this script.
- [`scripts/Invoke-FinalizeLockClaim.ps1`](scripts/Invoke-FinalizeLockClaim.ps1) — bounded pre-confirmation reconcile-lease claim. It exposes the common claim/status/exact-expiry/recover policy also used by post-confirmation landing: live contention is retryable, exactly validated expired metadata recovers automatically only through WorktreeCli's owner-CAS/all-worktrees-clear primitive, and unverifiable metadata requires external repair/decision authority and is never overridden or bypassed.
- [`scripts/Wait-AgentToolsQuiescence.ps1`](scripts/Wait-AgentToolsQuiescence.ps1) — required bounded read-only shared-artifact gate. Invoke it only for a canonical shared-artifact mutation:

  ```powershell
  & "$PRIMARY\.agents\skills\finalize-changes\scripts\Wait-AgentToolsQuiescence.ps1" `
      -RepositoryRoot $PRIMARY `
      -CooperatingSessionOwner $env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER `
      -WaitSeconds 55
  ```

  It emits one `broken-engine-shared-quiescence/v1` JSON result. Exit `0` reports `quiescent` or `shared-quiescence`, always with `requiresUserAuthority:false`, `retryAfterSeconds`, `waitedMilliseconds`, and `liveBlockers`; an unreadable, malformed, or invalid coordination ledger exits `2` as `authority-required` with `requiresUserAuthority:true`. Other input/internal failures exit `1` as `terminal` with no-authority. Reinvoke only after `retryAfterSeconds` when it reports `shared-quiescence`.
- [`scripts/Show-FinalizeApprovalReview.ps1`](scripts/Show-FinalizeApprovalReview.ps1) — sole owner of the SmartGit review-window launch, invoked last in step 4 immediately before the landing summary so the user returns from their review to a finished confirmation question. `unavailable`/`failed` is non-blocking: surface its message and exact `manualCommand` in the approval response. Not re-invoked after a post-confirmation re-rebase.
- [`scripts/Invoke-FinalizeLanding.ps1`](scripts/Invoke-FinalizeLanding.ps1) — exclusive owner of the post-approval landing transaction (structural preflight, Plan validation, locks, ancestry proofs, primary ref advance, and receipt-bound terminal claim release).
- [`scripts/Test-AgentToolsCandidateCertification.ps1`](scripts/Test-AgentToolsCandidateCertification.ps1) — read-only v2 receipt certification against immutable executables, stable before/after manifests, same-checkout source bytes, and expected-commit clean-filter blobs, including primary-resolved `json.hpp`. Any exit `2` mismatch requires a rebuilt candidate.
- [`scripts/Invoke-AgentToolsPromotion.ps1`](scripts/Invoke-AgentToolsPromotion.ps1) — guarded AgentTools promotion, invoked only by workflow step 5b after a landing whose diff requires it; never run it for any other purpose.

## Terminal Plan claims

Carry the exact receipt path, SHA-256, and approved completion or rejection
disposition through preflight, approval preparation, and landing. The receipt
must remain an ordinary file beneath session `Temp`; changed, missing, reparse,
foreign, or disposition-mismatched receipts block before primary mutation.

Terminal preparation writes a recoverable manifest, deletes the selected Plan,
and removes only direct child dependency edges. A recovery that changes child
metadata after landing confirmation invalidates that confirmation; revalidate
and present the refreshed candidate. Release requires the actual primary tip to
contain the terminal state. Never recreate a terminal claim after primary
advances.

## Fixtures

Fixtures are assertion-driven developer checks, not sidecars. They emit deterministic pass/fail lines and exit `0` only when every assertion passes; any assertion or setup/cleanup failure exits nonzero.

- [`scripts/Test-LandingLockStatusFixtures.ps1`](scripts/Test-LandingLockStatusFixtures.ps1) — deterministic scratch-repository coverage for the public landing-lock response contract; run it against a candidate WorktreeCli whenever that contract changes.
- [`scripts/Test-AgentToolsPromotionFixtures.ps1`](scripts/Test-AgentToolsPromotionFixtures.ps1) — first checks both supplied executables against the current capability contract, then runs scratch-only v2 certification, coordination, promotion, rollback, source-change, and binary-tamper coverage; never point it at primary outputs.
- [`scripts/Test-FinalizeWorkflowFixtures.ps1`](scripts/Test-FinalizeWorkflowFixtures.ps1) — scratch-only receipt binding, shared reconcile/landing claim behavior, terminal Plan preparation and release, session landing, and post-advance recovery coverage.

## Workflow

1. Confirm `/verify-changes` reported every in-scope check passed; stop on any failed, blocked, or unverified item.
2. **Primary checkout:** leave verified changes uncommitted and stop unless the user explicitly requested a commit. For a requested commit, render the landing summary (`## Context`, `## What landed`, `## Primary commit`, stating `Primary has not been committed.`) and ask `Confirm commit of this change on primary branch <primary-branch>?`. Only after an explicit affirmative response: claim the landing lock under the common typed policy — live foreign contention is retryable, exactly validated expired metadata recovers automatically through WorktreeCli's owner-CAS/all-worktrees-clear primitive, and only unverifiable metadata requires external repair/decision authority and is never overridden or bypassed — run the structural preflight, `git commit -F <message-file> --only -- <verified paths>` preserving unrelated index entries (write the message to a scratch file first; never place `-m` or any flag after `--`, where Git parses it as a pathspec), validate committed primary with `plan validate --baseline`, release any receipt-bound terminal claim only after the committed terminal state is proven, release the lock, and report `COMMITTED`.
3. **Session worktree:** start from the verified pre-reconcile acceptance matrix. Bind only that verified set to the session branch: inspect staged, unstaged, and untracked state; stop if a path mixes unrelated edits with the session change; stage only new session files; `git commit -F <message-file> --only -- <verified paths>` preserving unrelated index entries. Any remaining unrelated dirty status blocks reconciliation. **Then claim the reconcile lock and reconcile automatically before landing approval.** When primary has advanced, rebase the session onto it. A conflict-free rebase carries the verified evidence forward unchanged. Reconciliation mutates only the session worktree; it never advances primary.

   **Reconcile lock.** Before checking primary or rebasing, invoke `Invoke-FinalizeLockClaim.ps1` so primary cannot advance while reconciliation, conflict re-verification, a triggered `/session-audit`, and approval preparation run. Its common policy is also used by `Invoke-FinalizeLanding.ps1`: it polls a foreign live lease for at most 55 seconds, then returns `retryable-wait` with `requiresUserAuthority:false`; it recovers only exactly validated expired metadata through WorktreeCli's owner-CAS/all-worktrees-clear primitive; unverifiable metadata returns `authority-required` and requires external repair/decision authority, never an override or bypass. `claimantPid` is the PID of the one-shot WorktreeCli command and normally exits after producing its result; never use PID existence as session or lease liveness evidence. Authoritative evidence is the command exit/result plus `leaseState`, `heartbeatAt`, and `expiresAt`. Hold the lease only across agent-driven work, refreshing it (`lock refresh --owner`) at each long-stage boundary (conflict re-verification, a triggered audit, approval preparation). Release it before any user-facing wait — a conflict needing user judgment, the SmartGit review window, the landing summary and confirmation — and before step 5, whose `Invoke-FinalizeLanding.ps1` claims its own lease post-approval and would block on a still-held reconcile lease.

   **Overlap check.** After (or instead of a conflicted) rebase, intersect primary's changed paths since the session base (`git diff --name-only <session-base>..<primary-tip>`) with the session's changed files and the files they directly depend on. An empty intersection with a clean rebase proceeds silently. A non-empty intersection: inspect only the overlapping files for semantic breaks a textual merge hides — an API or symbol the session calls, a data/`.pack`/save layout the session reads or writes, or a CRC/SOA/version invariant the session touches.

   **Determinism counters.** When BOTH sides changed the same version/compatibility summand (`Frame::kiVersion` component summands, `kuiProtocolVersion`, DataPacker `.pack`/`DataHeader::kiVersion`/`Export*::GetVersion`), key the resolution on the individual summand, never the computed total: same logical change → keep the textual value; independent changes → `common-base + (number of independent bumps)`. Never stack a summand only one side touched. When only one side changed a summand, git merged it correctly — leave it.

   **Conflicts.** Plan metadata is tracked in Git; claim files are machine-local state and never participate in a rebase. Resolve code or Plan-file conflicts by hand, identify which acceptance rows the resolved bytes invalidate, and rerun only those rows; unchanged rows retain their evidence. Run `/session-audit` on the reconciled tree when its workflow trigger applies, before approval preparation. Resolve accepted audit findings, then rerun only checks invalidated by those fixes. Stop only when a conflict genuinely needs user judgment; release the reconcile lease first and re-claim on resume.
4. Apply the [canonical shared-artifact definition](../next-plan/references/execution-gates.md#canonical-shared-artifacts) before approval preparation. Only a landing with a listed shared-artifact mutation performs the ledger gate. Invoke [`scripts/Wait-AgentToolsQuiescence.ps1`](scripts/Wait-AgentToolsQuiescence.ps1) exactly as specified above, excluding the cooperating owner. On `shared-quiescence`, release any owned reconcile or landing lease and reinvoke after its `retryAfterSeconds` until `quiescent`; on `terminal`, stop. Then re-claim the reconcile lease and reconcile again because primary may have advanced. Never infer this gate from an ordinary tracked path or from code shared by client and server.

   Before approval preparation, carry the exact terminal Plan receipt path, SHA-256, and disposition when present; omit all receipt arguments for ordinary no-claim work. When AgentTools promotion is triggered, run `Test-AgentToolsCandidateCertification.ps1` against the reconciled tip; a mismatch blocks approval and requires a rebuild. If the canonical executable lacks the required scheduler capability during the one-shot cutover, pass the exact candidate receipt path and SHA-256 to `Invoke-FinalizeApprovalPreparation.ps1`; its returned `candidateBootstrap` identity is approval-bound and the same pair must be passed to `Invoke-FinalizeLanding.ps1`. Never supply an executable path directly. Run `Invoke-FinalizeApprovalPreparation.ps1` once on the final clean tree and retain its returned `approvedSession` tip and receipt identities. Release the reconcile lease here. Run `Show-FinalizeApprovalReview.ps1` last, then render the landing summary before any primary mutation:
   - `## Context` — the original problem, its concrete consequences, and the intended outcome.
   - `## What landed` — the final user-visible or workflow-visible outcome, grouped by plan objective, without internal algorithms, command plumbing, or test procedure.
   - `## Landing` — state `Primary has not advanced.`, then the primary branch, session branch, disposition of every remaining objective stage, and the exact remaining operation.

   Render the summary as the last content before the confirmation question with no tool invocation between them. Because a terminal transcript may scroll the summary away, the confirmation prompt itself restates the essentials so the approval is self-contained: what the change is in one sentence, the changed-file count and kind (code vs docs/plans), and the session branch. End with exactly: `Confirm landing this change from <session-branch> onto primary branch <primary-branch>?` Only an explicit affirmative response is landing sign-off; a decline or non-answer leaves primary unchanged.
5. Only that response may invoke `Invoke-FinalizeLanding.ps1` with the approved candidate, current tips, and—when approval bound it—the exact candidate receipt path/SHA-256. If primary advanced after confirmation, do not return to the user: rebase the approved candidate onto the new tip (repeating step 3's conflict handling if needed), then invoke the landing with the refreshed tips. A `landing-lock.claim-failed` blocker can now also mean another session's reconcile lease: exit `2` is a retryable state conflict — poll `lock status` and re-invoke when the lease clears. Return for a refreshed summary only when a conflict needs user judgment or the session's own bytes changed.

   **Post-landing terminal release.** Newly tracked Plans require no publication step. For a completed or rejected Plan, `Invoke-FinalizeLanding.ps1` reruns receipt-bound terminal preparation before mutation, requires `awaiting-landing`, validates the reconciled Plan tree, and calls `plan release-after-landing` only after primary contains the terminal state. If the process dies after primary advances, reinvoke the same sidecar with the same receipt identity: post-advance recovery proves containment and terminality before idempotent release without replaying the advance. A session that never lands leaves its claim untouched.

   **5b — AgentTools promotion (conditional).** Required exactly when the landed diff changes any non-Markdown path under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/`. Accept only a `broken-engine-agenttools-candidate/v2` receipt. After landing, promotion reruns certification against the landed commit; unchanged manifest identities survive a content-preserving rebase, while any membership, bytes, clean-filter blob, receipt, or executable mismatch requires a rebuild. Promotion writes the source stamp from the certification's commit identities. Run:

   ```powershell
   & "$ROOT\.agents\skills\finalize-changes\scripts\Invoke-AgentToolsPromotion.ps1" `
       -PrimaryRoot $PRIMARY -CandidateReceiptPath $CandidateReceipt -CandidateReceiptSha256 $CandidateReceiptSha256 `
       -LandedCommit $LandedCommit -CooperatingSessionOwner $env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER
   ```

   Run [`scripts/Test-AgentToolsPromotionFixtures.ps1`](scripts/Test-AgentToolsPromotionFixtures.ps1) only when this contract changes. The landing always stands after a promotion blocker: certification mismatches require rebuild; `promotion.rolled-back` restored the prior pair; `promotion.receipt-failed` leaves a promoted, verified, stamped pair; `promotion.shared-quiescence` is retryable and never asks for authority; `promotion.rollback-failed` is an authority-required hard stop with the named backup. Non-triggering landings skip promotion.
6. Verify the session worktree is clean, still registered, and contained in primary. Check registration with `Test-FinalizeWorktreeRegistration` from [`FinalizeWorkflowCommon.psm1`](../../scripts/FinalizeWorkflowCommon.psm1), which compares canonical Windows path identity — never string-compare `git worktree list` paths (forward slashes) against local paths (backslashes). Retain worktree and branch for user-managed cleanup. Repository success completes only the current stage. If another stage remains active, continue it in this session; if its next action needs approval, present that gate. When every stage is complete or explicitly user-deferred to an unclaimed tracked Plan, end with:

   ```text
   SESSION COMPLETE
   All verified worktree changes have landed on the parent branch, and all session claims are released.
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
- For `LANDED` with step 6 fully satisfied, the exact `SESSION COMPLETE` block last.
