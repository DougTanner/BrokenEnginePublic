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
- Any staged `plan order add` request paths from `/create-follow-up-plans` or `/save-plan`, passed to `Invoke-FinalizeLanding.ps1 -PlanAddRequestPaths` for post-landing publication; omit them and the follow-up rows silently never publish

## Sidecars

These scripts own the mechanics; never reconstruct their Git, lock, WorktreeCli, or SmartGit commands inline. Each documents its full contract in its comment header and returns one JSON object; require exit `0` and `status: pass` (exit `2` = deterministic blocker) — except `Show-FinalizeApprovalReview.ps1`, the sole non-`status: pass` sidecar, whose every launch outcome is acceptable and never gates a landing. Keep sidecar and Git plumbing output out of the user-visible transcript: capture each sidecar's JSON into a variable and print only its `status`, `code`, and the few decision-relevant fields as one short line — a flooded transcript buries the landing summary the user must read before approving.

- [`scripts/Test-FinalizePreflight.ps1`](scripts/Test-FinalizePreflight.ps1) — canonical read-only structural preflight (identity, Git state, WorktreeCli capability, wrapper claim). Runs once per landing, invoked by `Invoke-FinalizeLanding.ps1` at its pre-mutation checkpoint; the primary-commit route invokes it directly before committing.
- [`scripts/Invoke-FinalizeApprovalPreparation.ps1`](scripts/Invoke-FinalizeApprovalPreparation.ps1) — invoked once per landing on the final clean session tree; squashes the linear session range to one tree-identical commit whose sole parent is the current primary tip. Its returned `approvedSession` tip is the landing candidate. A later primary advance rebases that candidate directly; it does not rerun this script.
- [`scripts/Show-FinalizeApprovalReview.ps1`](scripts/Show-FinalizeApprovalReview.ps1) — sole owner of the SmartGit review-window launch, invoked last in step 4 immediately before the landing summary so the user returns from their review to a finished confirmation question. `unavailable`/`failed` is non-blocking: surface its message and exact `manualCommand` in the approval response. Not re-invoked after a post-confirmation re-rebase.
- [`scripts/Invoke-FinalizeLanding.ps1`](scripts/Invoke-FinalizeLanding.ps1) — exclusive owner of the post-approval landing transaction (structural preflight, locks, ancestry proofs, primary ref advance, post-landing queue publication, row release).
- [`scripts/Invoke-AgentToolsPromotion.ps1`](scripts/Invoke-AgentToolsPromotion.ps1) — guarded AgentTools promotion, invoked only by workflow step 5b after a landing whose diff requires it; never run it for any other purpose.

## Workflow

1. Confirm `/verify-changes` reported every in-scope check passed; stop on any failed, blocked, or unverified item.
2. **Primary checkout:** leave verified changes uncommitted and stop unless the user explicitly requested a commit. For a requested commit, render the landing summary (`## Context`, `## What landed`, `## Primary commit`, stating `Primary has not been committed.`) and ask `Confirm commit of this change on primary branch <primary-branch>?`. Only after an explicit affirmative response: claim the landing lock (a held or expired foreign lease may be recovered with `lock recover`, only with explicit user approval; unverifiable records block), run the structural preflight, `git commit -F <message-file> --only -- <verified paths>` preserving unrelated index entries (write the message to a scratch file first; never place `-m` or any flag after `--`, where Git parses it as a pathspec), validate committed primary with `plan order validate`, owner-unclaim any completed-plan row, release the lock, and report `COMMITTED`.
3. **Session worktree:** first bind the verified set to the session branch — inspect staged, unstaged, and untracked state; stop if a path mixes unrelated edits with the session change; stage only new session files; `git commit -F <message-file> --only -- <verified paths>` preserving unrelated index entries (same message-file rule: no flags after `--`). **Then claim the reconcile lock and reconcile automatically before landing approval.** When primary has advanced, rebase the session onto it. A conflict-free rebase of the verified commit is sufficient — do not regenerate verification, re-run reviews, or re-prove content that git already carried across the rebase. Reconciliation mutates only the session worktree; it never advances primary.

   **Reconcile lock.** Before checking primary or rebasing, claim the PC-global landing lock under step 2's claim and recovery rule, so primary cannot advance while reconciliation, conflict re-verification, a triggered `/session-audit`, and approval preparation run. A foreign lease means another session is mid-landing or mid-reconcile: the claim fails fast, and exit `2` is a retryable state conflict, not an error — poll `lock status` and retry, bounded by the holder's lease expiry, then rebase once onto the settled tip. Hold the lease only across agent-driven work, refreshing it (`lock refresh --owner`) at each long-stage boundary (conflict re-verification, a triggered audit, approval preparation). Release it before any user-facing wait — a conflict needing user judgment, the SmartGit review window, the landing summary and confirmation — and before step 5, whose `Invoke-FinalizeLanding.ps1` claims its own lease post-approval and would block on a still-held reconcile lease. A session that dies holding the lease blocks landings until expiry; recovery follows step 2's `lock recover` rule (explicit user approval only).

   **Overlap check.** After (or instead of a conflicted) rebase, intersect primary's changed paths since the session base (`git diff --name-only <session-base>..<primary-tip>`) with the session's changed files and the files they directly depend on. An empty intersection with a clean rebase proceeds silently. A non-empty intersection: inspect only the overlapping files for semantic breaks a textual merge hides — an API or symbol the session calls, a data/`.pack`/save layout the session reads or writes, or a CRC/SOA/version invariant the session touches.

   **Determinism counters.** When BOTH sides changed the same version/compatibility summand (`Frame::kiVersion` component summands, `kuiProtocolVersion`, DataPacker `.pack`/`DataHeader::kiVersion`/`Export*::GetVersion`), key the resolution on the individual summand, never the computed total: same logical change → keep the textual value; independent changes → `common-base + (number of independent bumps)`. Never stack a summand only one side touched. When only one side changed a summand, git merged it correctly — leave it.

   **Conflicts.** The queue is machine-local state, never in the tree, so `Order.md` can no longer appear in a rebase conflict. Resolve a code or plan-file conflict by hand, recompile the affected target(s), and re-review only the conflicted hunks; run one `/session-audit` only when those manual edits are semantic (late fixes, cross-file integration no reviewer saw). Stop for a reconciliation decision only when a conflict genuinely needs user judgment; release the reconcile lease first and re-claim on resume — that decision resolves the session tree only and waiting must not hold a landing lock.
4. Run `Invoke-FinalizeApprovalPreparation.ps1` once on the final clean tree; its returned `approvedSession` tip is the landing candidate. Release the reconcile lease here — nothing past this point may hold the landing lock until `Invoke-FinalizeLanding.ps1` claims its own. Run `Show-FinalizeApprovalReview.ps1` last, so the very next thing the user sees is the finished question. Then render the landing summary before any primary mutation — a merge-request-style retrospective, not a checklist or file inventory:
   - `## Context` — the original problem, its concrete consequences, and the intended outcome.
   - `## What landed` — the final user-visible or workflow-visible outcome, grouped by plan objective, without internal algorithms, command plumbing, or test procedure.
   - `## Landing` — state `Primary has not advanced.`, then the primary branch, session branch, disposition of every remaining objective stage, and the exact remaining operation.

   Render the summary as the last content before the confirmation question with no tool invocation between them. Because a terminal transcript may scroll the summary away, the confirmation prompt itself restates the essentials so the approval is self-contained: what the change is in one sentence, the changed-file count and kind (code vs docs/plans), and the session branch. End with exactly: `Confirm landing this change from <session-branch> onto primary branch <primary-branch>?` Only an explicit affirmative response is landing sign-off; a decline or non-answer leaves primary unchanged.
5. Only that response may invoke `Invoke-FinalizeLanding.ps1` with the approved candidate and current tips. If primary advanced after confirmation, do not return to the user: rebase the approved candidate onto the new tip (repeating step 3's conflict handling if needed), then invoke the landing with the refreshed tips. A `landing-lock.claim-failed` blocker can now also mean another session's reconcile lease: exit `2` is a retryable state conflict — poll `lock status` and re-invoke when the lease clears. Return for a refreshed summary only when a conflict needs user judgment or the session's own bytes changed.

   **Post-landing queue publication.** Queue rows publish only after primary advances, so a row can never reference a plan file that is not yet landed. After the primary ref moves, `Invoke-FinalizeLanding.ps1` publishes the session's queue mutations against the machine-local queue: `plan order complete` for the finished plan (its file was already removed with `git rm` in the session, so the row-removal tolerates the absent file) and `plan order add` for any staged follow-up requests passed via `-PlanAddRequestPaths`, then validates and releases the row claim. A session that never lands leaves primary and the queue untouched — the row and claim simply persist. Crash recovery: if the process dies between the primary advance and queue publication, rerun `plan order complete` — its `--plan` must be the canonical `Documents/...` path, not the order-relative row key the `plan row` commands use — and any staged `plan order add` requests (both retry-idempotent) directly under the retained owner claim, then release it — standalone and retryable, needing no landing-script rerun.

   **5b — AgentTools promotion (conditional).** Required exactly when the landed diff changes AgentTools sources — any non-Markdown path under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/`. The session must hold a `broken-engine-agenttools-candidate/v1` receipt from `New-AgentToolsCandidate.ps1` with `dirtyToolPaths: false` whose three `toolTreeHashes` match the landed commit's tool trees. Tree hashes survive a rebase that leaves tool bytes unchanged, so a candidate built before a reconcile or post-confirmation re-rebase usually remains valid — rebuild only when the landed tool trees actually differ from the receipt. After the landing transaction succeeds, run:

   ```powershell
   & "$ROOT\.agents\skills\finalize-changes\scripts\Invoke-AgentToolsPromotion.ps1" `
   	-PrimaryRoot $PRIMARY -CandidateReceiptPath $CandidateReceipt -CandidateReceiptSha256 $CandidateReceiptSha256 `
   	-LandedCommit $LandedCommit -CooperatingSessionOwner $env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER
   ```

   When the promotion contract itself changes, run [`scripts/Test-AgentToolsPromotionFixtures.ps1`](scripts/Test-AgentToolsPromotionFixtures.ps1) against a scratch repository — never per landing. Exit `0` promoted and re-verified the canonical pair atomically; report the promotion receipt path/SHA-256. Exit `2` is a deterministic blocker whose `code` decides the disposition — the landing always stands: `promotion.rolled-back` restored and re-verified the previous pair (fix the failure, rebuild a candidate, retry); `promotion.receipt-failed` means the pair IS promoted, verified, and stamped and only the receipt file is outstanding (do not rebuild — restore receipt-store access and record the residual); `promotion.coordination-blocked` means another session or held maintenance blocked (retry when clear); the receipt/source validation codes (`promotion.receipt-identity`, `promotion.dirty-candidate`, `promotion.source-mismatch`, `promotion.not-landed`, `promotion.partial-previous`, `promotion.candidate-*`) need a corrected candidate or repaired canonical state. Exit `1` with `promotion.rollback-failed` is a hard stop: canonical state may be partial; report the named backup directory and stop for the user. Landings whose diff does not require promotion skip 5b entirely.
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
