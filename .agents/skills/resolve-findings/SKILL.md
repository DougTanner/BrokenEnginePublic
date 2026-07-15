---
name: resolve-findings
description: >-
  Resolve an explicitly accepted review finding, compile failure, runtime
  failure, or post-fix verification assignment within the Broken Engine C++
  Code Change Process. Use this skill for delegated fix work after the main
  agent has adjudicated a finding or supplied concrete failure evidence, and
  for focused verification of fixed regions. Confirms root cause
  before editing, stays inside the assigned scope, verifies each fix, and
  reports exact changed regions and residuals. Supports explicit fix and
  independent-verify modes; independent-verify mode never edits.
allowed-tools: [Read, Grep, Glob, Edit, Agent, "Bash(git diff *)", "Bash(git status *)", PowerShell]
---

# Resolve Findings

Resolve only the accepted failures assigned by the caller. The main agent owns finding adjudication and scope decisions; this skill owns root-cause validation, the smallest justified fix, and scoped verification.

## Inputs

Require the caller to provide:

- **Mode**: `fix` or `independent-verify`
- Caller classification: **Intent** (`conformance` or `plan_delta`) and **Scope** (`non_structural` or `structural`)
- Source report compact-envelope identities (`REPORT`, `REPORT_SHA256`) and the stable IDs of each accepted finding or failure, with exact evidence locators and dependencies; invoke `Read-AgentReportSection.ps1` once per exact range under the shared [`be-agent-report/v1`](../../references/subagent-reporting.md) consumption contract to load the affected region, evidence, and expected behavior
- Plan document or concise intent summary
- Assigned file/function scope and session-start baseline
- Required verification, if the process already prescribes one
- For a delegated call, an absolute caller-assigned `ReportPath` under the session worktree's `Temp/AgentReports/`

If an input is missing, reconstruct it from the prompt and current worktree only when unambiguous. Otherwise report the item unresolved rather than choosing new scope or intent.

Fix mode accepts only `conformance + non_structural`. Return `PLAN DELTA REQUIRED` without editing if the fix would change approved behavior, scope, acceptance criteria, or verification obligations. An in-scope structural acceptance failure blocks the active change; only proven pre-existing or out-of-scope structural work routes to a follow-up plan. If the user explicitly expands current scope, main updates the canonical plan and sends the work back through `/implement-plan`; structural work never enters fix mode. Delegation is client-neutral: use a self-contained fresh Claude prompt or Codex `fork_turns:"none"`, preserving baseline, classification, and residuals.

The default correction budget is one fix wave followed by focused review and
retest of the changed regions and directly affected checks. A later wave
requires a blocker that remains reproducible after that verification; scope it
only to the regions and checks affected by the blocker. Do not restart general
review or verification to seek consensus.

## Fix Mode

For each assigned item:

1. State the suspected root cause in falsifiable terms.
2. Confirm it through direct code inspection or failure evidence. Read enough callers, callees, logs, or sibling paths to distinguish the root cause from a downstream symptom. A compile error identifies a failing expression, not necessarily the originating change.
3. If the evidence refutes the supplied diagnosis but proves a different in-scope cause, report the corrected cause before editing. If the cause remains uncertain, contradicts the plan, needs an architectural/user decision, or lies outside assigned scope, do not edit that item; return it as a residual with the evidence gathered.
4. Apply the smallest change that restores the plan's intended behavior. Do not refactor adjacent code, clean up pre-existing issues, or broaden the accepted finding.
5. Re-read the complete fixed region and its directly affected call path. Verify that the original failure scenario no longer follows from the code.
6. Run the caller-prescribed check. When none is prescribed, select the narrowest meaningful check: selective `/compile` for changed C++ regions, the relevant existing command for tooling/docs changes, or the exact `/agent-harness` scenario that reproduced a runtime failure. Delegate `/compile` to a Sonnet subagent and preserve its status plus error and warning lines verbatim; keep diagnosis and edits in this context. Do not silently substitute a weaker check.

Fix only errors introduced by this assignment's edits during verification. Report unrelated or structurally larger failures as residuals.

## Independent-Verify Mode

Treat the worktree as read-only. Do not use Edit, apply formatters, regenerate tracked files, or repair a failed check.

For each assigned fix:

1. Read the accepted finding, fixed region, and enough surrounding flow to restate the original failure scenario.
2. Attempt to reproduce the finding against the current code. Confirm that the fix addresses the proven cause rather than masking its symptom.
3. Run only the targeted build/runtime/static check assigned by the caller. Delegate builds to a Sonnet `/compile` subagent and preserve its status plus error and warning lines verbatim. Build artifacts are permitted; tracked-file changes are not.
4. Return one verdict:
   - `VERIFIED` — the original scenario is prevented and the scoped check passes.
   - `REFUTED` — the failure remains, the fix introduces a concrete replacement failure, or the scoped check fails because of the fixed region.
   - `UNRESOLVED` — required evidence or environment is unavailable.

Do not expand into a general review. A new issue outside the fixed regions is a residual, not a finding to fix.

## Authority and Boundaries

Use this intent order when sources disagree: explicit user statement, final approved plan plus approved deltas, AGENTS.md/docs/comments, current behavior. Name any contradiction and which source controls; do not silently reconcile it.

Treat repository-internal inputs as valid unless the finding concerns a trust boundary. Never remove working behavior to make a check pass without explicit user approval.

Do not verify disputed external API or documentation claims from memory. Return them for `/verify-external-claims` unless the caller already supplied an authoritative result.

## Report

Follow [`../../references/subagent-reporting.md`](../../references/subagent-reporting.md).
For a delegated call, require `ReportPath`, write this complete structure there,
and return only the compact indexed envelope. Keep mode, every item verdict,
tracked-file mutation state, `PLAN DELTA REQUIRED`, structural routing, failed
verification, and residual owner/action visible in the envelope. With no
delegated `ReportPath`, retain the complete inline report:

```markdown
## Finding Resolution

Mode: fix | independent-verify

### Item Results
- <item>: <FIXED | VERIFIED | REFUTED | UNRESOLVED>
  - Root cause: <suspected cause and confirming/refuting evidence with file:line or log line>
  - Change: <smallest fix, or none>
  - Verification: <command/scenario and exact pass/fail evidence>

### Files Changed and Regions Touched
- <path> — <function/region>
- none

### Residuals
- <unresolved/out-of-scope item, evidence, and required next owner/action>
- none
```

List `Files Changed and Regions Touched: none` in independent-verify mode. Do not report success for an item whose required verification was skipped; mark it `UNRESOLVED` and explain why.
