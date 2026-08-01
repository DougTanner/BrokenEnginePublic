---
name: reduce-file
description: >-
  Analyze or reduce oversized C++ headers and implementation files. Use for a
  standalone /reduce-file request, when preparing a decision-complete reduction
  plan, or when an approved plan assigns a file-size reduction. During code
  review, report only a qualifying size observation and defer planning.
argument-hint: <file-path>
allowed-tools: [Read, Write, Edit, Glob, Grep, PowerShell]
---

# Reduce File

Find the smallest cohesive boundary that brings an oversized C++ file below its
threshold without scattering ownership or disguising size.

## Context

Choose exactly one mode from the invocation:

- Review observation: While reviewing a change, measure the modified file
  and apply the review's qualification rules. Report only the size and concrete
  cohesive split opportunity. Do not map the file, draft a plan, or edit code;
  route an accepted out-of-scope residual through `/create-follow-up-plans`.
- Standalone or pre-approval analysis: Inspect the target and return one
  evidence-backed meaningful plan choice. Do not ask the user to select among
  speculative options. Produce the decision-complete draft inline; do not write
  or queue a plan unless the user explicitly authorizes that action.
- Approved-plan execution: Treat the approved plan and deltas as the decision
  authority. Implement its assigned reduction without reopening settled design
  choices. Stop and report a contradiction if repository evidence invalidates a
  meaningful plan assumption.

If no path is supplied, ask for one. Never add a source marker declaring an
oversized file accepted.

## Qualify the Target

Run:

```powershell
pwsh -NoProfile -File .agents/scripts/Measure-Tokens.ps1 -Path <path>
```

Measure several files in one batched run instead of one call per file. Use
`-Command`, not `-File`: under `-File` the comma-separated list binds as one
filename and the run fails.

```powershell
pwsh -NoProfile -Command "& '<absolute path to Measure-Tokens.ps1>' -Path 'a','b','c' -Json"
```

`bt-token-v1` is normalized UTF-8 bytes divided by four, rounded up.

- `.h`: reduction threshold is over 5,000.
- `.cpp`: reduction threshold is over 10,000.

At or below the applicable threshold, report the measurement and return no
reduction plan. Reject other extensions. Record the original size and, after an
approved implementation, the resulting size of every changed or new file.

## Analyze the Boundary

Read applicable `AGENTS.md` files, the full target, its declaration or
implementation counterpart, direct callers and dependencies, nearby helpers,
and project membership. Map:

- declarations and definitions with line ranges and approximate sizes;
- preprocessor affinity, templates, inline code, anonymous-namespace items,
  constants, local types, and global definitions;
- responsibility groups, call chains, and data each group reads or writes;
- symbols shared across proposed boundaries and include/circular-dependency
  consequences.

In approved-plan execution, use this map only to verify the approved boundary
and discover affected sites; do not choose a different design. Otherwise choose
the least disruptive cohesive reduction:

1. Move independent free functions, constants, or local types into an existing
   suitable utility pair, or a new `*Utils.h` / `*Utils.cpp` pair.
2. Extract a cohesive stateful responsibility into a new class. A concrete,
   non-template class keeps one `.h` / `.cpp` pair. The original object owns it
   by value unless a concrete lifetime, polymorphism, ABI, or dependency reason
   requires a pointer. Never introduce global ownership.
3. Split implementations by responsibility only when the declaration is a
   static-method struct. Keep its declarations in one header.

Never distribute one concrete class's member definitions across sibling `.cpp`
files merely to reduce the measured file. Template definitions remain inline in
headers unless an existing explicit-instantiation design proves otherwise.

Prefer an existing suitable helper over a new abstraction. A proposed class
must own meaningful data and behavior; do not wrap stateless functions in a
class. Preserve narrow client/server guards and current public interfaces unless
the approved design requires a change.

## Standalone Plan Draft

Return one recommended design with:

```markdown
## File Reduction: <file>
**Measured size:** <n>/<threshold> bt-token-v1 — **Classification:** <concrete class | template | static-method struct | other>
### Evidence and boundary
### Design
- <files retained/created, exact moves, ownership/interface/shared-symbol/include/affinity decisions, ordered buildable steps>
### Expected sizes
- `<file>`: ~<n> bt-token-v1 (from <n>)
### Critical files
### Out of scope
### Risks and verification
- <risk> -> <decisive check>
```

The draft must choose the boundary, filenames, ownership, interface shape,
shared-symbol placement, and implementation order. Include all affected files
and consuming build targets; do not leave alternative designs for a later
implementer to decide.

## Approved Execution

Make only the approved moves and required caller/include propagation. For every
added or removed `.cpp` or `.h`, route project and filter membership through
`/update-vcxproj`; do not hand-edit project XML. Every new `.cpp` or `.h` also
requires an affected-target build through `/compile`. Remeasure every changed
and new C++ file, run focused static checks, and report any file still above its
applicable threshold. Return exact changed regions, notes on which other code
sites may be affected, build targets, checks, and residuals to the parent Change
Workflow.
