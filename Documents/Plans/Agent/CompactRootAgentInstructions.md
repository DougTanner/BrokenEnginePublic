# Compact root agent instructions without semantic change

## Context

The root `AGENTS.md` is a cross-cutting instruction hub. The authoritative `bt-token-v1` measurement is currently 4,959 tokens (19,833 normalized bytes, 147 lines), 959 above the 4,000 hub target defined by `.agents/skills/update-claude-docs/SKILL.md`. The effective root-to-Documents-to-Plans chain is 8,081 tokens and the root-to-Documents-to-Features chain is 6,836, both below the 15,000 chain target, so the excess is advisory rather than an immediate context-safety failure.

The increase came from the approved seven-stage, risk-tiered C++ change-process rewrite and its reporting/finalization safety rules. Removing text during that implementation would have risked weakening the approved process or expanding an already bounded change. No live plan owns a semantic-preserving compaction of the root hub.

## Design

1. Inventory every normative contract in the current root `AGENTS.md`, with explicit coverage for the three risk tiers; all seven named stages; convergence and loopback rules; role/model selection; delegated immutable reporting; context lifecycle; authority order; diagnosis discipline; final verification and user-approved landing; and the engine-wide implementation invariants.
2. Reduce duplication and verbose restatement using concise prose, tables, and links to existing authoritative child/reference documents where the effective instruction chain still exposes the rule. Preserve root-level text for cross-cutting gates whose omission could change behavior.
3. Do not change process triggers, risk classification, required evidence, role authority, acceptance semantics, blocking behavior, or user approval. If a candidate compression has ambiguous semantic equivalence, retain the original rule.
4. Keep directory and subsystem routing usable from the root, with all referenced files and anchors resolving. Do not move requirements into an unlinked document or create a second source of truth.
5. Measure the final root and affected effective chains with `.agents/scripts/Measure-Tokens.ps1`; run the existing process regression/link checks and documentation review against the final bytes.

## Critical files

- `AGENTS.md` — semantic-preserving compaction of the cross-cutting hub.
- `.agents/references/cpp-change-process-regression.md` — existing behavioral regression matrix for the seven-stage process; update only if link/anchor maintenance is required, without changing expected behavior.
- Existing linked `AGENTS.md` and reference documents — authoritative destinations for deduplicated detail; touch only when necessary to keep links and ownership unambiguous.
- `.agents/scripts/Measure-Tokens.ps1` and `.agents/skills/update-claude-docs/SKILL.md` — measurement and target authorities; no behavior change expected.

## Out of scope

- Changing any C++ Code Change Process behavior, risk tier, role trigger, report contract, verification requirement, finalization gate, or engine invariant.
- Broad rewrites of child `AGENTS.md` files, skills, plans, or reference documents merely to improve prose.
- Raising the size target, changing the token metric, hiding required instructions outside the effective chain, or adding new workflow capabilities.
- Engine/runtime changes or unit tests.

## Acceptance criteria

- Root `AGENTS.md` measures at or below 4,000 `bt-token-v1` with `Measure-Tokens.ps1`.
- A before/after contract inventory demonstrates that every current risk-tier, seven-stage, reporting, convergence, verification, reconciliation, finalization, user-approval, diagnosis, and engine-invariant semantic remains effective with no process behavior change.
- The effective root-to-Documents-to-Plans and root-to-Documents-to-Features chains remain below 15,000 `bt-token-v1`; any other affected chain is measured and remains within its existing target/warning contract.
- Every changed Markdown link and referenced path resolves, no authoritative rule is duplicated inconsistently, and the C++ change-process regression reference retains the same expected outcomes.
- Documentation review, applicable link/regression checks, and `git diff --check` pass on the final tree.

## Notes

- Documentation-only structural debt; no engine runtime, determinism/CRC, wire, save/replay, `.pack`, shader, allocation-tracked, build, or client/server guard exposure.
- Score: Effort 2, Impact 2, Risks 1, total 1; Tier Small. The expected reduction is 959 `bt-token-v1`, with low but nonzero workflow-semantic risk despite documentation-only bytes.
