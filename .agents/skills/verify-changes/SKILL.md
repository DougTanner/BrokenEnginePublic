---
name: verify-changes
description: >-
  Verify final-evidence-gate changes — queue mutation or completion,
  reconciliation, requested primary commit or landing, shared build/bootstrap
  work, or Tier-3 integration — with a final-tree acceptance table. Work
  without such a gate uses proportionate direct checks and an inline completion
  summary instead.
allowed-tools: [Read, Grep, Glob, Agent, "Bash(git diff *)", "Bash(git status *)", "Bash(git ls-files *)", PowerShell]
---

# Verify Changes

Use this skill only when a final-evidence gate (root `AGENTS.md` definition)
applies. Work without such a gate — any tier — reports its direct checks,
changed files, and residuals inline without a final-tree acceptance table.
Completed `/next-plan` routes also follow the [canonical execution-gate
contract](../next-plan/references/execution-gates.md).

## Inputs

Require:

- Absolute adopted worktree path and fixed session-start baseline commit; the primary checkout is valid only for an explicitly user-authorized `primary-commit`
- Final approved plan and caller-supplied approved-delta summary (`none` is valid)
- Manager execution-control record: approved risk tier and concrete triggers,
  required and conditional roles, triggered-role dispositions, and the initial
  acceptance matrix
- Accumulated inline implementation, review, hygiene, build, and residual handoffs with their decisive checks

If lifecycle identity or required evidence is missing or ambiguous, return `BLOCKED`; do not rediscover a movable baseline or invent acceptance criteria.
Block when any handoff shows an unapproved plan delta. Only main may apply an exact user-approved delta before verification.

## Workflow

1. Require the current checkout to be the adopted worktree and the supplied baseline to be its fixed session-start commit. Inventory the final diff from that baseline, including untracked files.
2. Build an acceptance table from the final approved plan plus approved-delta
   summary, the execution-control record, every changed behavior visible in the
   diff, and every test obligation or residual in supplied handoffs. Every row
   has exactly these decision fields:
   `criterion/behavior -> decisive check -> expected result -> independent signal
   if this duplicates another check (otherwise none) -> status/evidence`.
   One decisive check may cover several criteria. Do not duplicate a check unless
   the row names the distinct independent signal it adds.
   The table also carries exactly one row per required review from the
   execution-control record: skill name, `delegated|inline`, findings count, and a
   one-word disposition (`accepted-fixed | refuted | mixed | none`) — recorded only, never
   a new validation stage or reason to re-run a review. Documentation, skill,
   tooling, and configuration changes require the strongest applicable static or
   functional check; they do not receive an automatic runtime skip.
   When the diff contains a changed `.agents/skills/*/SKILL.md`, invoke `/validate-skill` once for each changed skill and record its complete decisive result. That shared workflow is the only skill-validation contract: do not substitute a client-installed validator or abbreviated frontmatter check. Require `Validation: PASS`; `FAIL`, `BLOCKED`, `INVALID`, `SETUP_ERROR`, result/exit mismatch, or inability to run the validator makes the table non-passing.
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
6. Treat a check requiring new authority, hardware, or external coordination as `BLOCKED`. Report the blocker to the main session for a user decision; do not acquire the authority, silently narrow the check, convert it to a follow-up, or accept a waiver. Only an explicit user revision to scope or acceptance criteria can remove the original obligation. Rebuild the table from the revision and verify every revised criterion.
   Record every touched non-worktree state as `unchanged`, `intentionally persisted under owner contract`, `restored`, or `residual`, with exact path, owning contract/serialization mechanism, and evidence.
7. When the final tree changes an executable plan file (the queue itself is machine-local state and never appears in a git diff), set `$WorktreeCli` to the current checkout's provisioned `Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe`. In a session worktree, run `plan order validate --repo <canonical-git-common-dir> --worktree <session-worktree>` and require exit `0`, JSON `ok: true`, and no diagnostics. A `missing-plan-file` notice for a foreign row whose plan landed on primary after the session baseline is the expected stale-baseline condition (execution-gate contract, state 3): record it as evidence, never as a failure or blocker. A completed-plan row claim remains intentionally owner-held until finalization/landing: record its owner-qualified `ownedByRequester: true` status, not an absence expectation. When calling the row API directly, use the plan key relative to the queue directory. Otherwise record `plan-order validation: not triggered — no executable-plan change`; do not run it merely because another repository file changed.
8. Return `PASS` only when every in-scope testable item is `PASS`. Any `FAIL`, `SKIPPED`, `BLOCKED`, or `UNVERIFIED` item makes the overall result non-passing and prevents landing. Once the complete matrix passes, stop: do not add exploratory variants, consensus reruns, or unrelated checks. Loop only when changed bytes invalidate earlier evidence or a decisive check fails. A later conflict-free rebase onto an advanced primary does not invalidate this result — `/finalize-changes` owns reconciliation and its overlap check.

## Output

Follow [`../../references/subagent-reporting.md`](../../references/subagent-reporting.md).
Return the complete result inline:

- `Verification: PASS | BLOCKED`
- Adopted worktree and fixed baseline
- Changed files, one per line (`git status`-derived; no hashes required)
- Acceptance-table entries: criterion/behavior, decisive check, expected result,
  independent signal for any duplicate (otherwise `none`), status, and exact
  evidence
- One row per required review: skill name, `delegated|inline`, findings count,
  one-word disposition
- Session `plan order validate` result when triggered
- Fix/retest rounds and repository mutations, or `none`
- Failed, blocked, skipped, or unverified items, or `none`
- Residuals: blocker or `none` (always last)

On a non-passing result, use `Verification: BLOCKED` and preserve each actual item status and evidence in the blocker list.
