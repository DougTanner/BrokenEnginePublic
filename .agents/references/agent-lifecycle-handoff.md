# Agent Lifecycle Handoff (`be-agent-lifecycle-handoff/v1`)

Use this immutable handoff when the runtime signals compaction. It preserves
the current lifecycle without claiming that a new context inherited identity
or memory.

## Safe-stop rules

Finish only the current atomic operation, stop at the next non-mutating safe
boundary, and write a new GUID-named handoff under the live worktree's
`Temp/AgentReports/`. The path must be absent and the file is write-once. After
read-back, stop repository, queue, claim, build, and external-state mutations.

A main context resumes with `/clear` or a fresh task in the same live wrapper
and worktree. Do not start a new wrapper, adopt another wrapper's claim, or add
project hooks/resume modes. A compacted delegate is terminated at the safe
boundary and may be restarted once for the same assignment with this handoff
and a newly allocated report path. Before continuing, the replacement audits
all existing assigned-scope edits against the fixed baseline and handoff dirty
hashes. A second compaction or a mismatch is an indexed blocker for the manager.

## Required file schema

```text
Schema: be-agent-lifecycle-handoff/v1
Signal: <main-compaction | delegate-compaction | host-specific signal>
Created UTC: <RFC 3339 timestamp>
Worktree: <canonical worktree path>
Primary checkout: <BROKEN_ENGINE_PRIMARY_CHECKOUT or unknown>
Session branch: <BROKEN_ENGINE_SESSION_BRANCH or current branch>
Target branch: <BROKEN_ENGINE_TARGET_BRANCH or unknown>
Session owner: <BROKEN_ENGINE_AGENTCLI_SESSION_OWNER or unknown>
Session-start baseline: <BROKEN_ENGINE_BASELINE or full fixed commit>
Lifecycle phase: <root process step and substep>
Safe boundary: <last completed atomic operation; no mutation in progress>
Assignment: <main lifecycle or exact delegated assignment>
Approved plan: <canonical path and immutable identity>
Approved deltas: <complete approved-delta identity or none>

## Dirty file identity
<one row per dirty path: state<TAB>repo-relative path<TAB>byte-count-or-`deleted`<TAB>lowercase-SHA-256-or-`deleted`>

## Live claims and agents
<AgentCli session/queue/row/maintenance claims and live agent IDs/statuses, or none>

## Immutable reports
<one row per report: path<TAB>lowercase SHA-256<TAB>IDs<TAB>evidence locators>

## Residuals
<stable residual/blocker IDs with exact routing, or none>

## Exact next action
<one executable next action, including skill/assignment, inputs, and new ReportPath if delegated>
```

All paths and hashes describe current on-disk state; prose recollection is not
a substitute. A tracked path absent from disk uses exactly
`D<TAB>repo-relative path<TAB>deleted<TAB>deleted`; `D` is the lifecycle schema's
tracked-deletion state, not an empty-file identity. The replacement verifies
that every `D` path is absent and Git still reports it as a tracked deletion,
in addition to verifying worktree provenance, the five wrapper environment
values when available, existing dirty-file hashes, live claims, and every
report hash before acting. Any mismatch stops mutation and returns an indexed
blocker.

## Waiting and deadlines

Compaction does not change job deadlines. One orchestration wait and the user
update interval are at most 60 seconds, but a poll expiry is not a terminal
condition. Skill-owned build, shell, AgentCli serialization, and lock deadlines
remain authoritative. Terminate only on an explicit terminal/deadline condition
with no usable immutable report.
