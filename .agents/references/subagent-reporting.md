# Subagent Reporting Contract (`be-agent-report/v1`)

Use this contract when a skill runs in a delegated subagent. It keeps the full
skill report out of the manager's context while preserving a compact,
decision-complete return value.

## Modes

- **Delegated file-backed mode:** The caller supplies a non-empty `ReportPath`.
  Treat it as a required output and write the full report there.
- **Direct inline mode:** A user or main session invokes the skill directly
  without `ReportPath`. Return the skill's full existing report inline; do not
  create a report artifact.

A delegated prompt must supply `ReportPath`. If it is missing, empty, or cannot
be written with the available tools, stop and return a `BLOCKED` compact
envelope. Do not perform the delegated work when this precondition is known to
have failed. If writing or read-back verification fails after work begins,
return `BLOCKED` and identify that failure in the envelope.

Require the canonical `ReportPath` to be contained by the current session
worktree's `Temp/AgentReports/` directory and absent before work begins. The
caller allocates a new GUID-named path for every invocation and rerun. Reports
are immutable: never append to, truncate, or overwrite an existing report.

## Full Report File

Write the complete report using the invoking skill's existing report schema.
Do not replace sections, findings, evidence, changed-file lists, or residuals
with the compact envelope. Begin every report with:

```text
Schema: be-agent-report/v1
Requested role: <model role>
Actual executor: <known executor or unknown>
Fallback: <reason or none>
Paired-review diversity: <preserved | not-preserved + reason | N/A>
Worktree: <canonical path>
Session-start baseline: <full commit or N/A>
Plan/intent: <canonical plan path, approved-delta identity, or concise task>
```

Create exactly `ReportPath`, then read it back to confirm that it exists and
contains the complete report before returning success.

`ReportPath` is a coordination artifact, normally under `Temp/AgentReports/`.
Reports under `Temp/` are ignored, non-manifest state: do not count them as
repository changes, include them in changed-file lists or content manifests,
route them as residuals, or treat them as verification inputs unless the caller
explicitly asks to read that report. The caller owns unique path allocation;
the subagent must not choose a different path or silently fall back to inline
output.

## Compact Return Envelope

After the full report passes read-back verification, return only this envelope:

```text
STATUS: PASS | NEEDS_ACTION | BLOCKED
REPORT: <exact ReportPath>
SUMMARY: <one line>
COUNTS: <lowercase-key>=<number> [<lowercase-key>=<number> ...]
INDEX:
- <ID> | <severity-or-state> | <location-or-dash> | <one-line summary>
```

Use `REPORT: missing` when delegated mode omitted `ReportPath`. Keep `SUMMARY`
to one physical line. `COUNTS` may use skill-specific keys, but must include
`residuals=<number>` and counts for every indexed item class. Use `INDEX: none`
when there are no decision-driving items or residuals.

Status meanings:

- `PASS` — work completed and the full report needs no corrective or routing
  decision. Prescribed next-step inputs such as an implementation's sweep
  handoffs or reviewer focus areas may still be indexed when their destination
  is already fixed by the workflow.
- `NEEDS_ACTION` — work and reporting completed, but a finding, failed check,
  unapproved delta, unexpected residual, or non-routine handoff requires the
  caller to choose, classify, correct, or supply missing evidence.
- `BLOCKED` — required work or required file reporting did not complete.

## Stable Decision Index

Index every item the caller must adjudicate, apply, verify, propagate, or route,
plus every residual. This includes review findings, applied fixes that change
the tree, API-verification requests, sweep handoffs, reviewer focus areas,
unresolved contradictions, and blockers. Do not index informational inventories
such as files reviewed when they require no decision.

Use a type prefix and three-digit report-order number: `F001` finding, `X001`
applied fix, `A001` API verification, `H001` handoff/focus item, `R001`
residual, or `B001` blocker. Keep assigned IDs stable while assembling one
report and append new IDs rather than renumbering earlier items. A rerun
receives a new report path and creates an independent ID sequence. Each index
entry stays on one physical line and contains enough evidence to map
unambiguously to the full report. Counts must agree with the index and the full
report.
