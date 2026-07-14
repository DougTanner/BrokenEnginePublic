---
name: verify-changes
description: >-
  Verify every testable repository change at C++ Code Change Process step 9.
  Use after implementation, review resolution, documentation updates, project
  updates, and builds to create a final-tree verification ledger, drive the
  test-fix-retest loop, and produce the content manifest required by
  /finalize-changes.
allowed-tools: [Read, Grep, Glob, Agent, "Bash(git diff *)", "Bash(git status *)", "Bash(git ls-files *)", "Bash(git hash-object *)", PowerShell]
---

# Verify Changes

Prove the complete session change set in its isolated worktree. Do not weaken acceptance criteria, waive evidence, or report success for a partially verified tree.

## Inputs

Require:

- Absolute adopted session-worktree path and fixed session-start baseline commit
- Final approved plan and caller-supplied approved-delta summary (`none` is valid)
- For a delegated call, an absolute caller-assigned `ReportPath` under the session worktree's `Temp/AgentReports/`
- Paths to accumulated implementation, affected-code, review, style, documentation, project-membership, and build reports, including every residual and handoff

If lifecycle identity or required evidence is missing or ambiguous, return `BLOCKED`; do not rediscover a movable baseline or invent acceptance criteria.
Block when any report shows an unapproved plan delta or a subagent edit to the approved plan. Only main may apply an exact user-approved delta before verification.

## Workflow

1. Require the current checkout to be the adopted session worktree and the supplied baseline to be its fixed session-start commit. Inventory the final diff from that baseline, including untracked files.
2. Build a ledger from the final approved plan plus approved-delta summary, every changed behavior visible in the diff, and every test obligation or residual in supplied reports. Give each testable item one exact check and expected result. Documentation, skill, tooling, and configuration changes require the strongest applicable static or functional check; they do not receive an automatic runtime skip.
3. Run every check in the isolated worktree. For runtime-observable changes, have an Opus subagent invoke `/agent-harness`, using a self-contained fresh Claude prompt or Codex `fork_turns:"none"` with the baseline, final plan/deltas, and residual chain. Use the plan's Verification section when present; otherwise derive the smallest live scenario that covers the acceptance criteria and changed behavior.
4. Mark each testable item `PASS`, `FAIL`, `BLOCKED`, or `UNVERIFIED`, with exact evidence. Never use `SKIPPED` to dispose of an in-scope item. Evidence names the command or harness scenario, exit/verdict, and the decisive output, query, screenshot, log, or artifact location.
5. Return each failure and its evidence to the main session for Intent (`conformance | plan_delta`) and Scope (`non_structural | structural`) classification. Main sends only accepted `conformance + non_structural` work to an Opus subagent invoking `/resolve-findings`; plan deltas require user approval, and structural findings route through step 11. After any eligible fix, rerun the failed check and every regression check affected by that fix. Repeat test -> classify -> fix -> retest until they pass.
6. Treat a check requiring new authority, hardware, or external coordination as `BLOCKED`. Report the blocker to the main session for a user decision; do not acquire the authority, silently narrow the check, convert it to a follow-up, or accept a waiver. Only an explicit user revision to scope or acceptance criteria can remove the original obligation. Rebuild the ledger from the revision and verify every revised criterion.
   Record every touched non-worktree state as `unchanged`, `intentionally persisted under owner contract`, `restored`, or `residual`, with exact path, owning contract/serialization mechanism, and evidence.
7. After the last repository mutation, create the canonical final changed-file manifest relative to the fixed baseline:
	- Collect NUL-delimited paths from `git diff --name-only --no-renames -z <baseline> --` and `git ls-files --others --exclude-standard -z`.
	- Normalize separators to `/`, deduplicate, and sort paths ordinally. A rename is the old path deletion plus the new path blob.
	- For each present file, record `<path>\tblob:<oid>`, where `<oid>` is from `git hash-object --path=<path> -- <path>` so Git clean filters are applied. For each absent path, record `<path>\tDELETED`.
	- Any repository mutation that changes a path, blob hash, or deletion marker invalidates the ledger. Regenerate the manifest, rebuild the ledger against that final tree, and rerun all newly affected checks before reporting success.
8. Return `PASS` only when every in-scope testable item is `PASS` and the ledger is bound to the current manifest. Any `FAIL`, `SKIPPED`, `BLOCKED`, or `UNVERIFIED` item makes the overall result non-passing and prevents step 10 and landing.

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
- Ledger entries: criterion/behavior, exact check, `PASS` evidence
- Fix/retest rounds and repository mutations, or `none`
- Failed, blocked, skipped, or unverified items, or `none`
- Residuals: blocker or `none` (always last)

On a non-passing result, use `Verification: BLOCKED` and preserve each actual item status and evidence in the blocker list. Never emit a final-tree manifest as verified when it was generated before the last mutation.
