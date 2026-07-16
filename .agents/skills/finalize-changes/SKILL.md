---
name: finalize-changes
description: >-
  Reconcile, conditionally audit, and finalize verified repository changes in
  the final C++ Code Change Process stage. Use only after the final acceptance
  ledger proves every testable change, then
  leave primary checkout edits uncommitted or safely commit, reconcile, and
  land an existing session worktree under the PC-global landing lock. Rebase
  onto the primary branch automatically when it has advanced, resolve safe
  conflicts and version-counter collisions, and reverify when reconciliation
  changes session content or invalidates a verified assumption.
  Always present the final landing summary and require explicit user sign-off
  before advancing the primary branch. Retain the worktree and branch for
  user-managed cleanup. Report a safe-to-close session only when the complete
  user objective, not merely the current repository stage, is terminal.
allowed-tools: [Read, Bash, AskUserQuestion]
---

# Finalize Changes

Finalize only the verified change set. Preserve unrelated user changes, never create a worktree at this late stage, and never remove a session worktree or branch.

Use this workflow only for a requested commit or primary-branch landing. Ordinary Tier 1 and Tier 2 changes may remain uncommitted in the user-supplied checkout after their proportionate checks. For a requested landing, this skill exclusively owns the final commit, reconciliation rebase, landing lock, parent-branch update, claim release, and retained-worktree checks. Never push.

## Primary-mutation confirmation timing

Follow the [canonical next-plan execution-gate
contract](../next-plan/references/execution-gates.md). A user request such as
“commit it” or “land it” authorizes preparation—preflight, reconciliation,
required completion reapply, and affected reverification—but never the primary
history mutation. For either `primary-commit` or `session-landing`, ask exactly
one confirmation question only after the verified manifest, primary identity,
intended mutation, and visible summary are current. A changed approval-bound
value invalidates the response and requires a refreshed summary.

## Rebase-only history

Keep repository history linear. Reconciliation means running `git rebase <primary-branch>` from the session worktree so verified session commits are replayed onto current primary; resolve conflicts with the normal rebase continue/abort flow. Landing means advancing clean primary to that already-rebased session tip with `git rebase <session-branch>` after proving primary is its ancestor. The landing graph operation is a fast-forward, but the command remains `git rebase`.

Never run `git merge`, `git merge --ff-only`, `git pull` without `--rebase`, or `git rebase --rebase-merges`. Never create or preserve a multi-parent commit. Read-only ancestry inspection with `git merge-base` is not history integration and remains allowed.

## Inputs

- The one final-tree `/verify-changes` report under `%LOCALAPPDATA%\BrokenEngine\AgentReports\<repository-hash>`, its SHA-256, and exact ranges for the authoritative changed-file manifest and PASS acceptance ledger; consume only those ranges through `Read-AgentReportSection.ps1` before landing
- Manager execution-control record bound to the fixed process baseline: the
  complete user objective, every approved stage and deliverable with its
  disposition, approved risk tier/triggers, role dispositions, and acceptance
  matrix
- The execution-control record's pre-implementation path and lowercase SHA-256
  carried unchanged in the final verification PASS ledger
- For a session landing, the live wrapper-held WorktreeCli session claim and its five immutable provenance variables; primary-commit mode requires neither a wrapper claim nor a session worktree
- Current checkout path, branch, and commit
- Intended primary checkout path and branch plus session-start/base commit; stop before worktree landing if any is unavailable or ambiguous
- User request for a commit or landing, if any
- When the caller owns a selected plan row: normalized plan identity, row-claim owner/session token, claim receipt, and the latest verified WorktreeCli add/update/complete receipt applicable to the final tree
- For a `/next-plan` route, the immutable next-plan completion receipt path and
  lowercase SHA-256 recorded in the PASS acceptance ledger. Its validated
  chain, not a caller-selected value, supplies and must report the
  `session-landing` finalization mode.
- For a non-`/next-plan` route, caller-supplied finalization mode
  (`session-landing` or explicitly requested `primary-commit`); for every route,
  checkpoint, explicit manifest comparison base, and current/primary tips
  recorded by the preceding successful preflight
- One capability profile derived from the verified run before `initial`: whether any plan-row claim exists, whether it is a completed-plan claim requiring completion reapply or primary-commit release, and whether the approved manifest makes this a queue-changing session landing

## Workflow

Run shell/Git operations locally through Claude's Bash/PowerShell tools or Codex's shell tool; preserve the same commands, checks, and ownership rules. Request final landing sign-off through Claude `AskUserQuestion`, Codex `request_user_input`, or a direct blocking question when that UI is unavailable. The outcome is identical: no primary advancement without an explicit affirmative response to the exact current landing summary.

When finalization follows `/next-plan`, independently run
[`../next-plan/scripts/Test-NextPlanReceiptChain.ps1`](../next-plan/scripts/Test-NextPlanReceiptChain.ps1)
with `-CompletionReceiptPath` and `-CompletionReceiptSha256` from the PASS ledger. Require exit
`0`, `broken-engine-next-plan-receipt-chain-result/v1`, `status: pass`,
`workflowTerminal: false`, and `nextAction: finalize-changes`. Compare its
returned presentation, approval, and completion receipt paths/hashes and
`finalizationMode` against the ledger. A missing chain, non-passing result, or
mismatch is a blocker. Require the validated `/next-plan` mode to be
`session-landing`; if the caller also supplied a mode, reject it unless it
matches exactly. For routes that did not originate in `/next-plan`,
preserve the caller-supplied mode contract and do not require a receipt chain.

Use [`scripts/Test-FinalizePreflight.ps1`](scripts/Test-FinalizePreflight.ps1) as the single canonical read-only identity, Git-state, manifest, WorktreeCli, and wrapper-claim preflight. Pass the mode; checkpoint; current/primary worktree and branch identities; fixed baseline; phase-appropriate manifest comparison base; verification report path, lowercase SHA-256, and every exact manifest range; plus `BROKEN_ENGINE_WORKTREECLI_SESSION_OWNER` in session mode. Pass `-HasPlanRowClaim` only when finalization owns a row claim, `-HasCompletedPlanClaim` when its receipt needs session `complete --reapply` or primary-commit release, and `-QueueChangingLanding` only for a session landing whose approved manifest changes an executable queue or plan file. Session mode also requires the wrapper's five authoritative provenance variables to match; primary mode requires neither wrapper provenance nor a session claim. Parse the one `broken-engine-finalize-preflight/v1` JSON object and require exit `0`, `status: pass`, and `code: ok`. Exit `2` is the reported deterministic blocker; exit `1` is malformed input or unreadable/internal state. Omitted or empty required inputs, invalid modes/checkpoints, and invalid wait values return this JSON contract; PowerShell parser failures such as an unknown parameter or an option token with no following value occur before the script and are not protocol results. Never reconstruct a failed check ad hoc or search another WorktreeCli path. A missing, empty, wrong-target, or capability-stale executable requires explicitly authorized `/compile` primary maintenance.

The sidecar consumes only the authoritative manifest and PASS-ledger ranges through `Read-AgentReportSection.ps1` and preserves the exact-`PASS` gate. The `initial` result records observed identities and tips. Every later call passes those expectations, updated only when reconciliation or mutation changes state, and keeps the same capability profile. Invoke it at `initial`, after reconciliation when bytes or the primary tip changed, immediately `pre-mutation`, and `post-mutation` around a requested primary commit or primary ref advancement. After reconciliation use the rebased primary parent as `ManifestComparisonBase`; after landing retain the approval-covered comparison base.

For a session landing, [`scripts/Invoke-FinalizeApprovalPreparation.ps1`](scripts/Invoke-FinalizeApprovalPreparation.ps1) is the single pre-approval mutation and review-tool boundary. Invoke it only after reconciliation, completed-plan reapplication, affected reverification, and any triggered session audit have produced the final clean session tree. Pass the same current/primary identities, branches, baseline, manifest comparison base, expected tips, verification report hash/ranges, capability switches, and wrapper owner required by the final `after-reconciliation` preflight. The command validates the original candidate, collapses a linear multi-commit session range to one deterministic tree-identical commit with the current primary tip as its sole parent, atomically replaces only the expected session ref, rolls back a replacement whose postconditions fail, reruns `after-reconciliation` preflight against the final tip, and then opens SmartGit against the registered primary checkout with `--anchor-commit=<final-tip>`. Do not reconstruct its Git or SmartGit commands inline.

Require exit `0`, schema `broken-engine-finalize-approval-preparation/v1`, `status: pass`, `code: ok`, final preflight PASS with manifest equality, and one returned `approvedSession` tip before rendering the landing summary. Treat that tip as the only approval and landing candidate. SmartGit status `unavailable` or `failed` is non-blocking: copy its message and exact `manualCommand` into the approval response while keeping the normal landing gate in force. A preparation blocker leaves primary unchanged; if a replacement occurred, require `rollback: restored-original` before retrying from current state. Run [`scripts/Test-FinalizeApprovalPreparationFixtures.ps1`](scripts/Test-FinalizeApprovalPreparationFixtures.ps1) whenever this contract or its scripts change; the fixtures must not open a real GUI.

For a user-approved `session-landing`, the post-approval transaction is owned
exclusively by [`scripts/Invoke-FinalizeLanding.ps1`](scripts/Invoke-FinalizeLanding.ps1).
Pass the exact approval-covered session commit, the current/primary identities
and expected tips from the final reconciliation preflight, report path/hash and
returned manifest range, wrapper owner/session label, and any retained row
claim locator. For a completed-plan claim, also pass the exact latest WorktreeCli
`plan order complete` receipt JSON; the executor requires its owner, removed
plan, and both queue-byte hashes to match landed primary before unclaiming. The
executor re-runs preflight, derives the canonical Git common
directory and queue-changing scope from the manifest, owns the landing and
queue locks, advances primary, performs post-mutation preflight, reverses
owner-held queue locks, conditionally releases the landing lock, and releases a
verified row claim. Do not assemble any of those commands as inline PowerShell.
Require its `broken-engine-finalize-landing/v1` JSON result and exit `0` before
reporting `LANDED`; exit `2` may report a post-advance blocker but still carries
the authoritative lock-cleanup state.

1. Validate any required `/next-plan` receipt chain, resolve the authoritative
   mode as above, then run the `initial` sidecar preflight with that mode and
   record its canonical identities and current/primary tips. Consume and
   interpret the complete PASS ledger separately. Stop if the chain or sidecar
   blocks or any in-scope testable item is failed, skipped, blocked, or
   unverified. A user decision changes scope/acceptance before verification—it
   does not waive this gate.
2. If running in the primary checkout, leave verified changes uncommitted and stop unless the user explicitly requested a commit. For a requested commit, identify the exact verified paths without staging and render a primary-commit summary before any history mutation. Use `## Context`, `## What landed`, and `## Primary commit`; state `Primary has not been committed.`, then identify the primary checkout, branch and current tip, verified manifest path and SHA-256, intended commit message and paths, queue-changing status, maintenance operation and candidate receipt when applicable, and the exact remaining `git commit --only` mutation. End with `Confirm commit of verified manifest <manifest-sha256> on primary branch <primary-branch> at <primary-tip>?` Proceed only after an explicit affirmative response to that exact summary. If any contract-bound value changes, invalidate the response and render a refreshed summary. A decline or missing response leaves the verified changes uncommitted. If running in an existing session worktree, inspect staged, unstaged, and untracked state; stop if a path mixes unrelated edits with the session change. Preserve unrelated index entries, stage only new session files when needed, commit with `git commit --only -- <verified paths>`, and verify the commit diff exactly matches the verified change set and the committed file contents/deletions are byte-identical to the ledger manifest. A commit that only binds the already-verified manifest to a Git tree does not invalidate the ledger. Never push, force-update, destructively reset, or mutate another session's branch or worktree.
3. **Reconcile automatically before landing approval; stop only when a safety check cannot clear.** When primary has advanced, rebasing the session onto it is the routine multi-agent case — perform it automatically before requesting landing sign-off once BOTH mandatory safety checks below clear. Routine outcomes resolve automatically: disjoint-hunk merges, queue/`Order.md` semantic merges, delete-vs-modify where the other side already completed the same plan (accept the deletion), comment/citation/formatting drift, and version-counter collisions resolved by safety check A. This automatic reconciliation authorizes mutations only in the session worktree; it never authorizes advancing the primary branch.

   **Safety check A — determinism-counter collision.** A textual merge silently mis-resolves version/compatibility counters, and there is NO runtime backstop for a wrong counter value: a replay/CRC check runs inside ONE build and shares whatever value you pick, so it cannot detect a wrong cross-version number. This must therefore be correct AND complete at merge time. These counters are frequently a SUM of independent summands — `Frame::kiVersion` = a base constant + `engine::kiNavDataVersion` + each collection's `kiVersion` — plus standalone counters `kuiProtocolVersion`, DataPacker `.pack` / `DataHeader::kiVersion` / each `Export*::GetVersion`, and any other determinism/compatibility counter. Key the check on the INDIVIDUAL summand a change owns, NEVER the computed total:
   - If only one side changed a summand, git merged it correctly — leave it (do not re-stack; the total already advanced).
   - If BOTH sides changed the SAME summand from a common base, decide whether the two bumps are INDEPENDENT changes (different accompanying payload) or the SAME logical change (a cherry-pick, or the same plan run in two worktrees). Same change → keep the textual value. Independent → the textual merge under-resolves; set that summand to `common-base + (number of independent bumps)` so each landing owns a distinct value (two independent `X→X+1` bumps ⇒ `X+2`).
   - Never stack a summand only one side touched, and never stack a duplicate of the same change (that over-invalidates saves and can gratuitously break `kuiProtocolVersion` interop).

   **Safety check B — primary-delta vs session-assumption cross-check.** A textually clean rebase can still be semantically broken: primary may have changed a function, data/`.pack`/save layout, symbol, or CRC/SOA invariant that the session's changed files depend on, in a DIFFERENT file — so there is no conflict, it compiles, and the session's own (session-scoped) re-verify never re-examines primary's file. Enumerate primary's delta since the session base (`git diff <session-base>..<primary-tip>`; read the relevant hunks) and cross-check each session dependency against it: APIs/functions the session calls, data/`.pack`/save layouts the session reads or writes, symbols the session references, and CRC/SOA-member/version invariants the session touches. If primary changed any of them in a way the session's code assumes otherwise, the merge is wrong despite compiling.

   **Reconcile automatically only if BOTH checks clear.** Apply check A's summand fix by hand where needed; then rebase and let step 7 apply the evidence-preservation rule or run any required affected verification (a rerun validates the build and merged code, NOT the version numbers — check A is your only guard for those). Successful reconciliation still proceeds to the mandatory landing summary and user sign-off; do not advance primary yet.

   **Stop for a reconciliation decision** when either check cannot be cleared confidently: a summand collision whose independent-vs-duplicate status or correct value is unclear; a primary change that invalidates a session assumption and cannot be mechanically reconciled; overlapping edits to the same logic whose intents cannot both be preserved; or any reconcile you cannot affirmatively prove safe. **Default to stopping under uncertainty.** This decision is distinct from the mandatory landing sign-off: it resolves how to produce a reconciled session tree, but never authorizes advancing primary. Wait WITHOUT holding a clear-worktree PC-global landing lock; if it is already claimed (step 4), inspect every registered worktree as recovery does, release the exact owner only when all are inspectable and clear, and otherwise retain and report the claim. Re-claim and re-verify primary identity and tip after the user resolves the reconciliation question.

   **Evidence preservation and rerouting after rebase.** A completed acceptance ledger remains valid across a conflict-free rebase when both safety checks clear and the rebase leaves every session-owned changed path byte-identical to the verified manifest. Primary-only files entering the rebased history are not session changes and do not trigger the compile skill's baseline-diff data-mode rules. Regenerate the session manifest relative to the rebased primary parent and require it to match the verified manifest exactly; inspect the primary delta through safety check B. If reconciliation changes session-owned bytes or invalidates an assumption, return through only the affected targeted compile/static checks, correctness review of changed or newly relevant regions, triggered hygiene roles, and `/verify-changes` matrix rows. Required plan-order completion reapplication always regenerates verification so its receipt and validation are current. Unrelated primary deltas—including generated data or tracked asset changes—do not invalidate passing evidence.

   When the caller holds a completed-plan claim and cleanup receipt, reconciliation always invokes `plan order complete --repo <common-dir> --worktree <session-worktree> --owner <claim-owner> --session <label> --plan <normalized plan> --reapply` after the rebase and semantic conflict resolution. If an executable `Order.md` conflicts, resolve it to the complete primary version as a file; never select, delete, or hand-edit individual queue rows. After the rebase, `complete --reapply` is the deterministic owner of the target row/file removal and any dependency-edge pruning. Reapply is required even when the row/file are already absent: it emits a receipt bound to exact reconciled queue bytes. Require successful queue unlocks, then run session `plan order validate` and regenerate `/verify-changes` so its PASS ledger contains that reapply receipt and validation result. Never edit queue Markdown during reconciliation.

   After reconciliation and any required affected verification pass, apply the
   root conditional whole-change-audit gate. Invoke one fresh `/session-audit`
   over the complete logical change only when there were late semantic fixes,
   reconciliation changes, Tier-3 cross-file integration, or contract-significant
   changed regions no correctness reviewer saw. Record N/A with the evaluated
   triggers when none applies. A second audit requires a named distinct evidence
   domain; never duplicate the same prompt to seek consensus. Any accepted audit
   fix returns through its affected targeted check/review and verification rows
   before this gate is evaluated again.

	Then invoke the canonical approval-preparation command exactly once against that final verified state. Consume its final tip, preflight, squash/no-op, and SmartGit result. The command's tree and manifest identity checks preserve content-based evidence across a squash; its returned tip replaces the pre-squash session tip in every approval-bound field. If SmartGit did not open, include the returned message and manual command in the visible approval response without weakening or bypassing the landing confirmation.

   Then return the preparation-stage final user-visible response with an
   executive merge-request-style landing summary derived from the exact
   verified session tree. Render this response before any primary mutation.
   Write it as the retrospective counterpart to the approved plan: explain
   what motivated the change and what behavior or scope actually landed. Keep
   verification ledgers and implementation mechanics internal unless they
   expose a blocker the user must decide. Do not reduce the summary to a terse
   checklist, raw changed-file inventory, or handful of one-line bullets.

   Use these sections:

   - `## Context` — the original problem, its concrete consequences, and the intended outcome.
   - `## What landed` — the final user-visible or workflow-visible outcome, grouped by plan objective. Describe what is now included, removed, or intentionally queued without explaining internal algorithms, command plumbing, individual file edits, or test procedure.
   - `## Landing` — state `Primary has not advanced.` Then include the primary
      branch and current tip, session branch and approved session tip,
      reconciliation disposition (and any user-supplied decision), whether the
      verified manifest makes this queue-changing, the disposition of every
      remaining objective stage, and the exact remaining operation. These
      values, including the manifest identity, are the approval-covered state.

   Prefer short explanatory paragraphs. Omit verification results, review history, warnings, residual ledgers, file inventories, function/region details, and implementation mechanics from this approval summary unless one is a current blocker or materially changes what the user is approving. The result must tell a reviewer what is in the change without making them understand how it is implemented. End the response with this one direct question, substituting the current values: `Confirm landing <approved-session-commit> from <session-branch> onto primary branch <primary-branch>?` Do not ask another landing-confirmation question while this summary remains unchanged. Do not treat plan approval, implementation approval, a reconciliation decision, or general instructions to finish as landing sign-off. Proceed only after an explicit affirmative response to this exact final summary; only that response may invoke `Invoke-FinalizeLanding.ps1`. If the user declines, requests changes, or does not answer, stop with primary unchanged and apply the clear-release/active-retain rule. If the verified manifest, primary branch or tip, session branch or tip, reconciliation disposition, queue-changing status, scope, residuals, or verification status changes after rendering the summary and before primary advancement, invalidate the approval and return a refreshed visible summary with one new confirmation question.
4. Use only the exact WorktreeCli path and canonical Git common directory emitted by the successful sidecar; finalization never builds WorktreeCli or writes through shared Output. For a post-approval `session-landing`, `Invoke-FinalizeLanding.ps1` exclusively owns every lock, queue, Git, cleanup, and row-release action described in steps 4–11 below. The following detail is its contract, not an invitation to reconstruct commands inline. For a requested primary-checkout commit, generate an owner with `lock token`, and immediately before the history mutation run `lock claim --repo <git-common-dir> --owner <token> --session <label> --worktree <session-worktree> --lease-seconds 3600`. If claim returns held, inspect `lock status`. A live lease blocks. Recover an expired schema-3 lease only with explicit user approval and `lock recover --repo <git-common-dir> --expect <reported-owner> --owner <token> --session <label> --worktree <session-worktree> --lease-seconds 3600`; recovery itself verifies every registered worktree is inspectable and clear. An `unverifiable` schema 1–2 landing record may be taken over only with explicit approval and owner-matched `lock steal ... --lease-seconds 3600`, which upgrades it to schema 3. Other unverifiable records block.
5. Require `lock status --repo <git-common-dir>` to report this owner and `leaseState: live`. Before and after every Git mutation and every review, build, verification, or fix phase, run owner-only `lock refresh --repo <git-common-dir> --owner <token>`; while actively working or monitoring, refresh at least every 15 minutes. Refresh immediately before each Git command to narrow the worktree-snapshot race. Never refresh while waiting indefinitely for user input.
6. After the exact primary-commit confirmation required by step 2, run `pre-mutation` and require every approval-bound identity to remain unchanged. Preserve unrelated index entries, stage only new session files when needed, commit with `git commit --only -- <verified paths>`, then run `post-mutation` with the committed tip as the new expectation and verify the commit diff exactly matches the verified set and the unrelated index is unchanged. Invoke [`Write-AgentLandingArtifact.ps1`](../../scripts/Write-AgentLandingArtifact.ps1) with the committed tip, verified global report, report hash/ranges, owner/session identity, client identity, and completed-plan receipt when present before owner-unclaim. When the verified run owns a completed plan row, run `plan order validate --repo <git-common-dir> --worktree <primary-worktree>` on the now-clean committed primary and require exit `0`, JSON `ok: true`, and no diagnostics. Require the completed receipt's plan identity and both queue hashes to match primary and its plan file to remain absent; owner-check the row claim, then owner-unclaim it. Any validation, artifact, receipt, or release mismatch is a finalization blocker. Reverify this owner with `lock status`, release with `lock release --repo <git-common-dir> --owner <token>`, report `COMMITTED`, and stop.
7. For a session-worktree landing, refresh, rebase the session branch onto the primary-branch `HEAD`, then reconcile per step 3 without discarding either side. Reapply a completed-plan cleanup before final-tree verification. When reconciliation changes session-owned bytes, a completion is reapplied, or an assumption is invalidated, rerun only the affected checks, review, hygiene, and final ledger rows. After that final state and any triggered audit are ready, invoke `Invoke-FinalizeApprovalPreparation.ps1`; it owns the final `after-reconciliation` preflight and returns the only approvable session tip. Release the landing lock before rendering the visible summary and asking its one explicit landing-confirmation question. If the manifest, primary branch or tip, approved session branch or tip, reconciliation disposition, or queue-changing status changes after summary rendering, invalidate approval, reconcile and reverify as needed, rerun approval preparation, then return a refreshed summary. After approval, reacquire the lease immediately before step 8. Never merge primary into the session branch.
8. Only after explicit landing approval and reacquiring the landing lock, require `lock status` to report this owner, record the exact user-approved rebased session tip as `<verified-session-commit>`, and run `pre-mutation` with the approval-covered tip pair and manifest base. Determine whether the approved manifest changes either executable queue or any executable plan file under `Documents/Plans/` or `Documents/Features/`; if so, this is a queue-changing landing. For a queue-changing landing, while still holding the landing lock, acquire the Plans and Features queue locks in canonical path order with the landing owner/session identity. Record each successfully acquired exact queue locator immediately and require each lock to report ownership. Any failure from the first queue-lock attempt onward invokes the universal acquired-lock safe-stop rule below. Do not acquire queue locks for a non-queue landing.

   With the required locks held, re-read the primary checkout path, branch, `HEAD`, in-progress Git state, and porcelain status; require it to equal the exact `<landing-base-head>` covered by approval. Run `git merge-base --is-ancestor <landing-base-head> <verified-session-commit>` and require success, require `git rev-list <verified-session-commit>..<landing-base-head>` and `git rev-list --min-parents=2 <landing-base-head>..<verified-session-commit>` both produce empty output, then re-read primary identity, `HEAD`, in-progress state, and status and require the session branch still resolves to `<verified-session-commit>`. If primary or session moved, invoke the universal acquired-lock safe-stop rule and return to step 7; the prior approval is invalid. These checks must prove the final primary-side `git rebase <verified-session-commit>` has no primary commits to replay and can only advance the primary ref to the exact user-approved, already-verified session commit without creating or rewriting commits or content.

   Immediately before the primary Git command, run `pre-mutation` again with unchanged expectations. From the clean, unchanged primary run `git rebase <verified-session-commit>` while still holding landing then both queue locks for a queue-changing advance. Run `post-mutation` with both expected tips set to `<verified-session-commit>` and the approval-covered manifest base. Release both queue locks through the universal acquired-lock safe-stop rule's owner checks and reverse-canonical traversal before releasing the landing lock. This pure ref advance does not require another test rerun after primary is updated. If post-landing preflight fails, stop and apply the same universal rule while preserving lock-order release obligations.

   **Universal acquired-lock safe-stop rule.** This rule governs every cancellation, blocker, or failure after the first queue-lock acquisition attempt, including partial acquisition, second-lock acquisition or ownership failure, identity/ancestry/cleanliness failure, `pre-mutation` failure, Git failure, `post-mutation` failure, and the normal unlock path. Maintain the exact locators of successfully acquired queues. Before applying the landing-lock safe-stop rule, visit those locators once in reverse canonical order; owner-check each with `plan queue status`, unlock only a record still owned by the landing owner, and then prove that exact locator absent. Never unlock an absent, foreign-owned, or unverifiable record. Continue attempting the remaining earlier-acquired locators after one release failure. If any acquired queue cannot be proven released, retain that queue record and the landing claim and report its exact canonical repository, Order path, owner, status/lease evidence, and unlock error; never hide it behind a generic landing blocker. Only after every acquired queue is proven released may the landing claim follow the clear-worktree release/active-retain rule below.
9. Refresh and reverify `lock status` reports this owner and a live lease, require that every queue lock acquired in step 8 has been released, then run `lock release --repo <git-common-dir> --owner <token>`.
10. For a session-worktree landing that holds a plan-row claim, `Invoke-FinalizeLanding.ps1` writes the commit-keyed global landing artifact after its post-mutation preflight and before row release. If that write fails after the Git advance, it reports the landed-but-blocked artifact residual, releases its locks, and retains the row claim. After restoring artifact-store access, retry only `Invoke-FinalizeLanding.ps1 -ArtifactOnly` with both expected tips equal to the landed session commit; it reruns post-mutation validation, writes the artifact, and releases retained ownership without another Git mutation. Otherwise release the row only after the landed commit, primary manifest, and artifact are verified. Owner-check with `plan row status --repo <canonical-git-common-dir> --order <repo-relative-Order.md> --plan <Order-relative-plan>`, then post-land owner-unclaim with the same locator plus `--owner <caller-token>`. For completed plans, also require the landed tree to match the verified latest `complete`/`complete --reapply` receipt before unclaiming. Treat absence, owner mismatch, artifact, receipt mismatch, or release failure as a finalization blocker. Primary-commit release is owned by step 6. Other caller-scoped claims use their owning WorktreeCli API.
11. Verify the session worktree is clean, remains registered at the recorded path on the recorded branch, and its tip is fully contained in primary. Retain the worktree and branch unchanged for explicit user-managed cleanup; do not run `git worktree remove`, delete the directory, or delete the branch.
12. After every landing, claim-release, primary/session identity, manifest,
    containment, and cleanliness check above succeeds, re-read the manager's
    complete objective ledger. Repository success completes only the current
    stage. If another stage remains active, continue it in this session; if its
    next action needs approval, present that gate. Do not emit a terminal or
    safe-close message merely because the worktree is clean or claims are
    released.

    The session is objective-terminal only when every recorded stage and
    deliverable is complete, or each unfinished stage was explicitly deferred
    by the user and has a verified receipt in the live WorktreeCli plan queue.
    A plan file in a session worktree, an unlanded queue edit, or an informal
    follow-up note does not satisfy that rule. Re-read the pre-implementation
    `broken-engine-execution-control/v1` record and hash from the immutable
    verification ledger. Write the current complete objective ledger beneath
    ignored `Temp/` using
    `broken-engine-objective-ledger/v1`. Every stage records an ID and
    disposition and exactly the record's deliverables; the stage sets must be
    identical. A deferred stage also records the record-approved queue and plan
    identity, `userDeferred: true`, and the current live-primary WorktreeCli
    receipt (queue, plan, row hash, and primary commit). Run
    [`scripts/Test-FinalizeObjectiveContract.ps1`](scripts/Test-FinalizeObjectiveContract.ps1)
    with `-ObjectiveLedgerPath`, `-ExecutionControlPath`,
    `-ExecutionControlSha256`, and `-LivePrimaryWorktree`. The script verifies
    the prior record hash, exact stage/deliverable sets, and approved deferral
    identities, then invokes the current checkout's WorktreeCli against that
    clean primary and matches each deferred row and primary commit.
    Require its
    `broken-engine-finalize-objective-contract/v1` result to report `status:
    pass`, `objectiveTerminal: true`, and `nextAction: session-complete` before
    emitting the terminal block. Also run
    [`scripts/Test-FinalizeObjectiveContractFixtures.ps1`](scripts/Test-FinalizeObjectiveContractFixtures.ps1)
    whenever these instructions change to prove the policy and decision cases.

    Only after the repository checks and the objective-terminal rule both pass,
    end the user-facing response with this exact standalone block as its final
    content:

    ```text
    SESSION COMPLETE
    All verified worktree changes have landed on the parent branch, and all session claims are released.
    It is safe to close this session tab.
    ```

    Do not place caveats, residuals, follow-up suggestions, or any other text after this block. Never emit `SESSION COMPLETE` for `LEFT UNCOMMITTED`, `COMMITTED`, a blocked/failed landing, a retained claim, a dirty checkout, unequal primary/session tips, an active objective stage, an unfinished stage without explicit user deferral and a verified live-queue receipt, or any result that still requires user action. Those outcomes must end with their blocker or next required action instead.

Stop and report the exact blocker before any operation that would require broader authority, disturb unrelated changes, bypass a failed verification, or violate lock ownership.

On every cancellation, blocker, or failure after acquisition, first apply the universal acquired-lock safe-stop rule when any step-8 queue acquisition was attempted. Then enumerate all registered worktrees and resolve each administrative Git directory exactly as recovery does. If all are inspectable and none contains `MERGE_HEAD`, `rebase-merge`, `rebase-apply`, `CHERRY_PICK_HEAD`, `REVERT_HEAD`, `BISECT_LOG`, or `sequencer`, release the exactly owned landing claim before ending the turn. If any queue release failed, any marker exists, or inspection is unverifiable, retain the landing claim and report its owner, lease state, repository locator, and reason. Never leave a clear-worktree lease held across an open-ended user-response wait.

## Output

Report:

- `Finalization: LEFT UNCOMMITTED | COMMITTED | LANDED`
- `Objective status: COMPLETE | CONTINUING | DEFERRED TO LIVE QUEUE`
- Checkout, branch, and resulting commit
- Landing-lock status and retained session worktree/branch
- Reconcile mode: automatic or user-directed, plus landing-sign-off status and the exact primary/session commit pair covered by approval
- SmartGit launch status and returned manual command when launch was unavailable or failed
- Verification rerun after reconciliation, if any
- Files changed during finalization, or `none`
- Functions/regions touched during finalization, or `none`
- Residuals: blocker or `none` (always last)
- For `LANDED` with every repository and objective-terminal condition in step 12 satisfied, append the exact `SESSION COMPLETE` block after the structured report; it is the absolute final output.
