---
name: external-grill-plan
description: >-
  Review an immutable, plan-audited Tier-3 implementation plan and interview
  the user about decisions that still require judgment. Use after /plan-audit
  for every Tier-3 change; return exact refinements or a decision-complete
  no-delta result. Do not use for Tier-1 or Tier-2 work.
allowed-tools: [Read, Grep, Glob, Agent, PowerShell, AskUserQuestion]
---

# Grill Plan

Resolve the meaningful decisions that remain after repository exploration and a
fresh `/plan-audit`. The main session owns every user interaction. Delegates may
locate evidence or verify a single checkable external claim, but never interview the user
or choose for them.

Execution splits by role: the preparation `implementer` performs every
repository read, search, and WorktreeCli validation this skill requires and
returns immutable decision briefs; the main session interviews the user,
decides, and dispatches the `locator` for external-claim requests. Role
contract: `../next-plan/references/tier3-workflow.md`.

## Required Input and Boundary

Require all of the following before starting:

- an immutable Tier-3 plan supplied inline or by exact repository path;
- evidence that `/plan-audit` reviewed this exact plan revision, plus the
  manager's decision on every finding;
- user intent, applicable repository instructions, session baseline when one
  exists, and any changes the user approved after the plan;
- the draft execution card: Tier-3 triggers, roles, and each
  acceptance criterion with its decisive check and expected result.

Load an exact-path plan once and treat those bytes as the frozen plan text.
Never edit the plan, scheduler claims, code, or any other repository file. Audit
findings are decision inputs, not permission to patch that text. If the supplied
revision differs from the audited revision, the audit did not complete, or the
plan is not Tier 3, stop and return the needed correction to the manager.

## Workflow

1. Read the complete frozen plan text, audit result, finding decisions, execution
   card, applicable instructions, and every cited repository region needed to
   test its assumptions.
2. For a bug fix, rank three to five checkable causal hypotheses internally.
   Verify their predictions from code, logs, or supplied diagnostics. Ask only
   when multiple live causes would meaningfully change implementation or
   verification.
3. Run the gate in `references/library-gate.md` before other questions when the
   plan adds or rewrites a non-trivial subsystem, algorithm, or data structure.
4. Search the repository until local evidence either resolves a candidate
   decision or proves that user judgment is required. Do not ask the user to
   rediscover code facts.
5. Scan remaining decisions through the compact checklist below and map them as
   a dependency tree. Interview in rounds: each round asks the whole frontier —
   every decision whose prerequisites are already settled — in one interaction,
   splitting across consecutive interactions only when the host UI caps
   questions per call. A pending `locator` `/verify-external-claims` verdict is
   an unsettled prerequisite that blocks only the decisions depending on it;
   keep interviewing the rest of the frontier while it runs.
6. After each round's answers, record each selected choice and the exact plan
   refinement it implies, then recompute the frontier and ask the next round.
   A decision whose answer depends on another decision still open in this round
   belongs to a later round.
7. Run the closing checks, then return all refinements to the manager. The
   manager owns incorporation and approval validity. A design or library pivot
   additionally requires a fresh `/plan-audit` before this skill re-enters.

Do not manufacture optional improvements, future extensibility, or routine
confirmation questions. Stop when every meaningful branch is evidence-resolved,
user-resolved, or returned as a named pivot. If no meaningful decision survives,
return a no-delta handoff without asking the user anything.

## Decision Interaction

Lead with the first concrete decision, not a plan summary. For each question:

- state the repository evidence and why the choice changes the plan;
- offer exactly two or three meaningful, mutually exclusive choices;
- put the recommended choice first and explain its tradeoff;
- make every choice name the exact refinement it would produce.

Use the host's structured choice UI when available. Otherwise ask in prose but
preserve the same two or three choices, in the same recommended-first order,
with the same tradeoffs and refinements. Do not replace them with an open-ended
prompt. In prose, give every question in the round one fixed numbered shape so
the user can answer by number:

```text
Q<n> — <decision title>: <evidence, why the choice changes the plan, and the
two or three choices>
-> Recommended: <choice> — <tradeoff>
```

Dependent questions wait for a later round.

An exact refinement names the affected plan section and the concrete decision,
invariant, affected sites, or criterion/check text to add, replace, or remove.
Do not return summaries such as "clarify threading" or "use option A."

## Decision Checklist

Probe only surfaces the plan actually touches:

- Shape and ownership: unresolved alternatives; owning layer, translation
  unit, type, or phase; delete versus reserve; interface shape; reuse of an
  existing `common::` or vendored mechanism.
- Behavior and scale: hidden semantic changes, ordering/error/default
  behavior, unjustified constants or granularity, unbounded sparse-world scale,
  and kilometer-scale coordinates.
- Determinism and compatibility: client/server equivalence, floating-point
  or iteration order, CRC participation and `LogDifferences`, wire IDs,
  `kiVersion`/pack layout, save/replay compatibility, and interpolation versus
  snapping.
- Execution safety: Update/PostRender/Interpolate phase, dispatch ownership
  and shared writes, collection allocation/copy/spawn/transfer alignment,
  main-loop allocation tracking and workbuffer use.
- Integration: client/server guard scope, build/project/filter membership,
  PCH include placement, engine/game layer boundaries, consumers and mirrored
  sites, roles, and acceptance checks with independent signals for duplicates.

If the interface or system design itself is unresolved, do not improvise it.
Return a design pivot naming the exact questions for
`/external-design-interface`. Its result must be incorporated into a new plan
revision and pass a fresh `/plan-audit` before re-entry.

## Plan Context

The preparation `implementer` validates the scheduler once and supplies the
result in its brief. Require that brief to carry the passing envelope — exit
`0`, `status: valid`, `code: ok` — the tracked inventory it returned, any
validation notices, and the text of every referenced Plan. Missing or
non-passing evidence blocks this skill: stop and return the exact diagnostic to
the manager. Never run the validation yourself, and never run a command that
changes scheduler state.

Consult those tracked Plans only when the plan declares a dependency, shares
files or symbols with another Plan, or the closing checks reveal likely overlap.
Never read machine-local scheduler claims. `Documents/Features` is manual and
outside scheduler inventory.

## Closing Checks

Internally verify, without a routine final question:

- adjacent producers, consumers, shared headers, and mirrored client/server
  paths are covered;
- a deletion, existing mechanism, or verified library does not eliminate the
  proposed custom work;
- every exposed determinism, layout, protocol, replay, affinity, threading,
  trust-boundary, or allocation invariant is explicit;
- assumptions remain valid for unbounded counts, sparse-cell parallelism, and
  world-coordinate magnitude;
- roles and acceptance checks match the Tier-3 triggers.

Surface only a concrete meaningful decision established by current evidence. Feed
it through the same two-or-three-choice interaction contract.

## Handoff

Return the complete result inline. `Exact refinements` must preserve one entry
per decision, including the selected choice and precise plan text/section
change. `Plan delta` is relative to the frozen plan text; any behavior, scope,
architecture, acceptance, or verification change is meaningful.

```text
Plan snapshot: <inline identifier or exact path>
Plan-audit evidence: <audited revision identifier and result>
Plan delta: none | not meaningful | meaningful
Decisions and exact refinements:
- <decision ID> — <selected choice> — <section and exact refinement>
External claim verdicts:
- <stable claim ID> — VERIFIED | REFUTED | UNRESOLVED — <direct implication>
Execution-card decisions:
- <Tier-3 trigger/role/criterion -> decisive check -> expected result; independent signal when duplicated>
Required next step: none | incorporate refinements | incorporate library integration pivot and run fresh /plan-audit | run /external-design-interface, incorporate its design pivot, and run fresh /plan-audit
Files changed: none
Functions/regions touched: none
Residuals:
- <unresolved decision or none>
```

Questions, answers, exact refinements, pivots, and claim verdicts remain live
handoff data. Never collapse them into a prose summary.
