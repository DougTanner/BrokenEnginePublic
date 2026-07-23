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

Resolve the material decisions that remain after repository exploration and a
fresh `/plan-audit`. The main session owns every user interaction. Delegates may
locate evidence or verify an atomic external claim, but never interview the user
or choose for them.

## Required Input and Boundary

Require all of the following before starting:

- an immutable Tier-3 plan supplied inline or by exact repository path;
- evidence that `/plan-audit` reviewed this exact plan revision, plus the
  manager's disposition of every finding;
- user intent, applicable repository instructions, fixed baseline when one
  exists, and any approved deltas;
- the draft execution-control record: Tier-3 triggers, roles, and each
  acceptance criterion with its decisive check and expected result.

Load an exact-path plan once and treat those bytes as the interview snapshot.
Never edit the plan, scheduler claims, code, or any other repository file. Audit
findings are decision inputs, not permission to patch the snapshot. If the supplied
revision differs from the audited revision, the audit did not complete, or the
plan is not Tier 3, stop and return the needed correction to the manager.

## Workflow

1. Read the complete snapshot, audit result, finding dispositions, execution
   record, applicable instructions, and every cited repository region needed to
   test its assumptions.
2. For a bug fix, rank three to five falsifiable causal hypotheses internally.
   Verify their predictions from code, logs, or supplied diagnostics. Ask only
   when multiple live causes would materially change implementation or
   verification.
3. Run the Existing-Library Gate before other questions when the plan adds or
   rewrites a non-trivial subsystem, algorithm, or data structure.
4. Search the repository until local evidence either resolves a candidate
   decision or proves that user judgment is required. Do not ask the user to
   rediscover code facts.
5. Scan remaining decisions through the compact checklist below. Order them by
   dependency and batch up to three independent decisions in one interaction.
6. After each answer, record the selected choice and the exact plan refinement
   it implies. Continue only with decisions newly unblocked by that answer.
7. Run the closing checks, then return all refinements to the manager. The
   manager owns incorporation and approval validity. A design or library pivot
   additionally requires a fresh `/plan-audit` before this skill re-enters.

Do not manufacture optional improvements, future extensibility, or routine
confirmation questions. Stop when every material branch is evidence-resolved,
user-resolved, or returned as a named pivot. If no material decision survives,
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
prompt. Dependent questions wait for the prior answer.

An exact refinement names the affected plan section and the concrete decision,
invariant, affected sites, or criterion/check text to add, replace, or remove.
Do not return summaries such as "clarify threading" or "use option A."

## Decision Checklist

Probe only surfaces the plan actually touches:

- **Shape and ownership:** unresolved alternatives; owning layer, translation
  unit, type, or phase; delete versus reserve; interface shape; reuse of an
  existing `common::` or vendored mechanism.
- **Behavior and scale:** hidden semantic changes, ordering/error/default
  behavior, unjustified constants or granularity, unbounded sparse-world scale,
  and kilometer-scale coordinates.
- **Determinism and compatibility:** client/server equivalence, floating-point
  or iteration order, CRC participation and `LogDifferences`, wire IDs,
  `kiVersion`/pack layout, save/replay compatibility, and interpolation versus
  snapping.
- **Execution safety:** Update/PostRender/Interpolate phase, dispatch ownership
  and shared writes, collection allocation/copy/spawn/transfer alignment,
  main-loop allocation tracking and workbuffer use.
- **Integration:** client/server guard scope, build/project/filter membership,
  PCH include placement, engine/game layer boundaries, consumers and mirrored
  sites, roles, and acceptance checks with independent signals for duplicates.

If the interface or system design itself is unresolved, do not improvise it.
Return a design pivot naming the exact questions for
`/external-design-interface`. Its result must be incorporated into a new plan
revision and pass a fresh `/plan-audit` before re-entry.

## Existing-Library Gate

Run this gate when a mature external library could plausibly replace substantial
custom work. Skip it for bug fixes, refactors, tuning, content, narrow glue, or
logic inseparable from internal engine types.

1. State the capability in one sentence and inspect `ThirdParty/` and
   `ThirdParty/Prebuilts/` for an existing dependency.
2. Identify at most three plausible commercial-friendly candidates. Treat every
   external fact as a separate stable-ID claim packet for a delegated `locator`
   running `/verify-external-claims`:

   ```text
   Claim ID: EGP-EXT-###
   Proposition: <one fact that can be VERIFIED, REFUTED, or UNRESOLVED>
   Applicability: <project version, Windows/MSVC target, flags, or constraints>
   Dependent decision: <why this single fact changes the candidate choice>
   Candidate official source: <URL or exact upstream identifier, if known>
   ```

   License, current release/activity, Windows/MSVC support, C++ compatibility,
   determinism, and required feature support are distinct propositions. Never
   combine them into one verdict. Preserve stable IDs and exact verdicts; use
   only `VERIFIED` facts as established, and expose relevant `UNRESOLVED` facts.
3. Compare verified license, maturity, compatibility, and integration cost with
   the custom scope. Present two or three choices using the normal interaction
   contract: use a candidate, wrap it behind a thin adapter, or hand-roll for a
   specific evidenced reason.
4. If the user chooses a library, stop grilling the superseded custom design and
   return an integration-plan refinement covering vendoring and license notices,
   build/project/filter wiring, namespace/header isolation, adapter boundary,
   swapped call sites, exposed invariants, and acceptance checks. A library
   choice normally changes the work; never report it as "nothing to implement."
5. If the user chooses custom work, return the exact considered-library rejection
   rationale and continue with applicable checklist decisions.

A library or design pivot always returns to the manager for incorporation and a
fresh `/plan-audit`; it never mutates the supplied snapshot in place.

## Plan Context

Consult tracked Plans only when the plan declares a dependency, shares files or
symbols with another Plan, or the closing checks reveal likely overlap. Never
read machine-local scheduler claims. `Documents/Features` is manual and outside
scheduler inventory.

Use the provisioned read-only command:

```text
Tools\WorktreeCli\Platforms\VisualStudio2026\Output\WorktreeCli.exe plan validate --repo <absolute-git-common-dir> --worktree <checkout> --baseline <commit>
```

Require exit `0`, JSON `status: valid`, and `code: ok`; otherwise stop with the
exact diagnostic. Treat `plans` entries (`path`, `createdUtc`, `dependsOn`) as
the tracked inventory. Read only relevant referenced Plan files to compare scope.
Record non-blocking `notices`; do not run a scheduler mutation command.

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

Surface only a concrete material decision established by current evidence. Feed
it through the same two-or-three-choice interaction contract.

## Handoff

Return the complete result inline. `Exact refinements` must preserve one entry
per decision, including the selected choice and precise plan text/section
change. `Plan delta` is relative to the immutable snapshot; any behavior, scope,
architecture, acceptance, or verification change is material.

```text
Plan snapshot: <inline identifier or exact path>
Plan-audit evidence: <audited revision identifier and result>
Plan delta: none | non-material | material
Decisions and exact refinements:
- <decision ID> — <selected choice> — <section and exact refinement>
External claim verdicts:
- <stable claim ID> — VERIFIED | REFUTED | UNRESOLVED — <direct implication>
Execution-control decisions:
- <Tier-3 trigger/role/criterion -> decisive check -> expected result; independent signal when duplicated>
Required next step: none | incorporate refinements | incorporate library integration pivot and run fresh /plan-audit | run /external-design-interface, incorporate its design pivot, and run fresh /plan-audit
Files changed: none
Functions/regions touched: none
Residuals:
- <unresolved decision or none>
```

Questions, answers, exact refinements, pivots, and claim verdicts remain live
handoff data. Never collapse them into a prose summary.
