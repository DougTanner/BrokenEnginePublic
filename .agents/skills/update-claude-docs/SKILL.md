---
name: update-claude-docs
description: >-
  Synchronize AGENTS.md documentation and sibling CLAUDE.md import stubs after
  every C++ or GLSL change, and for explicit requests to update AGENTS.md,
  sync project memory, refresh repository guidance, or verify affected
  documentation. Also use for explicit AGENTS.md audit, grade, quality-report,
  repo-wide improvement, or audit-and-fix requests; audits report only unless
  improvement edits were already authorized.
allowed-tools: [Read, Edit, Write, Grep, Glob, Bash, PowerShell]
---

# Update AGENTS.md Documentation

Run inside one delegated `implementer`; never delegate. Return any
separate-role requirement to the manager.

Use one of these modes:

- Sync (default): inspect documentation governed by a caller-supplied changed-file list and fixed session baseline. Edit only when affected guidance is stale or a durable invariant is missing.
- Audit: grade the requested AGENTS.md scope and report findings without edits.
- Audit and fix: grade first, then improve only the scope whose edits were explicitly authorized in the request. Do not add another approval pause.

Exclude `CLAUDE.local.md` and other local overrides unless the user explicitly includes them. Ask for direction only when missing scope, a missing fixed baseline, or a documentation conflict would materially change the result; choose the simplest resolution for minor wording and organization choices.

## Sync Inputs and Boundary

Require the caller's complete changed-file list and fixed session-start commit. Treat the list as authoritative scope and the commit as the attribution baseline. Never infer session scope from `git status`, a dirty-tree diff, a moving merge base, or unrelated working-tree changes. If either required input is absent, request only the missing input.

For an explicit documentation-only request, use the named AGENTS.md or directory paths as the changed-file list while retaining the fixed baseline requirement. Do not trim, rewrite, or normalize unrelated sections discovered during inspection.

## Sync Workflow

1. From each changed C++ or GLSL file, walk toward the repository root and identify the nearest governing `AGENTS.md`. A directly named documentation path governs itself. A hub changed in scope also makes immediate descendants candidates for duplicated guidance made stale by that hub edit.
2. Discover `AGENTS.md` and `CLAUDE.md` paths to understand the hierarchy and sibling coverage. Exclude `ThirdParty/`, `Documents/Plans/`, `Documents/Features/`, and `Engine/Source/Graphics/Managers/*.AGENTS.md`; the manager files are linked references, not directory memory.
3. Read every governing document and relevant parent or sibling rule before deciding whether to edit. Compare the current code and changed regions against present-tense documentation.
4. Prefer no edit when the guidance remains correct. A method, member, local refactor, or ordinary bug fix normally needs no new prose. Document a new subsystem, cross-cutting pattern, or non-obvious invariant only when omitting it would cause a future editor to make a worse decision.
5. Update only affected sections. Create a directory `AGENTS.md` only for a distinct subsystem, and create its sibling `CLAUDE.md` stub in the same edit. Never create directory memory for a single-file utility.
6. Check the in-scope stub pairs in both directions: every directory `AGENTS.md` has a sibling `CLAUDE.md`, and every sibling `CLAUDE.md` has a same-directory `AGENTS.md`. Each stub contains exactly `@AGENTS.md` plus its line ending. Linked `*.AGENTS.md` references have no stub.
7. Measure every edited AGENTS.md with `pwsh -NoProfile -File .agents/scripts/Measure-Tokens.ps1 -Path <path>`. Target at most 2,000 `bt-token-v1` for a leaf and 4,000 for a cross-cutting hub; target an effective root-to-leaf chain below 15,000 and warn above 20,000. These are deterministic normalized-byte estimates, not exact model tokens. Reduce only affected prose; report pre-existing unrelated excess without trimming it.
8. Re-read edited files, verify links and stub bytes, and inspect the fixed-baseline diff limited to the authorized paths. If a directory is no longer a distinct subsystem, report its `AGENTS.md` and stub as a deletion candidate rather than deleting them without authority.

Treat a new or materially changed algorithm as a candidate only when correctness or performance depends on a non-obvious mathematical, numerical, coordinate/grid, ordering, or hardware assumption. Source comments own local rationale; AGENTS.md owns the subsystem constraint needed for future design decisions.

## Audit Modes

Read `references/audit-mode.md` completely before auditing. Apply its discovery boundary, rubric, report format, and content examples.

Audit mode emits the quality report and completion report without editing. Audit-and-fix mode emits the quality report first, then applies only already-authorized improvements using the sync content rules, shows the affected diffs, and rechecks scores, links, sizes, and stub integrity. An audit request that asks only to report, assess, review, or grade never authorizes fixes.

## Content Rules

### Current State and Vocabulary

- State how the code works now. Do not narrate sessions, plans, commits, migrations, removals, former names, dates, or before/after history; git owns that record.
- Use vocabulary established by root and governing documents, including *Collection*, *Frame*, phase names, *workbuffer*, `gp*` singleton, SOA, EWNS, and deterministic CRC. Search the tree before inventing a near-synonym.
- Do not silently override a parent or sibling invariant. Pause on a material contradiction and identify both sources; report a non-material inconsistency as a residual.
- Use direct, normal emphasis. Avoid all-caps directives and repeated rules.

### Architecture, Not Inventories

- Describe responsibilities, ownership, relationships, data flow, algorithms, and reasons a design constraint exists.
- Do not list members, enum values, variables, files, uniforms, bindings, push constants, or method call chains. Do not name-drop a symbol introduced by the current change merely to document the diff.
- Link to the canonical owner instead of duplicating parent, sibling, parallel-hierarchy, or `Documents/Architecture/` guidance. Use `@path` only for an intentional Claude import; use Markdown links for see-also references.
- Remove a sentence unless its absence would plausibly cause a worse future decision.

### Canonical Source Exemplars

A source exemplar may replace procedural prose only when it points to one stable file and symbol, labels the concern and the applicability variant, and leaves the governing invariant and reason in AGENTS.md. A concern may cite at most three exemplars. The source demonstrates implementation shape; documentation remains authoritative for ownership and constraints.

Do not establish one-off code introduced by the current change as canonical until an existing repository pattern supports it. During every affected documentation sync, verify that each cited path and symbol still demonstrates its label; retarget a stale symbol in the same edit. Keep distinct applicability variants distinct rather than presenting one exemplar as a universal policy.

### Stub Contract

Directory memory lives in `AGENTS.md`. Its sibling `CLAUDE.md` is only the one-line `@AGENTS.md` import. Put all guidance in AGENTS.md. Enforce the pairing bidirectionally, excluding `CLAUDE.local.md` unless explicitly authorized.

## Completion

End every mode with:

```text
Files changed:
- <AGENTS.md or CLAUDE.md path, or none>
Functions/regions touched:
- <document section, or none>
Residuals:
- <conflict, deletion candidate, pre-existing excess, or none>
```
