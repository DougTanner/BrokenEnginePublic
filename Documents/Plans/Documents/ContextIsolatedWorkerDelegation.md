<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-19T20:21:44.000Z","dependsOn":[]} -->
# Context-Isolated Worker Delegation

## Context

Core Change Workflow delegation currently permits workers to inherit broad conversation context and describes temporary-file-only handoffs. That exposes unrelated manager context to bounded workers, encourages repeated evidence forwarding, and conflicts with the intended manager-routed return path. The delegation contract needs one client-neutral brief format and explicit context-isolation defaults before manager-only routing can rely on it.

## Design

- Make Codex workers default to `fork_turns:"none"` and give Claude workers fresh, self-contained prompts. Use the smallest positive Codex turn fork only when exact conversation text is authoritative and cannot safely be summarized.
- Require every core delegation prompt to include these client-neutral records: `Delegation basis: mandated <workflow/skill> | task owner <bounded concern>` and `Context: none — Codex | fresh — Claude | turns <N> — Codex exception: <reason>`.
- Standardize the bounded brief fields: role and objective; in-scope and out-of-scope work; fixed user decisions; governing instructions and artifact paths; known files, symbols, or regions; baseline/tree when material; acceptance check; and return format.
- Start workers from the named artifacts. Expand beyond them only for a concrete dependency, decision, or failure, and verify every supplied hint against the current tree before relying on it.
- Replace the temporary-file-only handoff rule with manager-mediated concise inline returns. For large evidence, return an existing artifact or log path plus the selector needed to recover the relevant evidence. Create a `Temp/` artifact only when the owning workflow requires one.
- Define a recovery capsule for interrupted or resumed work: objective and scope, baseline/tree, completed evidence, unresolved issue, and next action. The manager routes the capsule; workers do not route work to other workers.

## Critical files

- Root `AGENTS.md` context-management and subagent handoff contract.
- `.agents/references/subagent-reporting.md` delegation, evidence, and recovery handoff formats.
- Core Change Workflow skills that construct worker prompts or receive their handoffs: `.agents/skills/next-plan/SKILL.md`, `.agents/skills/implement-plan/SKILL.md`, `.agents/skills/resolve-findings/SKILL.md`, `.agents/skills/verify-changes/SKILL.md`, and `.agents/skills/finalize-changes/SKILL.md`.

## Out of scope

- Manager-only core routing, no-spawn or core gate conversion, and transcript enforcement.
- Model or effort changes, a host-level hard one-agent cap, ConceptRAG, and token or compaction constants.
- Specialty workflow redesign outside the normal Change Workflow.

## Acceptance criteria

- The effective root and core-skill contracts consistently specify fresh/none context defaults, the two required prompt records, bounded brief fields, manager routing, evidence references, and recovery capsules without retaining the temporary-file-only contradiction.
- A static sweep of core context call sites finds fresh/none delegation with no omitted fixed user decision or governing artifact.
- Every changed skill passes `/validate-skill`.
- A fresh fork-none coherence review completes from the standardized brief without a missing-context recovery request.

## Notes

Future implementation is Tier 2 scoped workflow behavior. It does not expose engine/runtime behavior, determinism or CRC state, protocol, save/replay compatibility, data layout, client/server guard scope, build/bootstrap coordination, or allocation-tracked paths. This plan owns only the context, brief, handoff, and recovery contract; it is the directional prerequisite for `ManagerOnlyCoreWorkflow.md`, which consumes that contract without redefining it.
