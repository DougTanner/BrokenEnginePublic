# Subagent Reporting Contract (`be-agent-report/v1`)

Use this contract for delegated skill work. It keeps complete evidence in an
immutable file while giving managers a bounded, decision-complete packet.

## Modes and report lifetime

- **Delegated file-backed mode:** the caller supplies a non-empty `ReportPath`.
  The path must be absent, GUID-named, and canonically contained by the current
  worktree's `Temp/AgentReports/` directory without crossing a reparse point.
- **Direct inline mode:** no `ReportPath` was supplied. Return the skill's full
  existing report inline and do not create a report artifact.

A missing, invalid, pre-existing, or unwritable delegated path blocks work.
Create exactly the supplied file once, never append to or overwrite it, then
read it back before returning. After read-back, compute its lowercase SHA-256;
that hash binds every evidence locator in the compact envelope.

Reports under `Temp/` are ignored coordination state. Do not count them as
repository changes, content-manifest entries, or verification inputs unless a
caller explicitly supplies the report path, hash, and required indexed IDs.

## Truthful execution header

The full report starts with these fields in this order:

```text
Schema: be-agent-report/v1
Requested role: <workflow role>
Requested profile: <named custom-agent/profile or none>
Selection status: <selected | unavailable | not-requested | failed>
Configured model: <profile-configured model, unknown, or none>
Actual executor: <host-attested executor or unknown>
Actual runtime/model: <host-attested model identifier or unknown/unselectable>
Host attestation: <tool-returned metadata | trusted host hook | none>
Capabilities: <selection/delegation/runtime limitations material to this task>
Fallback: <fallback and reason or none>
Paired-review diversity: <preserved | not-preserved + reason | N/A>
Worktree: <canonical path>
Session-start baseline: <full commit or N/A>
Plan/intent: <canonical plan, approved-delta identity, or concise task>
```

`Requested role` is process intent. `Requested profile` and `Configured model`
describe configuration, not execution. `Selection status: selected` requires
host/runner metadata naming the selected profile. `Actual executor` and `Actual
runtime/model` require host/runner metadata returned by the delegation tool or
a trusted host hook; prompts, agent prose, report self-identification, and
`.codex/agents/*.toml` are never attestation. Without trusted metadata, use
`unknown` or `unknown/unselectable` and `Host attestation: none`.

Paired-review diversity is `preserved` only when trusted metadata proves the
required independent runtime/profile mapping. Unknown or same-model execution
is `not-preserved` with the reason. An unsupported surface may still run a fresh
role-prompted context when the owning workflow permits it, but must report
selection as `unavailable` and must not claim that a project profile was used.
The `codex-review` explicit Sol headless pin is a documented surface-specific
exception, not evidence that normal named-profile selection exists.

## Compact return envelope

After successful read-back and hashing, return only:

```text
STATUS: PASS | NEEDS_ACTION | BLOCKED
REPORT: <exact ReportPath>
REPORT_SHA256: <64 lowercase hexadecimal characters>
EXECUTION: requested_profile=<name|none> selection=<selected|unavailable|not-requested|failed> configured_model=<name|unknown|none> actual_runtime=<host-attested identifier|unknown/unselectable> attestation=<tool-returned|trusted-hook|none>
SUMMARY: <one physical line>
COUNTS: <lowercase-key>=<number> [<lowercase-key>=<number> ...]
INDEX:
- <ID> | <severity-or-state> | <location-or-dash> | <one-line summary> | evidence=L<start>-L<end>[;L<start>-L<end>...] | depends=<comma-separated IDs>|none
```

Use `REPORT: missing` and `REPORT_SHA256: unavailable` when delegated mode did
not receive or could not create a report. `COUNTS` includes `residuals` and a
count for every indexed class. Use `INDEX: none` only when no item needs later
adjudication, application, verification, propagation, or routing.

Status meanings:

- `PASS`: work completed with no corrective/routing decision outstanding.
  Prescribed next-step handoffs may still be indexed.
- `NEEDS_ACTION`: work completed, but a finding, failed check, unapproved delta,
  unexpected residual, or non-routine handoff needs a caller decision.
- `BLOCKED`: required work or required immutable reporting did not complete.

## Stable decision index and locators

Index every decision-driving item and residual. Prefix report-order IDs with
`F` (finding), `X` (applied change/fix), `A` (external API verification), `H`
(handoff/focus), `R` (residual), or `B` (blocker), followed by three digits.
Keep IDs stable while assembling one report; reruns use a new report path and
new independent ID sequence.

`evidence=` contains one or more exact, inclusive line ranges in ascending
order. The only range grammar is `L<positive integer>-L<positive integer>`;
separate multiple ranges with semicolons. Each range must contain the complete
evidence it claims, must be no larger than 16 KiB in the report's original
UTF-8 bytes, and must not rely on truncation. Split larger evidence across
additional ranges. `depends=` is `none` or the comma-separated IDs whose cited
evidence must also be read to decide this item. Dependencies are report-local
unless the packet explicitly includes another immutable report path/hash.

Consumers require the compact envelope's `REPORT`, `REPORT_SHA256`, requested
indexed IDs, `evidence` locators, and dependency IDs. Resolve dependencies, then
invoke [`Read-AgentReportSection.ps1`](../scripts/Read-AgentReportSection.ps1)
once for each exact range, passing the envelope's report path and SHA-256. Keep
each invocation's output as one string, including its original line terminators;
do not pipe it through line-oriented readers, join line arrays, or reserialize
it. PowerShell consumers capture the invocation directly in-process. Bash or
other native-process consumers invoke `pwsh -Command`, capture the helper result
inside that PowerShell process, and emit it with `[Console]::Out.Write($section)`;
invoking the helper directly with `pwsh -File` is not lossless because the host
appends an output-record terminator. Consumers do not read the whole report by
default. The helper fails closed on path, reparse, hash, UTF-8, range, or size
errors and never truncates.

An orchestration wait or poll is at most 60 seconds. Poll expiry means only that
no update arrived during that interval; it is not a report, delegate, skill, or
job deadline. Continue polling while the applicable skill-owned deadline and
terminal conditions permit, and terminate only on an explicit terminal/deadline
condition with no usable immutable report.

Fixture-driven consumer checks use tracked `decision-catalog.tsv` files only as
coverage inventories. Copy each selected normalized fixture report into the
current worktree's `Temp/AgentReports/`, hash that copy, construct its exact
ranges there, and consume it through the same helper path. A tracked normalized
copy is test input, never an authoritative process report.

Informational inventories need not be decision-indexed. Mandatory safety-gate
artifacts are the exception: `/verify-changes` indexes bounded ranges covering
its complete authoritative final-tree content manifest and complete PASS
ledger, even when every row passes. `/finalize-changes` must consume every one
of those ranges before landing; bounded extraction must not weaken that gate.
