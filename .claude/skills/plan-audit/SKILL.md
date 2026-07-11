---
name: plan-audit
description: >-
  Adversarially audit an implementation plan before /external-grill-plan. Use as
  C++ Code Change Process step 1 to find concrete flaws, verify structural
  assumptions against the repository, and propose tighter or simpler approaches.
  Runs inside one delegated Fable reviewer; findings only, with no plan/code
  edits, user interview, or further delegation.
allowed-tools: [Read, Grep, Glob]
disallowed-tools: [Agent, Write, Edit, Bash, AskUserQuestion]
---

# Plan Audit

Assume the supplied plan is flawed. Verify it against the current repository and return only concrete findings and improvement suggestions for the calling session to resolve through `/external-grill-plan`.

## Inputs

- Plan file and user intent
- Relevant repository paths and the current changed-file baseline, when available
- Accumulated constraints or known residuals

## Audit

1. Read the complete plan, applicable `AGENTS.md` files, and every cited code region.
2. Verify structural assumptions, call sites, mirrored client/server paths, ownership, data layout, frame phase, threading, determinism, serialization, build wiring, and runtime verification where relevant.
3. Hunt unresolved options, hidden behavior changes, contradictions, magic defaults, undeclared invariant exposure, missing affected locations, and scope that duplicates an existing mechanism.
4. Propose a concrete correction for each problem: resolve a decision, reuse an existing mechanism, narrow scope, add missing propagation or verification, or replace an invalid step.
5. Scale depth to the plan. Keep a one-file refactor light; inspect a new subsystem across its full integration surface.

Do not edit the plan or code, interview the user, or spawn another agent. The calling session owns judgment and passes accepted findings into `/external-grill-plan`.

## Output

For each finding:

> `plan-path:line` — **category** — concrete problem — evidence: `repository-path:line` — proposed improvement

If clean, state `PASS — no material plan flaws found.` End with:

> Files changed: none  
> Functions/regions touched: none  
> Residuals: `<incomplete audit items or none>`
