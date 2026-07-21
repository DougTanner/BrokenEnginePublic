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
dispatches every role. Give each role a bounded manifest or report slice and
forbid child delegation; schedule waves within the host's available concurrency.

Read root and target-applicable `AGENTS.md` files before dispatch. Before plan
authoring, require the wrapper-created worktree and live WorktreeCli session
claim required by `create-follow-up-plans`. If they are unavailable, the
read-only analysis may finish, but plan creation and publication are blocked.

## Phase 1: Architecture Shape

Invoke `external-architecture-review` natively through the normal skill surface
with the resolved target and scope mode. Do not open a client-specific skill
installation or reproduce its workflow here.

Retain its scoped manifest, authorities, verified findings, external-claim
residuals, and handoff note for Phase 2. Do not choose plan locations, filenames,
groups, collisions, dependencies, or duplicate dispositions at this stage.

## Phase 2: In-Function Mechanics

Invoke `external-refactor-clean` natively through the normal skill surface with
the same target and scope mode. Include the Phase-1 investigation paths as
evidence to inspect, without expanding the original finding boundary.

Keep its file-size triage separate from ordinary findings. Every oversized file
retains the explicit disposition `run /reduce-file <path>`; do not analyze it
inline or silently convert it into a generic refactor item.

## Phase 3: Findings Verification

After both reports complete, have the main context dispatch fresh `reviewer`
roles in bounded slices. Reviewers are findings-only: they may read the scoped
source, authorities, and reports supplied by the main context, but must not
write, rewrite, delete, score, or relocate a plan and must not delegate.

For every candidate, require a verdict and evidence for:

1. **Correctness** — cited path, symbol, behavior, and repository authority exist
   and support the claimed root cause.
2. **Benefit and safety** — the proposed outcome is functional or structural,
   not cosmetic, pattern-breaking, or more risky than the proven debt.
3. **Actionability** — the acceptance gap and smallest correction boundary are
   concrete enough for a plan author.
4. **External claims** — non-obvious API, specification, license, maintenance,
   or ThirdParty propositions become verification requests for the main context
   to route through `verify-external-claims`; unresolved claims remain residuals.

The main context adjudicates reviewer evidence once. Drop disproven candidates,
retain verified caveats, and preserve each accepted finding's originating phase,
symbols, evidence, invariant exposure, and acceptance gap. Keep oversized-file
items labeled as `/reduce-file` follow-ups.

## Phase 4: Author and Stage Follow-Ups

Invoke `create-follow-up-plans` natively with all accepted candidates, reviewer
verdicts, user decisions, source reports, session changed-file list, and the
deep-analysis intent that established each acceptance gap. Treat analysis
findings as pre-existing or out-of-scope debt, never as permission to fix code.

That skill exclusively owns plan classification, area placement, naming,
grouping, collision handling, duplicate mapping, live-plan updates,
Coordination/dependency decisions, scoring, and staged queue requests. Do not
pre-create plan files, copy a plan or queue schema, parse queue Markdown, or call
`plan order add` directly. Pass oversized candidates with their required
`/reduce-file <path>` disposition intact.

Account for every accepted candidate using the authoring skill's Created,
Updated existing, Duplicate mappings, or Residuals result. A missing live claim,
validation failure, conflict, or ungrounded item is a visible blocker or
residual, not grounds for an alternate queue path.

## Phase 5: Verify and Finalize

Run `verify-changes` against the final tracked plan tree, the complete
`create-follow-up-plans` report, and every staged mutation request. Map scope,
finding adjudication, candidate accounting, plan validation, and summary data to
decisive checks. Verification reviewers remain findings-only; route any accepted
semantic correction back through the owning workflow before re-verification.

Then invoke `finalize-changes`. Supply every staged add request so it can publish
rows only after the plan files land, and follow its user sign-off and claim-release
contract. Never publish queue additions or advance primary outside that workflow.

## Summary

After successful verification and the applicable finalization disposition,
report:

- exact target, scope mode, file count, and applicable authorities;
- architecture and refactor-clean finding counts;
- created, updated, duplicate-mapped, rejected, and residual items, with every
  oversized file still shown as `run /reduce-file <path>`;
- each authored plan's final tier, Effort, Impact, Risks, Score, and dependency
  disposition from `create-follow-up-plans`;
- one area Debt Score: **LOW** for nearly all Quick Win/Small, **MODERATE** for
  mostly Small/Medium, **HIGH** for multiple Large or any Architectural, and
  **CRITICAL** for several Architectural items or a core-invariant threat;
- verification result, finalization disposition, and queue publication state.

The Debt Score is a run retrospective only; never put it in a plan or queue
request. Do not claim a created plan is queued until finalization confirms its
post-landing publication.
