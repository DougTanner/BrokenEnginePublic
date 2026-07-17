---
name: finalize-changes
description: >-
  Reconcile, conditionally audit, and finalize verified repository changes:
  commit on primary only when explicitly requested, or rebase and land a
  session worktree under the PC-global landing lock, always with explicit user
  sign-off on the final landing summary before primary history changes.
allowed-tools: [Read, Bash, AskUserQuestion]
---

# Finalize Changes

Finalize only the verified change set. Preserve unrelated user changes, never create or remove a session worktree or branch, and never push. Use this workflow only for a requested commit or primary-branch landing, whose commit, reconciliation rebase, landing lock, parent-branch update, and claim release it exclusively owns.

Approval and confirmation semantics — what authorizes a primary mutation, summary invalidation, and the carried-approval exception — are owned by the [canonical execution-gate contract](../next-plan/references/execution-gates.md) (state 4); do not restate or redefine them.

## Rebase-only history

Keep history linear. Reconciliation runs `git rebase <primary-branch>` from the session worktree; landing advances clean primary to the already-rebased session tip with `git rebase <session-branch>` after proving primary is its ancestor. Never `git merge`, `git pull` without `--rebase`, or `--rebase-merges`; never create a multi-parent commit.

## Inputs

- The one final-tree `/verify-changes` report, its SHA-256, and exact manifest and PASS-ledger ranges, consumed only through `Read-AgentReportSection.ps1`
- Current and primary checkout identities; for a session landing, the live wrapper claim and its five provenance variables
- For a `/next-plan` route, the ledger's completion receipt path and lowercase SHA-256; otherwise the caller-supplied finalization mode
- When a plan-row claim is held: plan identity, owner/session token, and the latest verified WorktreeCli receipt

## Receipt-chain validation (single site)

When finalization follows `/next-plan`, run [`Test-NextPlanReceiptChain.ps1`](../next-plan/scripts/Test-NextPlanReceiptChain.ps1) with the PASS ledger's completion receipt path/SHA-256 — the only receipt-chain validation in the queue run. Require `status: pass` and `nextAction: finalize-changes`; take the receipt identities and `finalizationMode` only from its result and require `session-landing`. A missing chain, non-passing result, or mode mismatch is a blocker. Other routes need no chain and keep their caller-supplied mode.

## Sidecars

Three scripts own the mechanics; never reconstruct their Git, lock, WorktreeCli, or SmartGit commands inline. Each documents its full contract in its comment header and returns one JSON object; require exit `0` and `status: pass` (exit `2` = deterministic blocker). Keep sidecar and Git plumbing output out of the user-visible transcript: capture each sidecar's JSON into a variable and print only its `status`, `code`, and the few decision-relevant fields as one short line — a flooded transcript buries the landing summary the user must read before approving.

- [`scripts/Test-FinalizePreflight.ps1`](scripts/Test-FinalizePreflight.ps1) — canonical read-only identity/Git-state/manifest/WorktreeCli/wrapper-claim preflight. Invoke at `initial`, after reconciliation when bytes or the primary tip changed, `pre-mutation`, and `post-mutation`; after reconciliation use the rebased primary parent as `ManifestComparisonBase`.
- [`scripts/Invoke-FinalizeApprovalPreparation.ps1`](scripts/Invoke-FinalizeApprovalPreparation.ps1) — the single pre-approval mutation and review-tool boundary, invoked exactly once on the final clean session tree; its returned `approvedSession` tip is the only approval and landing candidate. SmartGit `unavailable`/`failed` is non-blocking: surface its message and `manualCommand` in the approval response. `-SkipSmartGit` only on a carried-approval rerun.
- [`scripts/Invoke-FinalizeLanding.ps1`](scripts/Invoke-FinalizeLanding.ps1) — exclusive owner of the post-approval landing transaction (locks, queue locks, ancestry proofs, primary ref advance, landing artifact, row release, and the universal acquired-lock safe-stop rule).
- [`scripts/Invoke-AgentToolsPromotion.ps1`](scripts/Invoke-AgentToolsPromotion.ps1) — guarded AgentTools promotion, invoked only by workflow step 5b after a landing whose manifest requires it; never run it for any other purpose.

## Workflow

1. Validate any required receipt chain, run the `initial` preflight, and consume the complete PASS ledger. Stop if anything blocks or any in-scope item is failed, skipped, blocked, or unverified.
2. **Primary checkout:** leave verified changes uncommitted and stop unless the user explicitly requested a commit. For a requested commit, render the state-4 summary (`## Context`, `## What landed`, `## Primary commit`, stating `Primary has not been committed.`) and ask `Confirm commit of verified manifest <manifest-sha256> on primary branch <primary-branch> at <primary-tip>?`. Only after an explicit affirmative response: claim the landing lock (a held or expired foreign lease may be recovered with `lock recover`, only with explicit user approval; unverifiable records block), refresh the owned lease immediately before each Git command, run `pre-mutation`, `git commit --only -- <verified paths>` preserving unrelated index entries, run `post-mutation` and verify the commit diff exactly matches the verified set, write the landing artifact via [`Write-AgentLandingArtifact.ps1`](../../../.agents/scripts/Write-AgentLandingArtifact.ps1), validate committed primary with `plan order validate`, owner-unclaim any completed-plan row, release the lock, and report `COMMITTED`.
3. **Session worktree:** first bind the verified set to the session branch — inspect staged, unstaged, and untracked state; stop if a path mixes unrelated edits with the session change; stage only new session files; `git commit --only -- <verified paths>` preserving unrelated index entries; and verify the commit diff is byte-identical to the ledger manifest (such a commit does not invalidate the ledger). **Then reconcile automatically before landing approval; stop only when a safety check cannot clear.** When primary has advanced, rebase the session onto it automatically once BOTH safety checks below clear (routine merges, `Order.md` semantic merges, same-plan delete-vs-modify, citation drift, and check-A counter resolutions resolve automatically). Reconciliation mutates only the session worktree; it never advances primary.

   **Safety check A — determinism-counter collision.** A textual merge silently mis-resolves version/compatibility counters, and there is NO runtime backstop for a wrong counter value: a replay/CRC check runs inside ONE build and shares whatever value you pick, so it cannot detect a wrong cross-version number. This must therefore be correct AND complete at merge time. These counters are frequently a SUM of independent summands — `Frame::kiVersion` = a base constant + `engine::kiNavDataVersion` + each collection's `kiVersion` — plus standalone counters `kuiProtocolVersion`, DataPacker `.pack` / `DataHeader::kiVersion` / each `Export*::GetVersion`, and any other determinism/compatibility counter. Key the check on the INDIVIDUAL summand a change owns, NEVER the computed total:
   - If only one side changed a summand, git merged it correctly — leave it (do not re-stack; the total already advanced).
   - If BOTH sides changed the SAME summand from a common base, decide whether the two bumps are INDEPENDENT changes (different accompanying payload) or the SAME logical change (a cherry-pick, or the same plan run in two worktrees). Same change → keep the textual value. Independent → the textual merge under-resolves; set that summand to `common-base + (number of independent bumps)` so each landing owns a distinct value (two independent `X→X+1` bumps ⇒ `X+2`).
   - Never stack a summand only one side touched, and never stack a duplicate of the same change (that over-invalidates saves and can gratuitously break `kuiProtocolVersion` interop).

   **Safety check B — primary-delta vs session-assumption cross-check.** A textually clean rebase can still be semantically broken: primary may have changed a function, data/`.pack`/save layout, symbol, or CRC/SOA invariant that the session's changed files depend on, in a DIFFERENT file — so there is no conflict, it compiles, and the session's own (session-scoped) re-verify never re-examines primary's file. Enumerate primary's delta since the session base (`git diff <session-base>..<primary-tip>`; read the relevant hunks) and cross-check each session dependency against it: APIs/functions the session calls, data/`.pack`/save layouts the session reads or writes, symbols the session references, and CRC/SOA-member/version invariants the session touches. If primary changed any of them in a way the session's code assumes otherwise, the merge is wrong despite compiling.

   **Stop for a reconciliation decision** when either check cannot be cleared confidently; default to stopping under uncertainty. That decision resolves the session tree only — it never authorizes advancing primary — and waiting must not hold a landing lock.

   A passing ledger survives a conflict-free rebase when both checks clear and every session-owned path stays byte-identical to the verified manifest; regenerate the manifest against the rebased primary parent and require exact equality. Otherwise return through only the affected checks, review, hygiene, and `/verify-changes` rows. When a completed-plan claim is held, always run `plan order complete ... --reapply` after the rebase (even when the row is already absent — it binds a receipt to the reconciled queue bytes), resolve a conflicting executable `Order.md` to the complete primary version as a file, run session `plan order validate`, and regenerate verification to carry that receipt. Then one fresh `/session-audit` only for late semantic fixes, reconciliation changes, Tier-3 cross-file integration, or contract-significant regions no reviewer saw.
4. After approval preparation, stage the entire post-approval step before asking for sign-off, so approval triggers exactly one already-validated call:
   - Assemble the complete `Invoke-FinalizeLanding.ps1` argument set (tips, report identity, manifest ranges, session identity, and any plan-row/receipt arguments).
   - Run that exact argument set with `-ValidateOnly` and require `status: validated`. It re-checks every input shape and binding — receipt fields, queue hashes against the session tree, plan-row ownership, report hash, artifact-writer reachability — without claiming any lock or mutating anything. A blocker here is resolved before approval, never after.
   - Write the `broken-engine-objective-ledger/v1` skeleton beneath ignored `Temp/` now, with every stage's `disposition` still pending, so step 6 only flips dispositions and validates.

   Then render the landing summary before any primary mutation — a merge-request-style retrospective, not a checklist or file inventory:
   - `## Context` — the original problem, its concrete consequences, and the intended outcome.
   - `## What landed` — the final user-visible or workflow-visible outcome, grouped by plan objective, without internal algorithms, command plumbing, or test procedure.
   - `## Landing` — state `Primary has not advanced.`, then the primary branch and current tip, session branch and approved session tip, reconciliation disposition, queue-changing status, disposition of every remaining objective stage, and the exact remaining operation.

   Render the summary as the last content before the confirmation question with no tool invocation between them. Because a terminal transcript may scroll the summary away, the confirmation prompt itself restates the essentials so the approval is self-contained: what the change is in one sentence, the changed-file count and kind (code vs queue/docs), the primary/session commit pair, and the queue-changing status. End with exactly: `Confirm landing <approved-session-commit> from <session-branch> onto primary branch <primary-branch>?` Only an explicit affirmative response to that exact summary is landing sign-off; a decline or non-answer leaves primary unchanged. Substitutes, invalidation, and the carried-approval exception follow the canonical contract.
5. Only that response may invoke `Invoke-FinalizeLanding.ps1` with the approval-covered tips and manifest identity — the same argument set `-ValidateOnly` already passed, minus the switch (on a carried-approval rerun whose tips changed, restage and re-run `-ValidateOnly` with the refreshed tips before the call); a landed-but-blocked artifact residual is retried only with `-ArtifactOnly`. `CompletedPlanReceipt` takes the WorktreeCli completion receipt JSON content or a path to a file containing it — for a reconciled session that is the `--reapply` receipt from step 3, the one whose `plansOrderSha256`/`featuresOrderSha256` hash-match the queue files being landed; the `broken-engine-next-plan-completion/v1` sidecar wrapper is not the receipt.

   **5b — AgentTools promotion (conditional).** Required exactly when the landed manifest changes AgentTools sources — any non-Markdown path under `Tools/WorktreeCli/`, `Tools/AgentHarness/`, or `Tools/ToolCommon/`. The session must hold a `broken-engine-agenttools-candidate/v1` receipt from `New-AgentToolsCandidate.ps1` built on the reconciled session commit (rebuild the candidate after reconciliation when the rebase changed anything; `dirtyToolPaths: true` blocks). After the landing transaction succeeds, run:

   ```powershell
   & "$ROOT\.agents\skills\finalize-changes\scripts\Invoke-AgentToolsPromotion.ps1" `
   	-PrimaryRoot $PRIMARY -CandidateReceiptPath $CandidateReceipt -CandidateReceiptSha256 $CandidateReceiptSha256 `
   	-LandedCommit $LandedCommit -CooperatingSessionOwner $env:BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER
   ```

   When the promotion contract itself changes, run [`scripts/Test-AgentToolsPromotionFixtures.ps1`](scripts/Test-AgentToolsPromotionFixtures.ps1) against a scratch repository — never per landing. Exit `0` promoted and re-verified the canonical pair atomically; report the promotion receipt path/SHA-256. Exit `2` is a deterministic blocker whose `code` decides the disposition — the landing always stands: `promotion.rolled-back` restored and re-verified the previous pair (fix the failure, rebuild a candidate, retry); `promotion.receipt-failed` means the pair IS promoted, verified, and stamped and only the receipt file is outstanding (do not rebuild — restore receipt-store access and record the residual); `promotion.coordination-blocked` means another session or held maintenance blocked (retry when clear); the receipt/source validation codes (`promotion.receipt-identity`, `promotion.dirty-candidate`, `promotion.source-mismatch`, `promotion.not-landed`, `promotion.partial-previous`, `promotion.candidate-*`) need a corrected candidate or repaired canonical state. Exit `1` with `promotion.rollback-failed` is a hard stop: canonical state may be partial; report the named backup directory and stop for the user. Landings whose manifest does not require promotion skip 5b entirely and behave exactly as before.
6. Verify the session worktree is clean, still registered, and contained in primary; retain worktree and branch for user-managed cleanup. Repository success completes only the current stage. If another stage remains active, continue it in this session; if its next action needs approval, present that gate. The session is objective-terminal only when every stage is complete or each unfinished stage was explicitly deferred by the user and has a verified receipt in the live WorktreeCli plan queue. Prove it: flip each stage's `disposition` in the `broken-engine-objective-ledger/v1` skeleton staged in step 4 (author it now only if step 4 was skipped, e.g. the primary-commit route) and run [`scripts/Test-FinalizeObjectiveContract.ps1`](scripts/Test-FinalizeObjectiveContract.ps1). The script verifies the prior record hash, exact stage/deliverable sets, and approved deferral identities; require `objectiveTerminal: true` before emitting, as the absolute final output:

   ```text
   SESSION COMPLETE
   All verified worktree changes have landed on the parent branch, and all session claims are released.
   It is safe to close this session tab.
   ```

   Never emit that block for `LEFT UNCOMMITTED`, `COMMITTED`, a blocked landing, a retained claim, a dirty checkout, an active or non-deferred stage, or any result still requiring user action; end with the blocker or next required action.

Stop and report the exact blocker before any operation that would require broader authority, disturb unrelated changes, bypass a failed verification, or violate lock ownership. On any failure, release an exactly-owned landing claim only after every registered worktree proves inspectable and free of in-progress Git markers (the shared `Test-FinalizeAllWorktreesClear` check); otherwise retain and report it. Never hold a clear-worktree lease across an open-ended user wait.

## Output

- `Finalization: LEFT UNCOMMITTED | COMMITTED | LANDED`
- `Objective status: COMPLETE | CONTINUING | DEFERRED TO LIVE QUEUE`
- Checkout, branch, resulting commit; lock status; retained worktree/branch
- Reconcile mode, sign-off status, approved primary/session commit pair
- SmartGit status and manual command when launch failed
- Verification rerun and files/regions touched during finalization, or `none`
- Residuals: blocker or `none` (always last)
- For `LANDED` with step 6 fully satisfied, the exact `SESSION COMPLETE` block last.
