<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-19T20:21:44.000Z","dependsOn":["Documents/Plans/Documents/ContextIsolatedWorkerDelegation.md"]} -->
# Manager-Only Core Workflow

## Context

The core Change Workflow assigns the main session both user-facing orchestration and task-evidence mechanics. That mixes repository inspection, edits, checks, review evidence, Plan-claim operations, reconciliation, and landing state into the context that must preserve user intent and adjudicate handoffs. Once `ContextIsolatedWorkerDelegation.md` (metadata prerequisite) has landed its context/brief/handoff format, the core path can make the main session a pure manager while preserving all existing approval, independence, claim, verification, and landing semantics.

This is a documentation/skill-contract change only: it rewrites who performs each core-workflow mechanic, not what any mechanic does.

## Design

- Limit the main session to mandatory skill-instruction reading, retaining user intent and fixed decisions, dispatching or resuming workers, adjudicating concise handoffs, asking user questions, and presenting plans, verification results, and landing summaries.
- Prohibit the main from inspecting task repository code, diffs, logs, or transcripts; editing tracked artifacts; building, testing, or reviewing; and performing Plan-claim, Git, reconciliation, or landing mechanics.
- Split the core flow into explicit ownership stages: a preparation worker handles executable Plan claim, current-code inspection, and resolved-plan preparation; the main conducts the approval interaction; the main dispatches bounded implementation, propagation, build, review, hygiene, and fix roles; a verification worker assembles acceptance evidence and reports any separate-role requirement to the main; a finalization worker reconciles and prepares the exact mutation summary; the main obtains confirmation; and the finalization worker performs the authorized primary mutation.
- Keep core workers depth one: they never spawn workers. Compatible specialist skills execute in the assigned worker context; work requiring a separate independent role returns through the existing concise handoff so the main can dispatch it.
- Remove inline fresh-review fallbacks from core worker contracts. If a mandatory reviewer is unavailable, report a blocker instead of substituting same-context review. (Existing instance: `.agents/skills/next-plan/SKILL.md` — the "If reviewer delegation is unavailable … `review freshness degraded`" clause.)
- Assign one bounded worker per concern. Do not dispatch duplicate searches, restatements, consensus agents, or speculative workers. Preserve mandatory independent revalidation and explicitly required disjoint fan-out.
- Preserve canonical approval and landing semantics, WorktreeCli-only scheduler mutation, live-claim rules, exact-primary-mutation confirmation, and receipt/sidecar ownership. Every edit rewrites role assignment (who runs the step, in which context) without changing any step's semantics.

## Scope contract

The scope below is both target and ceiling. The implementer makes the smallest complete change satisfying the acceptance criteria and adds no abstractions, configuration, refactors, new skills, new reference files, or fixes to adjacent prose or code it encounters. Naming a file grants permission to touch only the named regions in it, plus the mechanical necessities (frontmatter `allowed-tools` lists, intra-file links, cross-references to renumbered sections) that the named change forces.

### In scope (bounded regions)

- Root `AGENTS.md`:
  - "IMPORTANT: Context management and agent selection" — the "Subagents" bullet list and "Delegation roles" prose, only the sentences assigning execution context, depth, dispatch, and handoff behavior.
  - "IMPORTANT: Change Workflow" — the Definitions block and steps 1–8 plus "Convergence", only the sentences that say which session (main vs. worker) performs claim, inspection, edit, build, review, hygiene, verification, reconciliation, confirmation, and landing mechanics. Approval gates, tier definitions, gate triggers, and every step's semantics stay unchanged.
- `.agents/skills/<name>/SKILL.md` for exactly these core skills: `next-plan`, `plan-audit`, `implement-plan`, `update-affected-code`, `compile`, `repo-code-review`, `glsl-review`, `resolve-findings`, `code-style-review`, `validate-skill`, `update-claude-docs`, `update-vcxproj`, `verify-changes`, `session-audit`, `finalize-changes`. In each file, only: (a) the passages naming the executing role/context (main vs. delegated worker) and any child-spawn instruction; (b) the report/handoff passages, restated to the concise handoff so the main can adjudicate without task evidence; (c) any inline fresh-review or same-context-review fallback (replace with a reported blocker). All command sequences, gates, evidence rules, and script contracts in these skills stay byte-identical apart from those passages.
- `.agents/references/subagent-reporting.md` — only the passages stating who consumes handoffs and at what depth; the report format itself is owned by `ContextIsolatedWorkerDelegation.md` and is not redesigned here.
- `.agents/skills/next-plan/references/execution-gates.md` and `.agents/skills/next-plan/references/tier3-workflow.md` — only the sentences assigning gate/stage mechanics to the main session; gate states and semantics unchanged.

### Out of scope

- `analyze-diagsession`, `reduce-file`, `external-architecture-review`, `external-refactor-clean`, `external-deep-analysis`, `external-design-interface`, `external-skill-creator`, and `external-grill-plan` (Tier-3 grill) internals.
- All scripts under `.agents/skills/*/scripts/` and `.agents/scripts/` — role reassignment is prose-only; WorktreeCli and script mechanics do not change.
- Engine or runtime behavior, model choice, effort selection, and host-level hard tool enforcement.
- Changing canonical user approval, Plan-claim ownership, reconciliation, landing, or sidecar semantics.
- The context/brief/handoff format owned by `Documents/Plans/Documents/ContextIsolatedWorkerDelegation.md` and postmortem enforcement owned by `Documents/Plans/Documents/CoreWorkflowTranscriptEnforcement.md`.

## Critical files

- Root `AGENTS.md` (regions above).
- The fifteen core `SKILL.md` files listed in the scope contract.
- `.agents/references/subagent-reporting.md`.
- `.agents/skills/next-plan/references/execution-gates.md`, `.agents/skills/next-plan/references/tier3-workflow.md`.
- `Documents/Plans/Documents/ContextIsolatedWorkerDelegation.md` — prerequisite recorded in metadata; must land first.

## Risk tier

Tier 3: the change spans executable Plan selection, independent role boundaries, verification, reconciliation, and landing contracts. Invariant exposure: approval and landing semantics, WorktreeCli-only scheduler mutation, tracked metadata, claims, receipts, and sidecars. No engine determinism/CRC/protocol/save/runtime exposure. Run `/plan-audit` before implementation, then `/external-grill-plan` per the Tier-3 workflow.

## Acceptance criteria

- A static sweep of root `AGENTS.md` and the fifteen core skills proves the main-session contracts assign no task-evidence inspection, edit, build, test, review, Plan-claim, Git, reconciliation, or landing mechanics to the main.
- Core worker-only skills contain no child-spawn instruction and no inline fresh-review fallback (the `next-plan` inline-review clause is removed or converted to a reported blocker).
- Every changed skill passes `/validate-skill`.
- A live dogfood transcript shows the main performing only instruction reading, orchestration, adjudication, user questions, and user presentation; a depth-one worker tree; every task edit and check delegated; and no missing-context recovery.
- The final-evidence gate decisively covers preserved metadata validation, confirmation, reconciliation, landing, receipt, and claim-release contracts.

## Notes

Execution card: goal — move core mechanics out of manager context; boundary — normal Change Workflow only, regions per the scope contract; risk triggers — claim mutation, independent review, reconciliation, and primary landing; checks — skill validation, static ownership sweeps, dogfood transcript, and final-evidence acceptance table; roles — planner/reviewer gates remain independent, bounded workers perform mechanics, and main retains user decisions and confirmations.
