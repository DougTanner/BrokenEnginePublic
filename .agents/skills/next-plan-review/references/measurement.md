# Control-Work Measurement and Execution-Model Routing

Read this reference when assessing control-work share and execution-model
routing for [`/next-plan-review`](../SKILL.md).

## Measure control-work share

Classify each transcript-observable active interval exactly once as
`control work`, `actual work`, or `unattributed`. Control work is work whose
immediate object is a workflow-control artifact: creating, reading, reconciling,
validating, explaining, or coordinating execution cards, claims, locks, the
landing summary/confirmation, or workflow routing — dispatch, claim, lock, and
landing routing only — including workflow-control artifact classes that existed
at the reviewed commit but have since been removed from the workflow. Actual
work is engineering or repository work directly delivering or validating the
governing objective: investigation, implementation, propagation, debugging,
build/harness setup or result analysis, and substantive review/testing.
Engineering planning or coordination whose immediate object is delivery or
validation of the governing objective is actual work, and routing counts as control
work only when its immediate object is workflow control rather than task
delivery. Split an evidenced mixed interval; otherwise classify it as
`unattributed`.

Measure non-overlapping active intervals within each agent and sum those
per-agent intervals as active agent-time; it is not wall-clock time. Exclude
explicit user/external pauses and passive waits from active agent-time, while
retaining the overall timing disclosure required by `## Reconstruct and assess`
in [`SKILL.md`](../SKILL.md). Use only cited timestamps or
event ranges, tool runtimes, and Git/tool evidence; never infer private
reasoning. For each category, report time, share, coverage, confidence, and a
range when evidence is sparse.

Let `T = control work + actual work + unattributed` observed active agent-time.
Category shares use `T`; coverage is `(control work + actual work) / T`. For a
category-ambiguous interval, the lower control-work bound assigns all of it away
from control work and the upper bound assigns all of it to control work. Show an
exact point control-work share only when the evidence supports it. When `T = 0`,
report the measure as `unverified`, not a division.

The control decision is independent of the time category: separately label each
control as `required safety/control`, `candidate removable`, or `unverified`.
Required control work remains control work, but is not automatically waste.
Preserve the unchanged-input/non-firing safeguards: a removal or consolidation
recommendation requires measured cost, frequency, unique signal, and its safety
tradeoff; one quiet run is not removal evidence.

Assess core-delegation compliance with concrete evidence: manager-only core
activity; one manager with a single level of workers below it; one scoped
worker per concern; prohibited duplicate search, restatement, or consensus
work; artifact-path-plus-selector evidence forwarding rather than raw
forwarding; and capsule/resume recovery rather than repetition of completed
work. Mandatory fresh review, independent verification, and required disjoint
fan-out are legitimate independent work,
not duplicate effort. A compliance finding cites the delegation record,
session ID and timestamp or event/line location, artifact selector, or concrete
repeated operation.

## Verify execution-model routing

For every routing-inventory row, classify the actual assigned task before
considering its role label: `planning/design`, `review/audit`,
`implementation/propagation/documentation`, `judgment-heavy research`,
`locate/build/mechanical`, or `unable to classify`. Map that concern to the
appropriate workflow role, then evaluate the requested, configured, and actual
model and effort against the commit-time root `AGENTS.md` mapping and fallback,
not the requested role alone. When the assigned task is unable to classify, or
the governing mapping cannot be established, do not infer compliance.

Use only these allowed evidence chains from claim to conclusion. An ordinary
Claude child is compliant only with its parent delegation event, recorded
returned child relationship, and child-session execution metadata naming the
actual executor/model and effort. A headless `/codex-review` is compliant only
with its parent wrapper
invocation/result, the commit-time `.codex/codex-review.ps1` explicit model and
effort pins, and fixed structured output. A requested role, explicit requested
model/effort, or configured mapping proves intent only; when required model or
effort evidence cannot be proved, the verdict is `unverified`. Record the parent
event and child or headless route; relationship evidence; actual concern;
requested role/type and explicit model/effort; commit-time configured
model/effort mapping; actual executor/model/effort proof; fallback evidence;
verdict; exposed tokens/active time; and citation. Aggregate affected-child
counts and token/active-time cost only
where exposed; otherwise report cost as unavailable.

Verdicts are `compliant`, `compliant fallback`, `violation`, `unverified`, or
`not-executed`. `not-executed` is a nonfinding only when the parent event/result
conclusively proves the dispatch failed before any executor started. A started
child later aborted or interrupted still needs normal actual-model proof and a
normal verdict. Every `violation` or `unverified` row is a cited finding. Prefer
a routing mechanism or evidence fix before reminder prose; no routing result is
automatically P0.
