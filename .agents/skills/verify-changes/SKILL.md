---
name: verify-changes
description: >-
  Verify Tier-3, queue, reconciliation, and landing changes with a final-tree
  acceptance ledger and manifest. Tier-1 and Tier-2 work uses proportionate
  direct checks and an inline completion summary instead.
allowed-tools: [Read, Grep, Glob, Agent, "Bash(git diff *)", "Bash(git status *)", "Bash(git ls-files *)", "Bash(git hash-object *)", PowerShell]
---

# Verify Changes

Use this skill only for a final-evidence gate: queue mutation or completion,
reconciliation, or requested landing. Tier 1, Tier 2, and Tier 3 work without
one of those gates reports its direct checks, changed files, and residuals
inline without a final-tree ledger or manifest. Completed `/next-plan` routes
also follow the [canonical execution-gate
contract](../next-plan/references/execution-gates.md).

## Inputs

Require:

- Absolute adopted worktree path and fixed session-start baseline commit; the primary checkout is valid only for an explicitly user-authorized `primary-commit`
- Final approved plan and caller-supplied approved-delta summary (`none` is valid)
- Manager execution-control record bound to the fixed process baseline: approved
  risk tier and concrete triggers, required and conditional roles, triggered-role
  dispositions, and the initial acceptance matrix
- For a delegated call, an absolute caller-assigned `ReportPath` allocated by
  [`New-AgentReportPath.ps1`](../../scripts/New-AgentReportPath.ps1) beneath
  `%LOCALAPPDATA%\BrokenEngine\AgentReports\<repository-hash>`
- Accumulated inline implementation, review, hygiene, build, and residual handoffs with their decisive checks
- Every WorktreeCli `plan order add`, `update`, or `complete` receipt relevant to the final tree, including the request/plan identity, queue hashes, unlock results, owner/session identity, and `--reapply` disposition when present
- For a completed `/next-plan` route, its immutable completion receipt path and
  lowercase SHA-256. The chain validator derives the authoritative
  presentation and approval receipt identities and requires the authoritative
  `session-landing` finalization mode; caller restatements are not substitutes.

If lifecycle identity or required evidence is missing or ambiguous, return `BLOCKED`; do not rediscover a movable baseline or invent acceptance criteria.
Block when any handoff shows an unapproved plan delta. Only main may apply an exact user-approved delta before verification.

## Workflow

1. Require the current checkout to be the adopted worktree and the supplied baseline to be its fixed session-start commit. Inventory the final diff from that baseline, including untracked files.
   For a completed `/next-plan` route, run
   [`Test-NextPlanReceiptChain.ps1`](../next-plan/scripts/Test-NextPlanReceiptChain.ps1)
   with only `-CompletionReceiptPath` and `-CompletionReceiptSha256`. Require exit `0`,
   `broken-engine-next-plan-receipt-chain-result/v1`, `status: pass`,
   `workflowTerminal: false`, and `nextAction: finalize-changes`. Exit `2` is a
   deterministic blocker and exit `1` is malformed input or internal failure;
   neither may be reconstructed or waived. A missing completion receipt or
   hash is `BLOCKED`. Use only the validator-returned `presentationReceipt`,
   `approvalReceipt`, `completionReceipt`, and `finalizationMode` thereafter.
   Require that validated mode to be `session-landing`.
2. Build a ledger from the final approved plan plus approved-delta summary, the
   execution-control record, every changed behavior visible in the diff, every
   supplied plan-order receipt, and every test obligation or residual in supplied
   handoffs. Every row has exactly these decision fields:
   `criterion/behavior -> decisive check -> expected result -> independent signal
   if this duplicates another check (otherwise none) -> status/evidence`.
   One decisive check may cover several criteria. Do not duplicate a check unless
   the row names the distinct independent signal it adds. Documentation, skill,
   tooling, and configuration changes require the strongest applicable static or
   functional check; they do not receive an automatic runtime skip.
   A completed `/next-plan` ledger must record the validator's PASS result and
   authoritative `finalizationMode`, plus the canonical path and lowercase
   SHA-256 for its returned presentation, approval, and completion receipts.
   Any mismatch with a caller-supplied receipt identity or mode makes the
   ledger non-passing.
   When the manifest contains a changed `.agents/skills/*/SKILL.md`, invoke `/validate-skill` once for each changed skill and record its complete decisive result. That shared workflow is the only skill-validation contract: do not substitute a client-installed validator or abbreviated frontmatter check. Require `Validation: PASS`; `FAIL`, `BLOCKED`, `INVALID`, `SETUP_ERROR`, result/exit mismatch, or inability to run the validator makes the ledger non-passing.
3. Apply the approved tier as a ceiling on exploratory work, never as permission
   to weaken a criterion:
   - **Tier 1:** decisive static/schema/link/validator checks plus affected-target
     compilation when C++ changed. Do not add runtime scenarios without an
     approved criterion or exposed behavior requiring one.
   - **Tier 2:** Tier-1 checks plus the smallest observable behavior scenario for
     each approved runtime/tool behavior.
   - **Tier 3:** lower-tier checks plus only the invariant/integration checks the
     change actually exposes: client/server, replay/determinism/CRC, wire or
     serialization/save compatibility, threading, trust boundary, or shared
     build/bootstrap coordination.
   For an applicable runtime check, have an Opus subagent invoke `/agent-harness`,
   using a self-contained fresh Claude prompt or Codex `fork_turns:"none"` with
   the baseline, final plan/deltas, execution-control record, and residual chain.
   Use the plan's Verification section when present; otherwise derive the smallest
   live scenario that decisively covers the approved criterion.
4. Mark each testable item `PASS`, `FAIL`, `BLOCKED`, or `UNVERIFIED`, with exact evidence. Never use `SKIPPED` to dispose of an in-scope item. Evidence names the command or harness scenario, exit/verdict, and the decisive output, query, screenshot, log, or artifact location.
5. Return each decisive failure and its evidence to the main session for its one
   Intent (`conformance | plan_delta`) and Scope (`non_structural | structural`)
   adjudication. Main sends only accepted `conformance + non_structural` work to
   an Opus subagent invoking `/resolve-findings`; plan deltas require user
   approval. An accepted in-scope structural acceptance failure remains a blocker
   pending user direction; only proven pre-existing or out-of-scope structural
   residuals route to the conditional `/create-follow-up-plans` role. After an
   eligible fix, rerun the failed check and only the checks whose evidence the
   changed bytes invalidate. Default to
   one focused fix/retest round; a second requires a still-reproducible decisive
   blocker and examines only affected regions/checks.
6. Treat a check requiring new authority, hardware, or external coordination as `BLOCKED`. Report the blocker to the main session for a user decision; do not acquire the authority, silently narrow the check, convert it to a follow-up, or accept a waiver. Only an explicit user revision to scope or acceptance criteria can remove the original obligation. Rebuild the ledger from the revision and verify every revised criterion.
   Record every touched non-worktree state as `unchanged`, `intentionally persisted under owner contract`, `restored`, or `residual`, with exact path, owning contract/serialization mechanism, and evidence.
7. When the final tree changes an executable plan file or either queue, or carries a WorktreeCli add/update/complete receipt, set `$WorktreeCli` to the current checkout's provisioned `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe`. In a session worktree, run `plan order validate --repo <canonical-git-common-dir> --worktree <session-worktree>` and require exit `0`, JSON `ok: true`, and no diagnostics. A completed-plan row claim remains intentionally owner-held until finalization/landing: record its owner-qualified `ownedByRequester: true` status, not an absence expectation. When calling the row API directly, use the plan key relative to the selected `Order.md` directory. For an explicitly user-authorized primary commit, the owned `complete` receipt is the pre-commit decisive queue check: require its success, exact plan identity, queue hashes, and unlock results, then record clean-primary validation and owner-unclaim as mandatory `/finalize-changes` guards. Do not run the public validator against the dirty primary or call that guard a skipped acceptance item. Otherwise record `plan-order validation: not triggered — no queue or executable-plan change`; do not run it merely because another repository file changed.
8. Create the immutable final report through
	[`Write-AgentVerificationReport.ps1`](../../scripts/Write-AgentVerificationReport.ps1).
   Allocate the fresh report path through `New-AgentReportPath.ps1`, then pass
   it with the adopted worktree, fixed baseline,
	phase-appropriate manifest comparison base, plan/intent, and complete
	single-line ledger and queue-receipt/residual entries. Require exit `0` and
	`broken-engine-verification-report/v1` JSON with `status: pass`. Use only its
	returned report path, lowercase SHA-256, and manifest range thereafter. The
	writer collects NUL-delimited changed and untracked paths, normalizes and
	ordinal-sorts them, and emits raw `path<TAB>blob:<oid>|DELETED` rows. Never
	write, decorate, or range-count manifest rows in agent-authored Markdown.
	When ledger or receipt entries are PowerShell arrays, invoke the writer from
	the current PowerShell session with one hashtable splat (`& $writer
	@writerArgs`). Do not pass an array to `pwsh -File ... -AcceptanceLedgerLine`:
	command-line expansion turns its later entries into positional arguments.
	Any repository mutation that changes a path, blob hash, or deletion marker
	invalidates the ledger: call the writer again for a new report and rerun the
	newly affected checks before reporting success.
9. Return `PASS` only when every in-scope testable item is `PASS` and the ledger is bound to the current manifest. Any `FAIL`, `SKIPPED`, `BLOCKED`, or `UNVERIFIED` item makes the overall result non-passing and prevents conditional audit and landing. Once the complete matrix passes, stop: do not add exploratory variants, consensus reruns, or unrelated checks. Loop only when changed bytes invalidate earlier evidence or a decisive check fails.

## Output

Follow [`../../references/subagent-reporting.md`](../../references/subagent-reporting.md).
For a final-evidence gate, require one fresh global `ReportPath` and use the shared
writer exactly once after the final checks. Keep `Verification`,
worktree/baseline, final-manifest identity, every non-passing status, and every
decision-driving blocker in that one report. Without a final gate, retain the
complete result inline:

- `Verification: PASS | BLOCKED`
- Adopted worktree and fixed baseline
- Final changed-file manifest, one canonical entry per line
- Ledger entries: criterion/behavior, decisive check, expected result, independent
  signal for any duplicate (otherwise `none`), status, and exact evidence
- Session `plan order validate` result, or the primary-commit finalization obligation, plus every relevant add/update/complete receipt including exact queue hashes/unlock disposition
- For a completed `/next-plan` route, receipt-chain validator PASS,
  authoritative finalization mode, and exact presentation, approval, and
  completion receipt paths/hashes
- Fix/retest rounds and repository mutations, or `none`
- Failed, blocked, skipped, or unverified items, or `none`
- Residuals: blocker or `none` (always last)

The one final report identifies the complete authoritative changed-file manifest
and PASS ledger. `/finalize-changes` consumes only the exact ranges it needs
from that report and independently revalidates any recorded `/next-plan`
receipt chain; intermediate roles provide no report chain.

On a non-passing result, use `Verification: BLOCKED` and preserve each actual item status and evidence in the blocker list. Never emit a final-tree manifest as verified when it was generated before the last mutation.
