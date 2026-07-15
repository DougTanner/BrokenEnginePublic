---
name: verify-changes
description: >-
  Verify every testable repository change in the C++ Code Change Process Verify
  acceptance stage. Use after implementation, targeted checks, review resolution,
  and triggered style/documentation/project updates to create a risk-tiered
  final-tree acceptance ledger and the content manifest required by
  /finalize-changes.
allowed-tools: [Read, Grep, Glob, Agent, "Bash(git diff *)", "Bash(git status *)", "Bash(git ls-files *)", "Bash(git hash-object *)", PowerShell]
---

# Verify Changes

Prove the complete session change set in its isolated worktree. Do not weaken acceptance criteria, waive evidence, or report success for a partially verified tree.

## Inputs

Require:

- Absolute adopted session-worktree path and fixed session-start baseline commit
- Final approved plan and caller-supplied approved-delta summary (`none` is valid)
- Manager execution-control record bound to the fixed process baseline: approved
  risk tier and concrete triggers, required and conditional roles, triggered-role
  dispositions, and the initial acceptance matrix
- For a delegated call, an absolute caller-assigned `ReportPath` under the session worktree's `Temp/AgentReports/`
- Accumulated implementation, affected-code, review, style, documentation, project-membership, and build report compact-envelope identities (`REPORT`, `REPORT_SHA256`), including every residual and handoff ID with exact evidence locators and dependencies; invoke `Read-AgentReportSection.ps1` once per exact range under the shared [`be-agent-report/v1`](../../references/subagent-reporting.md) consumption contract
- Every AgentCli `plan order add`, `update`, or `complete` receipt relevant to the final tree, including the request/plan identity, queue hashes, unlock results, owner/session identity, and `--reapply` disposition when present

If lifecycle identity or required evidence is missing or ambiguous, return `BLOCKED`; do not rediscover a movable baseline or invent acceptance criteria.
Block when any report shows an unapproved plan delta or a subagent edit to the approved plan. Only main may apply an exact user-approved delta before verification.

## Workflow

1. Require the current checkout to be the adopted session worktree and the supplied baseline to be its fixed session-start commit. Inventory the final diff from that baseline, including untracked files.
2. Build a ledger from the final approved plan plus approved-delta summary, the
   execution-control record, every changed behavior visible in the diff, every
   supplied plan-order receipt, and every test obligation or residual in supplied
   reports. Every row has exactly these decision fields:
   `criterion/behavior -> decisive check -> expected result -> independent signal
   if this duplicates another check (otherwise none) -> status/evidence`.
   One decisive check may cover several criteria. Do not duplicate a check unless
   the row names the distinct independent signal it adds. Documentation, skill,
   tooling, and configuration changes require the strongest applicable static or
   functional check; they do not receive an automatic runtime skip.
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
7. After the last repository mutation, set `$AgentCli` to the current checkout's provisioned `Tools\AgentCli\Platforms\VisualStudio2026\Output\AgentCli.exe` and run `plan order validate --repo <canonical-git-common-dir> --worktree <session-worktree>`. Require exit `0`, JSON `ok: true`, and no diagnostics. Record the exact validation result and every add/update/complete receipt relevant to the final tree in the PASS ledger; record `none` when the session made no plan-order mutation. For completion after reconciliation, require the receipt to be the latest `complete --reapply` result bound to the reconciled queue bytes. A required receipt that is missing, stale, failed, or unlock-failed makes verification non-passing. Do not parse `Order.md` or substitute hand validation.
8. Create the canonical final changed-file manifest relative to the fixed baseline:
	- Collect NUL-delimited paths from `git diff --name-only --no-renames -z <baseline> --` and `git ls-files --others --exclude-standard -z`.
	- Normalize separators to `/`, deduplicate, and sort paths ordinally. A rename is the old path deletion plus the new path blob.
	- For each present file, record `<path>\tblob:<oid>`, where `<oid>` is from `git hash-object --path=<path> -- <path>` so Git clean filters are applied. For each absent path, record `<path>\tDELETED`.
	- Any repository mutation that changes a path, blob hash, or deletion marker invalidates the ledger. Regenerate the manifest, rebuild the ledger against that final tree, and rerun all newly affected checks before reporting success.
9. Return `PASS` only when every in-scope testable item is `PASS` and the ledger is bound to the current manifest. Any `FAIL`, `SKIPPED`, `BLOCKED`, or `UNVERIFIED` item makes the overall result non-passing and prevents conditional audit and landing. Once the complete matrix passes, stop: do not add exploratory variants, consensus reruns, or unrelated checks. Loop only when changed bytes invalidate earlier evidence or a decisive check fails.

## Output

Follow [`../../references/subagent-reporting.md`](../../references/subagent-reporting.md).
For a delegated call, require `ReportPath`, write this complete report there,
and return only the compact indexed envelope. Keep `Verification`,
worktree/baseline, final-manifest identity, every non-passing status, and any
decision-driving blocker visible in the envelope. With no delegated
`ReportPath`, retain the complete inline report:

- `Verification: PASS | BLOCKED`
- Worktree and fixed baseline
- Final changed-file manifest, one canonical entry per line
- Ledger entries: criterion/behavior, decisive check, expected result, independent
  signal for any duplicate (otherwise `none`), status, and exact evidence
- Session `plan order validate` result and relevant add/update/complete receipt, including exact queue hashes/unlock disposition
- Fix/retest rounds and repository mutations, or `none`
- Failed, blocked, skipped, or unverified items, or `none`
- Residuals: blocker or `none` (always last)

The compact envelope must index bounded ranges covering the complete
authoritative final changed-file manifest and the complete PASS ledger, even
when every row passes; split only at exact range boundaries within the shared
16 KiB limit. These safety-gate ranges are mandatory and cannot be replaced by
a summary or `INDEX: none`.

On a non-passing result, use `Verification: BLOCKED` and preserve each actual item status and evidence in the blocker list. Never emit a final-tree manifest as verified when it was generated before the last mutation.
