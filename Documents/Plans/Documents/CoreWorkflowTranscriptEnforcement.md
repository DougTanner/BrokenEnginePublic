<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-19T20:21:44.000Z","dependsOn":["Documents/Plans/Documents/ManagerOnlyCoreWorkflow.md"]} -->
# Core Workflow Transcript Enforcement

## Context

Manager-only routing needs a postmortem check that distinguishes compliant orchestration from inherited-context work, nested or duplicate agents, raw-evidence forwarding, and repeated work after recovery. `/next-plan-review` already owns one-reviewer retrospective analysis, so enforcement belongs in that existing review rather than in a new stage or tool.

## Design

- Extend `/next-plan-review` to verify the `Context` and `Delegation basis` records on core delegations and flag inherited Codex turns unless the smallest positive fork carries a concrete exception explaining why authoritative conversation text could not be summarized.
- Verify manager-only core activity, a depth-one worker tree, one bounded worker per concern, no duplicate search/restatement/consensus agents, no raw transcript or log forwarding when an artifact path plus selector is sufficient, and recovery by capsule/resume rather than repeated completed work.
- Treat mandatory fresh review, independent verification, and explicitly required disjoint fan-out as required independence, not duplication.
- Give the existing transcript reviewer fresh/none context with commit facts and transcript paths in a self-contained brief. Preserve the current one-reviewer shape; add no stage, reviewer, or tooling.
- Keep findings evidence-based by citing the delegation record, transcript event, artifact selector, or repeated operation that proves each compliance result.

## Critical files

- `.agents/skills/next-plan-review/SKILL.md` and `.agents/skills/next-plan-review/scripts/Find-AgentSessionTranscript.ps1`.
- `.claude/agents/reviewer.md`, the existing reviewer role contract used by `/next-plan-review`.
- `Documents/Plans/Documents/ManagerOnlyCoreWorkflow.md`, which must land first; `ContextIsolatedWorkerDelegation.md` is its transitive prerequisite.

## Out of scope

- Host-level enforcement, changes to core routing contracts, and specialty-workflow auditing.
- Automatic token accounting and model, effort, or global configuration changes.
- New transcript stages, tools, fixtures, or unit tests.

## Acceptance criteria

- Every changed skill passes `/validate-skill`.
- A targeted transcript scenario reports compliant `Context` and `Delegation basis` records and catches an intentionally evidenced violation when an existing fixture or mechanism already supports that check; no unit test or new fixture is added.
- The report verifies manager-only activity, worker depth, duplicate-effort exclusions, evidence forwarding, and recovery behavior without penalizing mandatory independent roles or disjoint fan-out.
- The transcript reviewer itself runs from a fresh/none self-contained brief containing commit facts and transcript paths.

## Notes

Future implementation is Tier 2 scoped postmortem workflow behavior. It does not expose engine/runtime behavior, determinism/CRC state, protocol, save/replay compatibility, data layout, client/server guard scope, build/bootstrap coordination, or allocation-tracked paths. This plan depends directionally on `ManagerOnlyCoreWorkflow.md` and transitively on `ContextIsolatedWorkerDelegation.md`.
