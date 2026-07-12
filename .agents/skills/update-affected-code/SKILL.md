---
name: update-affected-code
description: >-
  Propagates this session's C++ changes to every affected location the
  implementation didn't touch — call sites of changed signatures or semantics,
  mirrored client/server or per-collection patterns that must stay in sync, and
  stale references in comments or shared C++/GLSL headers. Step 3 of the
  AGENTS.md C++ Code Change Process; invoke after implementation and its
  self-audit, passing the changed-file list, touched functions/regions, and any
  sweep-exhaustiveness handoffs. Search-and-update only — no refactoring, no
  style fixes, no scope expansion.
allowed-tools: [Read, Grep, Glob, Edit, Bash]
---

# Update Affected Code

Propagate the session's changes outward: find every location whose correctness depends on the modified code and update it. Designed to run as a Sonnet subagent briefed by the caller.

## Inputs (from the caller's prompt)

- Changed-file list + touched functions/regions from the implementation step
- Plan document path (or the caller's one-paragraph intent summary) — needed to judge mirrored-pattern edits and plan-scope residuals
- Sweep-exhaustiveness items handed off by the self-audit — treat each as a mandatory search target and report its resolution individually

## What to Search For

For each changed symbol or behavior, Grep the repo (excluding `ThirdParty/`):

1. **Signature/identity changes** — renamed or re-parameterized functions, changed enum values, struct layout changes: find every user. The step 8 build catches most, but fix them now, and catch what the compiler can't — LOG/format strings, string-matched names, data tables keyed by name.
2. **Semantic changes** — changed units, ranges, coordinate conventions, defaults: callers that still compile but embed the old assumption.
3. **Mirrored patterns** — client/server sibling functions, per-collection boilerplate (AllocateAndCopy / LogDifferences / Spawn / Transfer), parallel switch statements or tables enumerating the same set: if the change touched one instance of a mirror, verify each counterpart and update it where the plan's intent clearly requires the same edit.
4. **Stale references** — comments, LOG text, and dual-language shader/C++ headers naming a changed symbol or describing changed behavior.

Confirm exhaustiveness by Grep, not recall: for every rename or repeated-pattern change, grep the old identifier and account for every hit.

## Non-Scope

- Sibling *features* or scope expansion — a counterpart site needing a design decision is a residual, not an edit
- Style, naming, formatting (code-style-review); AGENTS.md docs (update-claude-docs); vcxproj membership (update-vcxproj)
- Refactoring or cleanup the change doesn't force

## Build

Build the edited `.cpp` files via selective `/compile` and fix compile errors before reporting (implementing subagents build inline).

## Report

- Files changed + functions/regions touched (one line each)
- Per handed-off item: resolved (what was updated, or why nothing needed updating) or REFUTED with evidence
- End with residuals — affected sites found but not updated, incomplete searches, and hits you could not classify — or "none"
