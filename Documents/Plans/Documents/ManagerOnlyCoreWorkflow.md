# Manager-Only Core Workflow

## Context

The core Change Workflow assigns the main session both user-facing orchestration and task-evidence mechanics. That mixes repository inspection, edits, checks, review evidence, queue operations, reconciliation, and landing state into the context that must preserve user intent and adjudicate handoffs. After context-isolated briefs exist, the core path can make the main a manager while preserving all existing approval, independence, queue, verification, and landing semantics.

## Design

- Limit the main session to mandatory skill-instruction reading, retaining user intent and fixed decisions, dispatching or resuming workers, adjudicating concise handoffs, asking user questions, and presenting plans, verification results, and landing summaries.
- Prohibit the main from inspecting task repository code, diffs, logs, or transcripts; editing tracked artifacts; building, testing, or reviewing; and performing queue, Git, reconciliation, or landing mechanics.
- Split the core flow into explicit ownership stages: a preparation worker handles queue claim, current-code inspection, and resolved-plan preparation; the main conducts the approval interaction; the main dispatches bounded implementation, propagation, build, review, hygiene, and fix roles; a verification worker assembles acceptance evidence and reports any separate-role requirement to the main; a finalization worker reconciles and prepares the exact mutation summary; the main obtains confirmation; and the finalization worker performs the authorized primary mutation.
- Keep core workers depth one: they never spawn workers. Compatible specialist skills execute in the assigned worker context; work requiring a separate independent role returns through the existing concise handoff so the main can dispatch it.
- Remove inline fresh-review fallbacks from core worker contracts. If a mandatory reviewer is unavailable, report a blocker instead of substituting same-context review.
- Assign one bounded worker per concern. Do not dispatch duplicate searches, restatements, consensus agents, or speculative workers. Preserve mandatory independent revalidation and explicitly required disjoint fan-out.
- Preserve canonical approval and landing semantics, WorktreeCli-only queue mutation, live-claim rules, exact-primary-mutation confirmation, and ownership of staged requests, receipts, and other sidecars.

## Critical files

- Root `AGENTS.md` Change Workflow, context-management, delegation-role, and approval/finalization contracts.
- Core orchestration skills under `.agents/skills/`: `next-plan/SKILL.md`, `plan-audit/SKILL.md`, `implement-plan/SKILL.md`, `update-affected-code/SKILL.md`, `compile/SKILL.md`, `repo-code-review/SKILL.md`, `glsl-review/SKILL.md`, `resolve-findings/SKILL.md`, `code-style-review/SKILL.md`, `validate-skill/SKILL.md`, `update-claude-docs/SKILL.md`, `update-vcxproj/SKILL.md`, `verify-changes/SKILL.md`, `session-audit/SKILL.md`, and `finalize-changes/SKILL.md`.
- `.agents/references/subagent-reporting.md` and the execution-gate, queue, claim, staged-request, receipt, and landing references consumed by those skills.
- `Documents/Plans/Documents/ContextIsolatedWorkerDelegation.md`, which must land first.

## Out of scope

- `analyze-diagsession`, `reduce-file`, external architecture/refactor/deep analysis, `external-design-interface`, `external-skill-creator`, and Tier-3 grill internals.
- Engine or runtime behavior, model choice, effort selection, and host-level hard tool enforcement.
- Changing canonical user approval, queue ownership, reconciliation, landing, or sidecar semantics.
- The context/brief/handoff format owned by `ContextIsolatedWorkerDelegation.md` and postmortem enforcement owned by `CoreWorkflowTranscriptEnforcement.md`.

## Acceptance criteria

- A static sweep proves core main-session contracts assign no task-evidence inspection, edit, build, test, review, queue, Git, reconciliation, or landing mechanics to the main.
- Core worker-only skills contain no child-spawn instruction and no inline fresh-review fallback.
- Every changed skill passes `/validate-skill`.
- A live dogfood transcript shows the main performing only instruction reading, orchestration, adjudication, user questions, and user presentation; a depth-one worker tree; every task edit and check delegated; and no missing-context recovery.
- The final-evidence gate decisively covers preserved queue, confirmation, reconciliation, landing, receipt, and claim-release contracts.

## Notes

Future implementation is Tier 3 because it spans queue selection, independent role boundaries, verification, reconciliation, and landing. Execution card: goal—move core mechanics out of manager context; boundary—normal Change Workflow only; risk triggers—queue mutation, independent review, reconciliation, and primary landing; invariant exposure—approval and landing semantics, WorktreeCli ownership, claims, staged requests, receipts, and sidecars, with no engine determinism/CRC/protocol/save/runtime exposure; checks—skill validation, static ownership sweeps, dogfood transcript, and final-evidence acceptance table; roles—planner/reviewer gates remain independent, bounded workers perform mechanics, and main retains user decisions and confirmations. Run `/plan-audit` before implementation and use `/external-grill-plan` only if the audit or current tree exposes material architectural ambiguity. This plan depends directionally on `ContextIsolatedWorkerDelegation.md`.
