<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-19T20:21:44.000Z","dependsOn":[]} -->
# Context-Isolated Worker Delegation

## Context

Core Change Workflow delegation still permits workers to inherit broad conversation context, and the root contract still routes subagent-to-subagent handoffs through temporary files (root `AGENTS.md`, "Subagents" bullet: "Subagent-to-subagent handoffs go through temporary files; return only the file paths to the parent session"). That exposes unrelated manager context to bounded workers, encourages repeated evidence forwarding, and conflicts with the intended manager-routed return path.

Parts of the target contract already exist: `.agents/references/subagent-reporting.md` "Default handoff" already defines the concise inline return as the norm, and `/implement-plan` already dispatches its single worker with `fork_turns: "none"` (Codex) or a fresh self-contained prompt (Claude) via `.agents/skills/implement-plan/references/client-compatibility.md`. What is missing is one client-neutral delegation contract applied uniformly across the core skills: context-isolation defaults stated at the root, two required prompt records, standardized brief fields, removal of the surviving temporary-file-only handoff rule, evidence-by-reference for large returns, and a recovery capsule for interrupted work. This plan owns only the context, brief, handoff, and recovery contract; it is the directional prerequisite for `ManagerOnlyCoreWorkflow.md`, which consumes that contract without redefining it.

## Design

- **Context-isolation defaults.** Codex workers default to `fork_turns:"none"`; Claude workers receive fresh, self-contained prompts. The smallest positive Codex turn fork is permitted only when exact conversation text is authoritative and cannot safely be summarized. State this default once in the root `AGENTS.md` "Subagents" bullet list; keep `/implement-plan`'s `references/client-compatibility.md` a consistent skill-local restatement, not a second definition.
- **Two required prompt records.** Every core delegation prompt includes these client-neutral records, defined canonically in `.agents/references/subagent-reporting.md` and required by the root `AGENTS.md` "Subagents" contract:
  - `Delegation basis: mandated <workflow/skill> | task owner <bounded concern>`
  - `Context: none — Codex | fresh — Claude | turns <N> — Codex exception: <reason>`
- **Standardized bounded brief fields**, defined once in `.agents/references/subagent-reporting.md`: role and objective; in-scope and out-of-scope work; fixed user decisions; governing instructions and artifact paths; known files, symbols, or regions; baseline/tree when material; acceptance check; and return format. `/implement-plan` "Required Brief" and `/resolve-findings` "Required Assignment" align their field lists to this canon (they may keep skill-specific fields such as mode, ownership snapshot, and classification; they must not omit or contradict a canonical field).
- **Workers start from the named artifacts.** Expand beyond them only for a concrete dependency, decision, or failure, and verify every supplied hint against the current tree before relying on it.
- **Manager-mediated inline returns replace the temporary-file-only rule.** Rewrite the root `AGENTS.md` bullet "Subagent-to-subagent handoffs go through temporary files; return only the file paths to the parent session" to: workers return concise inline handoffs to the manager, who routes them; for large evidence, return an existing artifact or log path plus the selector needed to recover the relevant evidence; create a `Temp/` artifact only when the owning workflow requires one. `.agents/references/subagent-reporting.md` "Default handoff" remains the authoritative return format and gains the evidence-by-reference rule.
- **Recovery capsule** for interrupted or resumed work, added to `.agents/references/subagent-reporting.md` alongside the existing "Liveness and interruption" continuation guidance: objective and scope, baseline/tree, completed evidence, unresolved issue, and next action. The manager routes the capsule; workers do not route work to other workers.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent content encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (link targets, cross-references) the named change requires.

In scope:

- Root `AGENTS.md` — only the bullet list under "### Subagents" in "IMPORTANT: Context management and agent selection": the bullet "Give subagents only the instructions and context their task needs; they return a concise, clearly defined response" (add the context-isolation default and required prompt records) and the bullet "Subagent-to-subagent handoffs go through temporary files; return only the file paths to the parent session" (replace per Design). No other section of `AGENTS.md`.
- `.agents/references/subagent-reporting.md` — the "Default handoff" section (evidence-by-reference rule) and new canonical content for the two prompt records, bounded brief fields, and recovery capsule; the "Liveness and interruption" continuation paragraph may reference the capsule. The "Final-evidence gate" section is untouched.
- `.agents/skills/next-plan/SKILL.md` — only the delegation sentences: Workflow step 3 ("Tier 2 delegates `/plan-audit` to one `reviewer`" and the Tier 3 `/plan-audit` delegation line) and Workflow step 5 ("Every implementer delegation prompt carries the truth-grounding guardrail..."), extended to carry the two required records. The claim/sidecar workflow, "Primary advance", and "Handoff before approval" sections are untouched.
- `.agents/skills/implement-plan/SKILL.md` — only the opening dispatch paragraph and the "## Required Brief" field list (align to the canonical brief fields); `.agents/skills/implement-plan/references/client-compatibility.md` stays consistent with the root default. Phases 1-2 and "## Handoff" are untouched.
- `.agents/skills/resolve-findings/SKILL.md` — only the "## Required Assignment" field list (align to the canonical brief fields). "## Fix Workflow" and "## Handoff" are untouched.
- `.agents/skills/verify-changes/SKILL.md` — only the "## Inputs" handoff enumeration, if and only if the renamed handoff contract makes its wording incorrect. All audit sections and "## Decision and Output" are untouched.
- `.agents/skills/finalize-changes/SKILL.md` — only the "## Inputs" list, if and only if the renamed handoff contract makes its wording incorrect. All sidecar, workflow, and output sections are untouched.

## Out of scope

- Manager-only core routing, no-spawn or core gate conversion, and transcript enforcement (owned by `ManagerOnlyCoreWorkflow.md` and `CoreWorkflowTranscriptEnforcement.md`).
- Model or effort changes, a host-level hard one-agent cap, ConceptRAG, and token or compaction constants.
- Specialty workflow redesign outside the normal Change Workflow; any skill not named in scope.
- Handoff report formats already defined in the named skills' "## Handoff" sections and `subagent-reporting.md` "Final-evidence gate".

## Risk tier and invariants

Tier 2 — scoped workflow/tool behavior. It exposes no engine/runtime behavior, determinism or CRC state, protocol, save/replay compatibility, data layout, client/server guard scope, build/bootstrap coordination, or allocation-tracked paths. Invariant: the delegation-report contract consumed by `/verify-changes` (inline acceptance table, `Build required` line) must remain valid after the edits.

## Acceptance criteria

- The effective root and core-skill contracts consistently specify fresh/none context defaults, the two required prompt records, the canonical bounded brief fields, manager routing, evidence references, and recovery capsules, with no surviving temporary-file-only handoff statement in root `AGENTS.md`.
- A static sweep of the in-scope delegation call sites (the named skill regions) finds fresh/none delegation with no omitted fixed-user-decision or governing-artifact brief field.
- Every changed skill passes `/validate-skill`.
- A fresh fork-none coherence review completes from the standardized brief without a missing-context recovery request.
