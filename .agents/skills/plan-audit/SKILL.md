---
name: plan-audit
description: >-
  Adversarially audit a Tier-3 implementation plan before /external-grill-plan
  only when its execution card retains a material scope, architecture, or
  acceptance ambiguity. Do not add it to decision-complete plans or Tier-1 or
  Tier-2 task-driven changes.
  Runs inside one delegated Fable reviewer; findings only, with no plan/code
  edits, user interview, or further delegation.
allowed-tools: [Read, Grep, Glob, PowerShell]
disallowed-tools: [Agent, Edit, Bash, AskUserQuestion]
---

# Plan Audit

Use this skill only when a Tier-3 execution card has a material scope,
architecture, or acceptance ambiguity. Assume the supplied plan is flawed,
verify it against the current repository, and return only concrete findings and
improvement suggestions for the calling session to resolve through
`/external-grill-plan` when user input is actually needed. It inherits the
[canonical `/next-plan` execution-gate
contract](../next-plan/references/execution-gates.md): the audit is conditional,
findings-only work and does not create an approval gate.

## Inputs

- Complete current plan, execution card, and fixed session baseline
- User intent and applicable repository instructions
- Relevant repository paths and every cited code region
- Accumulated constraints or known residuals
- Current manager execution-control record: risk tier and concrete triggers,
  required and conditional roles, and the initial acceptance-criterion matrix

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
7. Audit the execution-control proposal. Escalate to the highest applicable tier:
   Tier 1 for mechanical documentation/style/project-membership or local
   behavior-preserving work with no public signature or invariant exposure;
   Tier 2 for one subsystem's scoped behavior without determinism/CRC, wire,
   serialization/save/replay, threading, trust-boundary, or shared-coordination
   exposure; Tier 3 for any excluded Tier-2 surface, all-session build/bootstrap
   coordination, or independently owned subsystem integration. Verify that every
   concrete trigger is named, required and conditional roles fit the actual file
   types and risks, and each acceptance criterion has an initially decisive check
   and expected result, with a named independent signal for any duplicate check.
   Do not require `/repo-code-review` for documentation,
   workflow, style-only, or project-membership-only changes; require it when
   changed C++ or shader-adjacent logic needs its correctness contract. A reviewer
   may recommend escalation with evidence but may not silently lower the tier.

Do not edit the plan or code, interview the user, or spawn another agent. The calling session owns judgment and passes accepted findings into `/external-grill-plan`.

## Output

For each finding:

> `plan-path:line` — **category** — concrete problem — evidence: `repository-path:line` — proposed improvement

If clean, state `PASS — no material plan flaws found.` End with:

> Files changed: none  
> Functions/regions touched: none  
> Residuals: `<incomplete audit items or none>`
