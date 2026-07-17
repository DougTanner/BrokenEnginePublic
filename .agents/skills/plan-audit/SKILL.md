---
name: plan-audit
description: >-
  Adversarially audit a Tier-2 or Tier-3 implementation plan before
  implementation; for Tier 3 it runs before /external-grill-plan. Do not add it
  to Tier-1 mechanical changes.
  Runs inside one delegated Fable reviewer; findings only, with no plan/code
  edits, user interview, or further delegation.
allowed-tools: [Read, Grep, Glob, PowerShell]
disallowed-tools: [Agent, Edit, Bash, AskUserQuestion]
---

# Plan Audit

Use this skill for every Tier-2 and Tier-3 change; Tier-1 mechanical work
skips it. Assume the supplied plan is flawed, verify it against the current
repository, and return only concrete findings and improvement suggestions for
the calling session to resolve when user input is actually needed — through
`/external-grill-plan` for Tier 3, or directly with the user for Tier 2. The
audit is findings-only work and never creates an approval gate; a `/next-plan`
invocation additionally follows the [canonical execution-gate
contract](../next-plan/references/execution-gates.md).

## Inputs

- Complete current plan
- Execution card, fixed session baseline, and manager execution-control record
  when they exist (Tier 3, queue, reconciliation, and landing work); an
  ordinary Tier-2 session supplies none
- User intent and applicable repository instructions
- Relevant repository paths and every cited code region
- Accumulated constraints or known residuals

## Reporting Mode

Return the audit inline. A plan audit is a decision aid before implementation,
not final evidence; its finding summary is the next role's input.

## Audit

1. Read the complete plan, applicable `AGENTS.md` files, and every cited code region.
2. Verify structural assumptions, call sites, mirrored client/server paths, ownership, data layout, frame phase, threading, determinism, serialization, build wiring, and runtime verification where relevant.
3. Hunt unresolved options, hidden behavior changes, contradictions, magic defaults, undeclared invariant exposure, missing affected locations, ungrounded requirements or checks, and scope that duplicates an existing mechanism.
4. Ground each proposed correction in user intent, an existing repository contract, or a necessary integration consequence. Reuse an existing mechanism, narrow scope, add missing propagation or verification, or replace an invalid step when that evidence decides the correction. When it does not, report the material user decision instead of selecting one.
5. Scale depth to the plan. Keep a one-file refactor light; inspect a new subsystem across its full integration surface.
6. For a `/next-plan` invocation, compare the complete current plan to the
   execution card and fixed session baseline. Verify that its goal, out-of-scope
   boundary, Tier-3 trigger, interfaces and invariants, acceptance checks with
   expected observations, role dispositions, and unresolved decisions agree
   with current repository evidence. Report any mismatch as a finding for the
   calling session; do not manufacture authority artifacts or another approval
   gate.
7. Audit the execution-control proposal against the risk-tier definitions in
   root `AGENTS.md`, classifying at the highest applicable tier. Verify that every
   concrete trigger is named, required and conditional roles fit the actual file
   types and risks, and each acceptance criterion has an initially decisive check
   and expected result, with a named independent signal for any duplicate check.
   Do not require `/repo-code-review` for documentation,
   workflow, style-only, or project-membership-only changes; require it when
   changed C++ or shader-adjacent logic needs its correctness contract. A reviewer
   may recommend escalation with evidence but may not silently lower the tier.

Do not edit the plan or code, interview the user, or spawn another agent. The calling session owns judgment and passes accepted findings into `/external-grill-plan` for Tier 3, or resolves them with the user directly for Tier 2.

## Output

For each finding:

> `plan-path:line` — **category** — concrete problem — evidence: `repository-path:line` — proposed improvement

If clean, state `PASS — no material plan flaws found.` End with:

> Files changed: none  
> Functions/regions touched: none  
> Residuals: `<incomplete audit items or none>`
