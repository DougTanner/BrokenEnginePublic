<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-19T20:21:44.000Z","dependsOn":["Documents/Plans/Documents/ManagerOnlyCoreWorkflow.md"]} -->
# Core Workflow Transcript Enforcement

## Context

Manager-only routing needs a postmortem check that distinguishes compliant orchestration from inherited-context work, nested or duplicate agents, raw-evidence forwarding, and repeated work after recovery. `/next-plan-review` (`.agents/skills/next-plan-review/SKILL.md`) already owns one-reviewer retrospective transcript analysis — its `## Fresh transcript analysis` section dispatches exactly one fresh `reviewer` over the proven parent/child transcripts, and its `## Reconstruct and assess` section defines the ordered assessment — so enforcement belongs inside that existing review, not in a new stage or tool. The delegation records this plan verifies (`Delegation basis: ...` and `Context: none — Codex | fresh — Claude | turns <N> — Codex exception: <reason>`) are defined by `ContextIsolatedWorkerDelegation.md`; the manager-only routing rules (manager-only core activity, depth-one workers, one bounded worker per concern, capsule recovery) are defined by `ManagerOnlyCoreWorkflow.md`. Both land before this plan per metadata; this plan adds only the retrospective enforcement of those already-defined contracts.

## Design

- In `## Fresh transcript analysis` of `next-plan-review/SKILL.md`, extend the single fresh reviewer's required analysis to verify the `Context` and `Delegation basis` records on every core delegation event found in the proven transcripts, and to flag any inherited-context Codex turn fork unless it is the smallest positive fork carrying a concrete exception reason explaining why authoritative conversation text could not be summarized. The reviewer keeps its existing fresh/none, self-contained brief (commit facts, sanitized transcript locators, trust rules, targeted event ranges) — no new context channel.
- In `## Reconstruct and assess`, add compliance verification alongside the existing five assessment concerns: manager-only core activity, a depth-one worker tree, one bounded worker per concern, no duplicate search/restatement/consensus agents, no raw transcript or log forwarding where an artifact path plus selector is sufficient, and recovery by capsule/resume rather than repetition of completed work.
- State the independence exclusions with the checks: mandatory fresh review, independent verification, and explicitly required disjoint fan-out are required independence, never flagged as duplication. This extends, and must not weaken, the section's existing anti-false-positive rule that repetition claims require proof of unchanged inputs.
- Every compliance result cites its evidence: the delegation record text, transcript event (session ID plus timestamp or event/line location, matching the skill's existing citation rule), artifact selector, or the concrete repeated operation.
- Compliance findings are reported inside the existing `## Report` template's `## Findings by concern` sections (token efficiency and process overhead are the natural homes); add no new report stage, reviewer, or section structure beyond what carrying these findings requires. Preserve the current one-reviewer shape end to end.

## Critical files

- `.agents/skills/next-plan-review/SKILL.md` — sole edit target.
- Read-only references: `.agents/skills/next-plan-review/scripts/Find-AgentSessionTranscript.ps1` (transcript discovery mechanics, unchanged), `.claude/agents/reviewer.md` (existing reviewer role contract, unchanged), `Documents/Plans/Documents/ManagerOnlyCoreWorkflow.md` and `Documents/Plans/Documents/ContextIsolatedWorkerDelegation.md` (the contracts being enforced; the former must land first, the latter is its transitive prerequisite).

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change and add no abstractions, configuration, refactors, or fixes to adjacent content encountered along the way.

In scope — only these regions of `.agents/skills/next-plan-review/SKILL.md`:

- `## Fresh transcript analysis`: the reviewer's required analysis and brief contents, adding the delegation-record and inherited-turn checks.
- `## Reconstruct and assess`: the compliance checks, independence exclusions, and their evidence-citation requirement.
- `## Report`: only the minimal wording needed for compliance findings to land in the existing `## Findings by concern` sections.

Naming this file grants no permission to touch anything else in it: the frontmatter, `## Prove provenance`, the provenance/trust rules, the handoff block format, and the report template structure stay unchanged except for the minimal report wording named above.

Out of scope:

- Host-level enforcement, changes to core routing contracts (the two prerequisite plans own those), and specialty-workflow auditing.
- Automatic token accounting and model, effort, or global configuration changes.
- New transcript stages, tools, scripts, fixtures, or unit tests; any edit to `Find-AgentSessionTranscript.ps1`, `.claude/agents/reviewer.md`, or `next-plan-review/agents/openai.yaml`.

## Risk tier

Tier 2 — scoped postmortem workflow behavior in one skill. It exposes no engine/runtime behavior, determinism/CRC state, protocol, save/replay compatibility, data layout, client/server guard scope, build/bootstrap coordination, or allocation-tracked paths. Invariants to preserve: the one-reviewer shape, fresh/none reviewer context, transcript trust rules, and the existing proof requirements for repetition findings.

## Acceptance criteria

- Every changed skill passes `/validate-skill`.
- A targeted transcript scenario reports compliant `Context` and `Delegation basis` records and catches an intentionally evidenced violation when an existing fixture or mechanism already supports that check; no unit test or new fixture is added.
- The revised skill text requires verification of manager-only activity, worker depth, duplicate-effort exclusions, evidence forwarding, and recovery behavior without penalizing mandatory independent roles or disjoint fan-out.
- The transcript reviewer still runs from a fresh/none self-contained brief containing commit facts and transcript paths.

## Notes

Depends directionally on `ManagerOnlyCoreWorkflow.md` (metadata edge) and transitively on `ContextIsolatedWorkerDelegation.md`. If either prerequisite lands with different record names or routing rules than described here, the enforcement wording follows the landed contracts, not this plan's quotations of them.
