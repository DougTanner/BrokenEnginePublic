# Bound Agent Context and Truthful Capability Reporting

## Context

Audited sessions repeatedly loaded complete reports and large workflow entry
points, while some delegated reports described requested Fable/Opus/Sonnet
diversity as actual execution even though the exposed spawn surface had no
named-agent selector or runtime-model metadata. File-backed reports already
preserve evidence; the missing pieces are immutable bounded locators, truthful
host-attested execution fields, and a safe lifecycle handoff on compaction.

## Design

1. Extend `be-agent-report/v1` with truthful requested-role/profile, selection status, configured model, actual executor/runtime, capability, fallback, and diversity fields. Only host/runner metadata may attest the actual model. Configuration and self-report are not attestation. Unsupported surfaces continue in fresh role-prompted contexts marked unknown/unselectable and not-diverse.
2. Normal Codex delegation requests `fable`, `opus`, or `sonnet` only when the surface exposes named selection. Preserve the existing `codex-review` explicit Sol headless pin as a documented surface-specific exception.
3. Extend compact envelopes with `EXECUTION`, `REPORT_SHA256`, and indexed evidence locators using `evidence=L<start>-L<end>[;L<start>-L<end>...]` plus `depends=<IDs>|none`. Downstream packets carry report path, hash, IDs, and locators rather than report bodies.
4. Add `.agents/scripts/Read-AgentReportSection.ps1` with `-ReportPath`, `-ExpectedSha256`, and one exact `-Range`. Require canonical containment under the current worktree's `Temp/AgentReports`, no reparse escape, exact immutable hash, strict UTF-8/range validation, and a 16 KiB per-range maximum that fails instead of truncating.
5. Propagate helper use to every report consumer: next-plan, external-grill-plan, implement-plan, update-affected-code, repo-code-review, adversarial-review, glsl-review, code-style-review, resolve-findings, session-audit, create-follow-up-plans, verify-changes, and finalize-changes. `/verify-changes` indexes the complete authoritative manifest and PASS ledger; `/finalize-changes` consumes every corresponding bounded range without weakening its safety gate.
6. Require fresh context for every distinct assignment. Reuse is limited to the same assignment's focused correction/retest. Duplicate identical prompts are permitted only after failure before a usable immutable report exists.
7. Add `be-agent-lifecycle-handoff/v1`: worktree provenance, lifecycle phase/safe boundary, approved plan/deltas, dirty-file hashes, live claims/agents, report path/hash/IDs, residuals, and exact next action. Main compaction writes the handoff, stops mutations, and resumes via `/clear` or a fresh task in the same live wrapper/worktree. A compacted delegate is terminated and restarted once with the handoff and a new report path; existing edits are audited first. Do not add project hooks or wrapper resume/adopt mode.
8. Define 60 seconds only as the maximum orchestration wait and user-update interval. Poll expiry is not a job failure. Terminate only on an explicit terminal/deadline condition with no usable report; retain skill-owned build/lock deadlines.
9. Remove the old plan's unconditional five-skill progressive-disclosure rewrite. Make only the minimum report-consumer edits required by this contract; do not split skills for size.
10. Create one tracked normalized/redacted fixture corpus shared with `LinearRiskTieredCppChangeProcess`. Freeze the current retained report sets using ordinal UTF-8 manifest rows `<filename>\t<byte-count>\t<file-sha256>\n`:
    - DataPacker `d730205ab92e91b48f46d29076f12165dde8dda5`: 21 reports, 112270 bytes, manifest SHA-256 `4655da5ac8ffc7db53eed3e8e8f58872f7bb9047f7677a103ecab3dde4b0fa33`.
    - PREfast `98b5272c0cb836b822cc5692484107768426b103`: 51 reports, 328170 bytes, manifest SHA-256 `543e943a887335cafaaf75dfc573d04f544483ad49f7b254c7aa1a69a9508597`.
    - Save `90f7ed3450b9318d89b3de74cb7e1271f12789ce`: 48 reports, 362650 bytes, manifest SHA-256 `7b462e9a720441dff0316c7c6fe1d8b6d70233df29c8d7e074b61aefc09f3120`.
11. Preserve and semantically reconcile the current dirty baseline plus every `Order.md` overlap warning for scope/queue, canonical-finalization, deterministic-queue, and linear-risk plans; none is a prerequisite.

## Verification obligations

- Positive extraction fixtures cover exact and multi-range evidence, complete verify manifest/PASS ledger handoff, and all decision-driving items/residuals from the three normalized session corpora without whole-report ingestion.
- Negative fixtures cover wrong hashes, missing reports, malformed/out-of-bounds ranges, path/reparse escapes, oversized ranges, self-reported identity, and completed delegates without reports.
- Simulate main/subagent compaction and a delegate running longer than 60 seconds within its valid deadline.
- Smoke named agents only on a surface that exposes selection plus host metadata; otherwise prove the truthful unknown/unselectable branch.
- Run PowerShell parser checks, `git diff --check`, link checks, and `/validate-skill` for every changed skill.

## Critical files

- `AGENTS.md`; `.agents/references/subagent-reporting.md`; `.agents/references/agent-lifecycle-handoff.md`; `.agents/scripts/Read-AgentReportSection.ps1`.
- The listed report-consumer skills and their narrowly required references.
- `.agents/references/reporting-fixtures/`, shared with `Agent/LinearRiskTieredCppChangeProcess.md`.
- `.codex/agents/{fable,opus,sonnet}.toml` remain configuration owners and are not rewritten.

## Out of scope

- Engine/runtime/data-format changes, broad skill splitting, project hook configuration, wrapper adoption, or unit tests.
- Hiding evidence, truncating evidence, weakening finalization gates, or claiming prompt/configuration identity as runtime attestation.

## Acceptance criteria

- Reports and envelopes satisfy the truthful execution, immutable hash, bounded locator, dependency, and mandatory finalization-evidence contracts.
- Distinct assignments use fresh contexts; compaction produces the specified immutable handoff and safe stop; waits longer than 60 seconds are expressed as repeated orchestration polls without changing valid job deadlines.
- The shared fixtures preserve the approved frozen identities and cover every indexed finding/residual without requiring whole-report ingestion.
- Parser, positive/negative extraction, link, diff, and changed-skill validation checks pass.

## Notes

- Developer workflow only. Preserve the current dirty baseline and all warning-only queue overlaps.
- Score: Effort 3, Impact 4, Risks 1, total 0; Tier Medium.
