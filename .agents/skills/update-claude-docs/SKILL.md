---
name: update-claude-docs
description: >-
  Synchronize AGENTS.md documentation and sibling CLAUDE.md import stubs after
  every C++ or GLSL change, and for an explicit request to update AGENTS.md.
  Also use for an explicit AGENTS.md audit or audit-and-fix request; audits
  report only unless improvement edits were already authorized.
allowed-tools: [Read, Edit, Write, Grep, Glob, Bash, PowerShell]
---

# Update AGENTS.md Documentation

Run inside one delegated `implementer`; never delegate. Return any
separate-role requirement to the manager.

Use one of these modes:

- Sync (default): inspect documentation governed by a caller-supplied changed-file list and session baseline. Edit only when affected guidance is stale or a durable invariant is missing.
- Audit: grade the requested AGENTS.md scope and report findings without edits.
- Audit and fix: grade first, then improve only the scope whose edits were explicitly authorized in the request. Do not add another approval pause.

Exclude `CLAUDE.local.md` and other local overrides unless the user explicitly includes them. Ask for direction only when missing scope, a missing session baseline, or a documentation conflict would meaningfully change the result; choose the simplest resolution for minor wording and organization choices.

## Sync Inputs and Boundary

Require the caller's complete changed-file list and session baseline. Treat the list as authoritative scope and that commit as the attribution point. Never infer session scope from `git status`, a dirty-tree diff, a moving merge base, or unrelated working-tree changes. If either required input is absent, request only the missing input.

For an explicit documentation-only request, use the named AGENTS.md or directory paths as the changed-file list while retaining the session-baseline requirement. Do not trim, rewrite, or normalize unrelated sections discovered during inspection.

## Sync Workflow

1. Run the discovery script once with the caller's changed-file list, from the repository root:

   ```powershell
   pwsh -NoProfile -File .agents/skills/update-claude-docs/scripts/Get-AffectedAgentsDocs.ps1 <changed path> [<changed path> ...]
   ```

   Pass the paths as separate arguments; a relative one resolves against the repository root, never the shell's working directory. Exit `0` with `status` `pass` returns the payload below; exit `1` with `status` `error` carries the failing `code` and `message` and nothing usable. A list that exceeds the output cap reports a nonzero `omittedCount`; rerun with fewer changed paths when something you need is omitted. Never reconstruct these operations inline — walking the hierarchy, sweeping stub pairs, or measuring sizes by hand.

   - `chains` — per changed path, the root-to-leaf governing `AGENTS.md` documents in `documents`, the nearest one in `governing`, and `hubCandidates`, the immediate descendant documents whose duplicated guidance a hub edit can make stale. A directly named documentation path governs itself.
   - `sizes` — the `bt-token-v1` size of each chain document with its `hub` or `leaf` classification and target (4,000 and 2,000), plus each chain's `totalTokens` against the 15,000 target and 20,000 warning. These deterministic normalized-byte estimates are advisory, not exact model tokens, and no verdict authorizes trimming: reduce only prose this change affects, and report pre-existing unrelated excess without trimming it.
   - `stubPairs` — the repository-wide bidirectional pairing sweep, excluding `ThirdParty/`, `Documents/Plans/`, `Documents/Features/`, the linked `Engine/Source/Graphics/Managers/*.AGENTS.md` references, and local overrides. `stub.missing` is a directory `AGENTS.md` with no sibling `CLAUDE.md`, `stub.orphan` a `CLAUDE.md` with no same-directory `AGENTS.md`, `stub.malformed` a stub whose bytes are not exactly `@AGENTS.md` plus one line ending. Fix a reported defect inside the authorized scope in the same edit; report one outside that scope as a residual.
2. Read every governing document and relevant parent or sibling rule before deciding whether to edit. Compare the current code and changed regions against present-tense documentation.
3. Prefer no edit when the guidance remains correct. A method, member, local refactor, or ordinary bug fix normally needs no new prose. Document a new subsystem, cross-cutting pattern, or non-obvious invariant only when omitting it would cause a future editor to make a worse decision.
4. Update only affected sections. Create a directory `AGENTS.md` only for a distinct subsystem, and create its sibling `CLAUDE.md` stub in the same edit. Never create directory memory for a single-file utility.
5. Re-read edited files, verify links, rerun the script to confirm stub bytes, and inspect the session-baseline diff limited to the authorized paths. If a directory is no longer a distinct subsystem, report its `AGENTS.md` and stub as a proposed deletion rather than deleting them without authority.

Treat a new or meaningfully changed algorithm as a candidate only when correctness or performance depends on a non-obvious mathematical, numerical, coordinate/grid, ordering, or hardware assumption. Source comments own local rationale; AGENTS.md owns the subsystem constraint needed for future design decisions.

## Audit Modes

Read `references/audit-mode.md` completely before auditing. Apply its discovery boundary, rubric, report format, and content examples.

Audit mode emits the quality report and completion report without editing. Audit-and-fix mode emits the quality report first, then applies only already-authorized improvements using the sync content rules, shows the affected diffs, and rechecks scores, links, sizes, and stub integrity. An audit request that asks only to report, assess, review, or grade never authorizes fixes.

## Content Rules

### Current State and Vocabulary

- State how the code works now. Do not narrate sessions, plans, commits, migrations, removals, former names, dates, or before/after history; git owns that record.
- Use vocabulary established by root and governing documents, including *Collection*, *Frame*, phase names, *workbuffer*, `gp*` singleton, SOA, EWNS, and deterministic CRC. Search the tree before inventing a near-synonym.
- A *hub* is a subsystem's main AGENTS.md that links to the detail docs below it. Existing wording also calls those detail docs *leaves*; *detail doc* is the term to use in new prose.
- Do not silently override a parent or sibling invariant. Pause on a meaningful contradiction and identify both sources; report an inconsistency that is not meaningful as a residual.
- Use direct, normal emphasis. Avoid all-caps directives and repeated rules.
- Apply the one-term-per-concept and plain-words rules in root `AGENTS.md` `## Directives`. Reintroducing a second name — including one the repository previously retired in favor of the established term — is a finding, not a style choice.
- Write concrete actions, not abstract noun-stacks: say who does what ("the loop that keeps processing until the queue is empty"), not a compressed label ("drain loop") — unless the label is an established defined term or a code identifier.

### Architecture, Not Inventories

- Describe responsibilities, ownership, relationships, data flow, algorithms, and reasons a design constraint exists.
- Do not list members, enum values, variables, files, uniforms, bindings, push constants, or method call chains. Do not name-drop a symbol introduced by the current change merely to document the diff.
- Link to the authoritative owner instead of duplicating parent, sibling, parallel-hierarchy, or `Documents/Architecture/` guidance. Use `@path` only for an intentional Claude import; use Markdown links for see-also references.
- Every ancestor AGENTS.md loads automatically alongside a child document, so a child never references, links, or "See Also"s its parent or any other ancestor AGENTS.md. Remove such a reference whenever you edit the text around it.

### Removing Text

- Remove a sentence unless its absence would plausibly cause a worse future decision.
- Never delete knowledge that still changes a decision; shorten it instead.
- When a rule moves out of a document, put it in a named place — a specific AGENTS.md or a specific source comment (`file:line` or function) — and say where. It may move; it may not just disappear. Shortening a rule that stays in the same document is not a move.
- Before saying a rule now lives somewhere else, open that file and confirm it is really there. Never assume a source comment exists.
- Keep knowledge that reading the current code cannot bring back — debugging findings, driver or build-tool behavior, questions already investigated and settled — while it still changes a decision. "The code cannot prove it" is not a reason to remove it.
- A size target never justifies deleting a sentence that still changes a decision. When a needed sentence pushes a file over its target, report the excess or propose splitting the file instead.

### Authoritative Source Exemplars

A source exemplar may replace procedural prose only when it points to one stable file and symbol, labels the concern and the applicability variant, and leaves the governing invariant and reason in AGENTS.md. A concern may cite at most three exemplars. The source demonstrates implementation shape; documentation remains authoritative for ownership and constraints.

Do not establish one-off code introduced by the current change as authoritative until an existing repository pattern supports it. During every affected documentation sync, verify that each cited path and symbol still demonstrates its label; retarget a stale symbol in the same edit. Keep distinct applicability variants distinct rather than presenting one exemplar as a universal policy.

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
