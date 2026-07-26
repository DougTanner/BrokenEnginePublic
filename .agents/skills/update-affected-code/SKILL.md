---
name: update-affected-code
description: >-
  Propagate an owned set of C++ or GLSL changes to every correctness-dependent
  caller, producer, consumer, mirror, serialization identity, and CPU/GPU
  contract the implementation did not update. Use during the Implement and
  propagate stage after any C++ or GLSL change, and for candidates outside a
  scoped review-fix wave. Search-and-update only; no refactoring, style work,
  or scope expansion.
allowed-tools: [Read, Edit, Grep, Glob, Bash, PowerShell]
---

# Update Affected Code

Run as one `implementer` over a fixed owned change set. Propagate only edits
forced by the approved behavior. Do not delegate, refactor, clean up, update
AGENTS.md, or edit project XML.

## Required Brief

Require:

- fixed session-start baseline and the exact owned changed files/regions,
  separated from pre-existing and concurrent work;
- approved plan or concise intent, applicable repository instructions, and
  implementation handoff;
- every affected-site trigger. Each signature, semantic, layout, and identity
  trigger states the old contract and new contract explicitly, plus its
  symbol/pattern and search scope. Sweep, guard, and mirror triggers state the
  invariant and counterpart scope.

Return `BLOCKED` without editing when the ownership boundary, controlling
intent, or a required old/new contract is missing. When the implementation
reports no triggers, inspect the owned diff and changed regions, construct the
applicable searches, and report the verified absence; do not infer it from the
handoff.

## Workflow

1. Read every owned changed region in full context and the producers,
   consumers, callers, sibling implementations, shared headers, and governing
   instructions needed to understand each handed-off contract.
2. Search both the old and new side of every contract. Account for all old-name
   hits after a rename and all users of the new symbol or representation. Use
   repository-tracked searches that work on the current host (prefer `git grep`
   with explicit pathspecs), limited to relevant C++, headers, and GLSL. Exclude
   `ThirdParty/`, generated data, and build/output directories; inspect owned
   untracked source additions directly. Do not use a moving merge base or
   `git status` alone to attribute changes.
3. Trace signatures, defaults, units, ranges, coordinate/W conventions,
   ownership, phases, enum values, and string/table keys through every caller,
   switch, dispatch table, format string, and mirror. Preserve deliberate
   client/server and per-collection parallel structure.
4. Search CPU-to-GLSL and GLSL-to-CPU in both directions. Compare member order,
   byte size, alignment/padding, descriptor set and binding numbers, push
   constant ranges, enum/flag numeric values, upload/fill sites, and every
   pipeline or shader consumer. Also trace serialization order, CRC membership,
   version gates, save/replay and wire readers/writers, payload sizing, and
   numeric or table identities. A compiling site is not evidence that its old
   assumption remains valid.
5. If a `Collection<T>` member or layout is added, removed, reordered, or
   retyped, read `add-collection-member` (`../add-collection-member/SKILL.md`)
   completely and treat its live-variant checklist as authoritative. Report an
   unresolved CRC, persistence, transfer, hydration, version, or identity
   choice instead of inventing intent.
6. Edit only sites whose correctness clearly depends on the new contract.
   Leave sibling features and design-dependent counterparts as residuals. Do
   not perform style fixes, documentation updates, project membership edits,
   abstractions, or incidental cleanup.
7. Re-run the old/new searches after editing and re-read every changed region.
   Run focused static or schema checks available in context. Return compilation,
   runtime checks, domain review, and documentation sync to the manager.
8. Emit an `/update-vcxproj` handoff for every added or removed C++/GLSL file
   and every existing C++ file that gained or lost a whole-file
   `BT_CLIENT`/`BT_SERVER` guard. Do not inspect or modify project XML.

## Handoff

Return one disposition per trigger, followed by the standard handoff:

```text
Trigger dispositions: <trigger — RESOLVED with updated sites or verified no-op;
  REFUTED with evidence; or UNRESOLVED with owner/action>
Status: PASS | NEEDS_ACTION | BLOCKED
Changed files: <path — exact functions/types/regions, or none>
Decisive checks: <old/new tracked searches, traces, rereads, and static checks>
Project membership trigger: /update-vcxproj — <paths/reason> | none
Build required: <exact targets/configuration/platform and project-member paths,
  or none>
Reviewer focus areas: <contract and failure condition to falsify, or none>
Residuals: <affected site not updated, incomplete search, ownership conflict,
  or unclassified hit, or none>
```

Keep `Residuals` last. Name each changed file once. `PASS` requires every
trigger resolved or refuted and every planned search complete; requested builds
remain `builder` work dispatched by the manager rather than passed checks.
