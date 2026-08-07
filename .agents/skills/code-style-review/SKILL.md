---
name: code-style-review
description: Reviews and auto-fixes provably meaning-preserving C++ style violations in session-changed ranges or an explicit cleanup scope, per `Documents/C++StyleGuide.txt`. Use after C++ changes or for a requested style, naming, or formatting cleanup; routes semantic candidates for classification instead of changing behavior.
allowed-tools: [Read, Write, Edit, Grep, PowerShell]
---

# Code Style Review

Run inside one delegated `mechanic`; never delegate. Review C++ only. Fix style
when the edit is provably meaning-preserving; return anything that could change
behavior or an interface for caller classification. Style review is not a
landing gate (defined in root `AGENTS.md`).

## Scope

- By default, review `.cpp` and `.h` ranges changed in this session, using the
  implementation handoff and conversation edits. Derive those ranges from the
  read-only inventory: `pwsh -NoProfile -File
  .agents/scripts/Get-SessionChangeInventory.ps1 -RepositoryRoot <absolute
  repository toplevel> -Baseline <full 40-character SHA> -Regions` (in Claude
  Code's Git Bash terminal convert the script path and root with `cygpath -w`
  exactly as `../cleanup-worktrees/SKILL.md` shows). It writes no file and
  prints one `broken-engine-session-change-inventory/v1` object; the
  session-changed C++ ranges are its `regions` rows whose path carries the
  `class` `cpp` or `dual-language-header` in `entries`. Only `status` `pass`
  (exit 0) is usable; `blocked` (exit 2) or `error` (exit 1) means the ranges are
  unavailable — report that instead of proceeding. The entries list is capped at
  500 rows and the regions table at 400, and the ranges are derived from both, so
  read `truncated`: the ranges are usable only when `truncation.entries` and
  `truncation.regions` each report an emitted count equal to the full count, and
  if either falls short report the ranges unavailable instead of proceeding. An
  untracked file appears only when the caller supplies it with
  `-IncludeUntracked <comma-separated paths>`, and `counts.unlistedUntracked`
  reports how many untracked files the run did not list. Never enumerate these
  ranges inline.
- When the caller supplies a cleanup scope, use exactly those C++ files and
  ranges instead. State whether the scope is session-changed or caller-supplied.
- Shader style is out of scope; do not review or route it. The only shader
  edits are the reference updates that propagate a C++ rename (see Renames and
  References).

Read `Documents/C++StyleGuide.txt` first; it is authoritative. Inspect every
applicable rule, not only grep-friendly examples below.

## Review

1. Search the selected ranges for violations. High-value checks include:
   Hungarian notation and complete names (Rules 3, 14, 56, 57); `auto`,
   template, float, null, and override rules (15, 19, 27-29); container access
   and types (16, 21, 32); namespace and member access rules (41, 49); pointer
   conditions, argument layout, initializers, preprocessor form, braces, and
   early-return guards (50-52, 58, 61, 62).
2. Auto-fix only when the resulting C++ meaning is demonstrably unchanged.
   Examples include whitespace, argument layout, an exact deduced type replacing
   disallowed `auto`, and `NULL` replaced where it is a null pointer constant.
   Rule 15 permits `auto` for XMVECTOR/XMMATRIX results, a type obvious from a
   template parameter on the right, iterators, structured bindings, and a
   lambda expression assigned directly to the variable. It remains forbidden
   in plain range-based loops.
3. Do not auto-fix a proposed finding that requires changing container type or access
   semantics, public API, class/struct access or layout, control flow, overload
   resolution, or numeric behavior. Report it for caller classification and the
   applicable domain review.

Rule 49 forwarding findings are routed, not auto-fixed — see `/repo-code-review`
(`../repo-code-review/SKILL.md`).

## Renames and References

Rename an identifier only when it is a meaning-preserving style correction and
all code references can be propagated. For every rename:

1. Search the old identifier across the repository before editing.
2. Propagate every reference the rename breaks in C++ and shader sources,
   including references outside the selected ranges. Applying the shader-side
   reference updates is part of the rename.
3. Route stale `AGENTS.md` references to `/update-claude-docs`. List ordinary
   documentation and plan references as caller residuals.
4. Return the exact affected build targets; a rename is not verified without
   those builds.

## Session Cleanup

- Remove confirmed temporary debug instrumentation added during the session,
  including temporary `LOG`, `printf`, `DEBUG_BREAK()`, `assert(false)`,
  `// FIXME`, and `// HACK` lines. Take the added-versus-pre-existing
  distinction from the residue scanner below. Search again for their exact text
  or existing unique debug tag and require zero remaining matches in
  session-added C++. Never add a tag merely to defer cleanup, and do not alter
  pre-existing intentional debug logs.
- In selected C++ comments, remove `AGENTS.md` or `CLAUDE.md` navigation text
  only when the remaining technical statement stays complete, taking the same
  added-versus-pre-existing distinction from the scanner's hits of that kind.
  Delete a comment whose sole content is the pointer; otherwise preserve its
  technical content and repair punctuation. Never touch strings or non-comment
  code.
- In selected changed comments, remove text that merely explains a language
  feature or established house pattern already visible in the declaration.
  Preserve invariants, required ordering and consequences, lifetime or threading
  contracts, and platform or driver workarounds.

The scanner is `pwsh -NoProfile -File
.agents/scripts/Find-SessionDebugResidue.ps1 -RepositoryRoot <absolute
repository toplevel> -Baseline <full 40-character SHA>`, with optional `-Head
<commit>` and the `-IncludeUntracked` switch, which makes the scanner enumerate
every untracked file itself and include those files in the scan, and the same
`cygpath -w` conversion in Git Bash. It scans added lines only and prints one
`broken-engine-session-debug-residue/v1` object with `hits` rows of `path`,
`line`, `kind`, and `text`, plus `counts` and `truncated`. It reports candidates
only: it never edits a file, never decides whether a hit is temporary or
intentional, and never writes to disk, so every judgment and removal above stays
here. Only `status` `pass` (exit 0) is usable; `blocked` (exit 2) or `error`
(exit 1) means the session-added distinction is unavailable — report it rather
than reconstructing these scans inline, and treat `truncated` `true` as hits the
run did not list.

## Output

```markdown
## Style Review Results
Scope: session-changed ranges | caller-supplied cleanup scope

### Fixes Applied
- file:line — Rule N — correction

### Renames and Required Builds
- old → new — propagated C++ references

Build required:
- exact affected targets, or none

### Routed Findings
- file:line — proposed finding — classification/domain-review route

### Documentation Residuals
- identifier — file:line — `/update-claude-docs` or caller

Files changed:
- path, or none
Functions/regions touched:
- function or region, or none
Residuals:
- unresolved item, or none
```
