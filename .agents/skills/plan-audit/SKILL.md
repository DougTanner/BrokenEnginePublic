---
name: plan-audit
description: >-
  Adversarially audit an implementation plan before /external-grill-plan. Use as
  the C++ Code Change Process Approve and classify stage to find concrete flaws,
  verify structural assumptions and the proposed risk classification against the
  repository, and propose tighter or simpler approaches.
  Runs inside one delegated Fable reviewer; findings only, with no plan/code
  edits, user interview, or further delegation.
allowed-tools: [Read, Write, Grep, Glob, PowerShell]
disallowed-tools: [Agent, Edit, Bash, AskUserQuestion]
---

# Plan Audit

Assume the supplied plan is flawed. Verify it against the current repository and return only concrete findings and improvement suggestions for the calling session to resolve through `/external-grill-plan`.

## Inputs

- Plan file and user intent
- Relevant repository paths and the current changed-file baseline, when available
- Accumulated constraints or known residuals
- Draft manager execution-control record when available: fixed process baseline,
  proposed risk tier and concrete triggers, required and conditional roles, and
  the initial acceptance-criterion matrix
- For a `/next-plan` invocation or a canonical plan containing either authority heading: the immutable source-scope packet, `## Provenance map`, `## Approved-delta ledger`, and explicit approved-delta summary
- `ReportPath` when invoked in a delegated subagent

## Reporting Mode

When delegated, require `ReportPath` and follow
[`be-agent-report/v1`](../../references/subagent-reporting.md): write the full
audit in the existing Output schema, verify it, and return only the compact
envelope. A missing or unwritable delegated report blocks the audit. A direct
invocation without `ReportPath` keeps the existing full inline output.

## Audit

1. Read the complete plan, applicable `AGENTS.md` files, and every cited code region.
2. Verify structural assumptions, call sites, mirrored client/server paths, ownership, data layout, frame phase, threading, determinism, serialization, build wiring, and runtime verification where relevant.
3. Hunt unresolved options, hidden behavior changes, contradictions, magic defaults, undeclared invariant exposure, missing affected locations, and scope that duplicates an existing mechanism.
4. Propose a concrete correction for each problem: resolve a decision, reuse an existing mechanism, narrow scope, add missing propagation or verification, or replace an invalid step.
5. Scale depth to the plan. Keep a one-file refactor light; inspect a new subsystem across its full integration surface.
6. For a `/next-plan` invocation or a plan containing either scope-authority heading, first require both headings, then independently verify the source packet before trusting the synthesis: read back its lossless original bytes, recompute SHA-256, match plan path and fixed baseline, validate any tracked author commit/blob identity, and confirm every `S###` against the captured source. Require every execution step to cite one matching `S###`, `P###`, or `D###`; every `P###` to derive from named S/C evidence and satisfy the exact required-propagation boundary; and every `D###` to derive from a candidate, exact user decision, approved-delta ledger, and supplied summary. Flag missing or mismatched source evidence, untraceable execution, a pending/non-execution candidate in execution, or any other unauthorized state. A correctly excluded `Delta requested` is a grill decision, not implementation authority. `Follow-up pending` and Rejected states remain non-execution and do not invalidate the base plan. In every authority audit result, explicitly identify the complete D ID set, exact ledger rows, and exact approved-delta summary verified, including IDs `none`, ledger `- none`, and summary `none` for the empty state; if any check fails, label the D authority verification failed instead of identifying it as verified.
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

For every authority-section audit, place `Verified D authority: IDs <complete set>; ledger <exact rows>; summary <exact value>` before the findings only after that exact state passes verification; otherwise place `D authority verification: FAILED — <reason>`.

> Files changed: none  
> Functions/regions touched: none  
> Residuals: `<incomplete audit items or none>`
