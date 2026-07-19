---
name: update-affected-code
description: >-
  Propagates this session's C++ changes to every affected location the
  implementation didn't touch — call sites of changed signatures or semantics,
  mirrored client/server or per-collection patterns that must stay in sync, and
  stale references in comments or shared C++/GLSL headers. Invoke during the
  Implement and propagate stage after any code change; documentation, skill,
  and script changes do not trigger it, and a review-fix wave confined to one
  function with no signature or contract change scans its own affected sites,
  invoking this skill only for candidates outside its assigned scope.
  Search-and-update only — no refactoring, style fixes, or scope expansion.
allowed-tools: [Read, Write, Grep, Glob, Edit, Bash]
---

# Update Affected Code

Propagate the session's changes outward: find every location whose correctness depends on the modified code and update it. Designed to run as an `implementer` subagent briefed by the caller — propagation edits real code and needs judgment.

## Inputs (from the caller's prompt)

- Concise inline implementation handoff, including every changed region and affected-site trigger
- Manager execution-control record when one exists (Tier 3, queue, reconciliation, and landing work)
- Plan document path (or the caller's one-paragraph intent summary) — needed to judge mirrored-pattern edits and plan-scope residuals
- Every affected-site trigger loaded from the supplied implementation reports —
  treat each sweep/signature/identity/semantics/layout/guard/mirror item as a
  mandatory search target and report its resolution individually. When the
  implementation emitted none, verify that absence with a targeted search of
  the changed regions and report `no affected sites` with the searches run.

## Reporting Mode

Return the propagation handoff inline. A propagation sweep is not a final
evidence gate; include each trigger disposition and residual directly.

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
