---
name: code-style-review
description: Reviews and auto-fixes provably meaning-preserving C++ style violations in session-changed ranges or an explicit cleanup scope, per `Documents/C++StyleGuide.txt`. Use after C++ changes or for a requested style, naming, or formatting cleanup; routes semantic candidates for classification instead of changing behavior.
allowed-tools: [Read, Write, Edit, Grep, PowerShell]
---

# Code Style Review

Run inside one delegated `mechanic`; never delegate. Review C++ only. Fix style
when the edit is provably meaning-preserving; return anything that could change
behavior or an interface for caller classification. Style review is not a
final-evidence gate.

## Scope

- By default, review `.cpp` and `.h` ranges changed in this session, using the
  implementation handoff and conversation edits.
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
   conditions, argument layout, initializers, preprocessor form, and braces
   (50-52, 58, 61).
2. Auto-fix only when the resulting C++ meaning is demonstrably unchanged.
   Examples include whitespace, argument layout, an exact deduced type replacing
   disallowed `auto`, and `NULL` replaced where it is a null pointer constant.
   Rule 15 permits `auto` for XMVECTOR/XMMATRIX results, a type obvious from a
   template parameter on the right, iterators, structured bindings, and a
   lambda expression assigned directly to the variable. It remains forbidden
   in plain range-based loops.
3. Do not auto-fix a candidate that requires changing container type or access
   semantics, public API, class/struct access or layout, control flow, overload
   resolution, or numeric behavior. Report it for caller classification and the
   applicable domain review.

For Rule 49, flag a getter, setter, drain, take, `Is*`, `Can*`, or equivalent
function whose entire implementation is one state access, assignment, or
pass-through call when callers can access the underlying state or component
independently. Prefer direct public access; private state is justified only when
one complete multi-statement operation preserves an invariant or required
ordering. Do not classify semantic codecs or serialization adapters as trivial
forwarding. Route these candidates because changing an interface or access
surface is not a style auto-fix.

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
  `// FIXME`, and `// HACK` lines. Search again for their exact text or existing
  unique debug tag and require zero remaining matches in session-added C++.
  Never add a tag merely to defer cleanup, and do not alter pre-existing
  intentional debug logs.
- In selected C++ comments, remove `AGENTS.md` or `CLAUDE.md` navigation text
  only when the remaining technical statement stays complete. Delete a comment
  whose sole content is the pointer; otherwise preserve its technical content
  and repair punctuation. Never touch strings or non-comment code.
- In selected changed comments, remove text that merely explains a language
  feature or established house pattern already visible in the declaration.
  Preserve invariants, required ordering and consequences, lifetime or threading
  contracts, and platform or driver workarounds.

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

### Routed Candidates
- file:line — candidate — classification/domain-review route

### Documentation Residuals
- identifier — file:line — `/update-claude-docs` or caller

Files changed:
- path, or none
Functions/regions touched:
- function or region, or none
Residuals:
- unresolved item, or none
```
