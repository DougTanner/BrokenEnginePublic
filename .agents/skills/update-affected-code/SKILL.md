---
name: update-affected-code
description: >-
  Propagates this session's C++ changes to every affected location the
  implementation didn't touch — call sites of changed signatures or semantics,
  mirrored client/server or per-collection patterns that must stay in sync, and
  stale references in comments or shared C++/GLSL headers. Invoke during the
  Implement and propagate stage when implementation emits a sweep handoff or a
  signature, identity, semantics, layout, guard-affinity, or mirrored-pattern
  trigger. Search-and-update only — no refactoring, style fixes, or scope expansion.
allowed-tools: [Read, Write, Grep, Glob, Edit, Bash]
---

# Update Affected Code

Propagate the session's changes outward: find every location whose correctness depends on the modified code and update it. Designed to run as a Sonnet subagent briefed by the caller.

## Inputs (from the caller's prompt)

- Implementation report compact-envelope identities (`REPORT`, `REPORT_SHA256`) plus the indexed changed-region and affected-site-trigger IDs, exact evidence locators, and dependencies; invoke `Read-AgentReportSection.ps1` once per exact range under the shared [`be-agent-report/v1`](../../references/subagent-reporting.md) consumption contract instead of requiring the caller to paste reports
- Manager execution-control record bound to the fixed process baseline
- Plan document path (or the caller's one-paragraph intent summary) — needed to judge mirrored-pattern edits and plan-scope residuals
- Every affected-site trigger loaded from the supplied implementation reports —
  treat each sweep/signature/identity/semantics/layout/guard/mirror item as a
  mandatory search target and report its resolution individually. When the
  implementation emitted none, main records `/update-affected-code: N/A` and
  does not invoke this skill.
- `ReportPath` for this delegated step

## Reporting Mode

This process step is delegated, so require `ReportPath` and follow
[`be-agent-report/v1`](../../references/subagent-reporting.md): write the full
report in the existing Report schema, verify it, and return only the compact
envelope. Index every applied propagation, handed-off-item disposition, and
residual. A missing or unwritable report blocks the sweep. If invoked directly
without `ReportPath`, preserve the existing full inline report.

## What to Search For

For each changed symbol or behavior, Grep the repo (excluding `ThirdParty/`):

1. **Signature/identity changes** — renamed or re-parameterized functions, changed enum values, struct layout changes: find every user. The later targeted compile catches most, but fix them now, and catch what the compiler can't — LOG/format strings, string-matched names, data tables keyed by name.
2. **Semantic changes** — changed units, ranges, coordinate conventions, defaults: callers that still compile but embed the old assumption.
3. **Mirrored patterns** — client/server sibling functions, per-collection boilerplate (AllocateAndCopy / LogDifferences / Spawn / Transfer), parallel switch statements or tables enumerating the same set: if the change touched one instance of a mirror, verify each counterpart and update it where the plan's intent clearly requires the same edit.
4. **Stale references** — comments, LOG text, and dual-language shader/C++ headers naming a changed symbol or describing changed behavior.

Confirm exhaustiveness by Grep, not recall: for every rename or repeated-pattern change, grep the old identifier and account for every hit.

## Non-Scope

- Sibling *features* or scope expansion — a counterpart site needing a design decision is a residual, not an edit
- Style, naming, formatting (code-style-review); AGENTS.md docs (update-claude-docs); vcxproj membership (update-vcxproj)
- Refactoring or cleanup the change doesn't force

## Targeted-check handoff

Do not replace the manager's following targeted compile/static-check stage.
Report every edited `.cpp`, affected target, static validator, or non-C++ check
that stage must run before correctness review.

## Report

- Files changed + functions/regions touched (one line each), or `none`
- Per handed-off item: resolved (what was updated, or why nothing needed updating) or REFUTED with evidence
- Targeted compile/static-check handoff for the propagated final bytes, or `none`
- End with residuals — affected sites found but not updated, incomplete searches, and hits you could not classify — or "none"
