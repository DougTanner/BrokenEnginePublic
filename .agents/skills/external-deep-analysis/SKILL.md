---
name: external-deep-analysis
description: >-
  Run a two-phase C++ analysis pipeline on a file or directory: architecture
  review for cross-file shape, then refactor-clean for in-function mechanics.
  Verify findings, route proven debt through the repository follow-up-plan and
  finalization workflows, and report an area debt score. Use only when the user
  explicitly requests a deep or full code analysis; never select it for routine
  changes or general code questions. Security auditing is excluded.
disable-model-invocation: true
allowed-tools: [Read, Write, Edit, Grep, Glob, Agent, Bash, PowerShell]
---

# Deep Analysis Pipeline

Run the two native analysis skills in order, verify their findings independently,
then hand proven residuals to the repository's plan-authoring and finalization
workflows. Do not implement source fixes during this analysis.

## Scope and Execution Context

Require a target path. For a directory, default to files directly within it;
include descendants only when the user explicitly requests recursion. State the
resolved mode and exact target in every phase invocation. An exact-file target
remains exact-file scope.

Run from the main invoking context. If repository policy prevents the current
context from dispatching, return a main-context dispatch requirement instead of
approximating either analysis inline. The main context invokes skills and
dispatches every role. Give each role a scoped manifest or report slice and
forbid child delegation; schedule rounds within the host's available concurrency.

Read root and target-applicable `AGENTS.md` files before dispatch. Plan authoring
needs the checkout for `create-follow-up-plans`.

## Phase 0: Scoped Metric Evidence

Before architecture analysis, invoke `code-quality-metrics` Snapshot once with
the resolved relative-POSIX target, the same `Exact`, `Directory`, or
`Recursive` scope mode, and the absolute checkout root. This single capture
analyzes the complete corpus and the resolved target together; do not substitute
separate corpus and target runs. Call the digest wrapper `pwsh -NoProfile -File
.agents/skills/code-quality-metrics/scripts/Get-CodeQualityEvidence.ps1 -Mode
Snapshot -Target <resolved-relative-POSIX-path> -Scope <resolved-mode>
-RepositoryRoot <absolute-checkout-root> -Phase0Hints`, and consume its
`profile`, `targetSelection`, `coverage`, and `hints` fields. This main context
may add `-EvidenceDirectory` with an absolute ignored `Temp/` path to retain the
full report, which the digest names in `evidencePath`.

An operational failure blocks the pipeline and emits one error envelope with null
digest fields: exit `1` carries `evidence.contract-mismatch` naming the exact
field path for a contract violation, or `internal.error` for any other unexpected
operational error, and exit `2` forwards the entry point's own error verbatim in
`underlying`. Treat
every reported parse omission as an explicit metric residual, not a pipeline
failure.

`hints` arrives already ordered, truncated, and counted in four categories —
target file and area outliers, target-intersecting clone groups, target
high-complexity functions, and target skips — each stating its `total` and
`emitted` counts, including zeroes. Forward exactly these hints to both native
analysis phases as received; never reconstruct their selection, ordering, or
truncation inline.

The forwarded hints are scoped evidence to inspect, never findings: they do
not expand the original target, create Plans, alter the Debt Score, or replace
source inspection. Retain corpus coverage and all parse-omission residuals for
the final summary even when only target omissions are forwarded.

## Phase 1: Architecture Shape

Invoke `external-architecture-review` natively through the normal skill surface
with the resolved target and scope mode. Include the Phase-0 scoped hints as
investigation evidence without expanding the target. Do not open a client-specific
skill installation or reproduce its workflow here.

Retain its scoped manifest, authorities, verified findings, external-claim
residuals, and handoff note for Phase 2. Do not choose plan locations, filenames,
groups, collisions, dependencies, or duplicate decisions at this stage.

## Phase 2: In-Function Mechanics

Invoke `external-refactor-clean` natively through the normal skill surface with
the same target and scope mode. Include the Phase-0 scoped hints and Phase-1
investigation paths as evidence to inspect, without expanding the original
finding boundary.

Keep its file-size triage separate from ordinary findings. Every oversized file
retains the explicit instruction `run /reduce-file <path>`; do not analyze it
inline or silently convert it into a generic refactor item.

## Phase 3: Findings Verification

After both reports complete, have the main context dispatch fresh `reviewer`
roles in scoped slices. Reviewers are findings-only: they may read the scoped
source, authorities, and reports supplied by the main context, but must not
write, rewrite, delete, score, or relocate a plan and must not delegate.

For every candidate, require a verdict and evidence for:

1. Correctness — cited path, symbol, behavior, and repository authority exist
   and support the claimed root cause.
2. Benefit and safety — the proposed outcome is functional or structural,
   not cosmetic, pattern-breaking, or more risky than the proven debt.
3. Actionability — the unmet acceptance criterion and smallest correction
   boundary are concrete enough for a plan author.
4. External claims — non-obvious API, specification, license, maintenance,
   or ThirdParty propositions become verification requests for the main context
   to route through `verify-external-claims`; unresolved claims remain residuals.

The main context decides on reviewer evidence once. Drop disproven candidates,
retain verified caveats, and preserve each accepted finding's originating phase,
symbols, evidence, invariant exposure, and unmet acceptance criterion. Keep
oversized-file items labeled as `/reduce-file` follow-ups.

## Phase 4: Route, Verify, and Land

Analysis findings are pre-existing or out-of-scope debt, never permission to fix
code in this workflow. Route every accepted candidate through
`create-follow-up-plans`, passing oversized files with their required
`/reduce-file <path>` instruction intact. Acceptance and landing then follow root
`AGENTS.md` Change Workflow Steps 7 and 8 through `verify-changes` and
`finalize-changes`.

## Summary

After successful verification and the applicable finalization outcome,
report:

- exact target, scope mode, file count, and applicable authorities;
- metric profile, target and corpus coverage, scoped-hint total/emitted
  counts, and every metric residual;
- architecture and refactor-clean finding counts;
- created, updated, duplicate-mapped, rejected, and residual items, with every
  oversized file still shown as `run /reduce-file <path>`;
- each authored plan's final tier trigger, acceptance boundary, and dependency
  outcome from `create-follow-up-plans`;
- one area Debt Score: LOW for nearly all Quick Win/Small, MODERATE for
  mostly Small/Medium, HIGH for multiple Large or any Architectural, and
  CRITICAL for several Architectural items or a core-invariant threat;
- verification result, finalization outcome, and tracked Plan validation state.

The Debt Score is a run retrospective only; never put it in a Plan. Do not call
a written Plan claimed or scheduler-visible until finalization confirms its
tracked bytes landed.
