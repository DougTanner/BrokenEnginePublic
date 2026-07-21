# Pre-Edit Discovery Guardrails

## Context

The Change Workflow requires implementation workers to read current code and search for existing helpers, but `implement-plan` does not state a concrete pre-edit search gate for a new function, type, helper, or substantial logic block. The later `repo-code-review` duplication check can therefore discover an avoidable parallel implementation only after code exists. `resolve-findings` already requires root-cause investigation and stops on unresolved ownership or plan contradictions; this plan must not duplicate or weaken that narrower fix-mode contract.

## Design

1. Audit the workflow's tracked-file editing routes by following the role assignments in root `AGENTS.md` into `implement-plan`, `resolve-findings`, `update-affected-code`, and the mechanical specialist skills. Confirm that `implement-plan` is the only general implementation route where a worker may introduce a new function, type, helper, or substantial logic block. If another general route bypasses it, stop before editing skills and return the exact route as a plan contradiction; expanding this plan beyond `implement-plan` requires an approved plan delta.
2. Add a pre-edit discovery gate to `implement-plan` Phase 1. Before introducing one of those constructs, search with `rg` for the proposed identifier, responsibility terms describing the behavior, the closest sibling implementation, and direct callers or consumers. Read plausible matches before deciding that new code is necessary.
3. Require reuse of an existing mechanism when it satisfies the current contract. When a plausible match does not fit, establish the concrete mismatch in the implementation reasoning; include it in the handoff only when the rejected candidate or new duplication is non-obvious to a reviewer.
4. Separate discoverable uncertainty from product or architecture decisions. Continue read-only inspection for repository facts. If ownership, invariant exposure, or shared-code blast radius remains materially uncertain after the targeted searches, do not edit the affected shared region; return the exact unknown as a residual for the manager to resolve with the user when necessary.
5. Keep the existing implementation handoff schema. The new gate changes pre-edit behavior, not reporting fields, review ownership, or the authority order for plan/repository contradictions.

## Critical files

- `.agents/skills/implement-plan/SKILL.md` — Phase 1 implementation and contradiction-handling rules.
- Root `AGENTS.md` and `.agents/skills/resolve-findings/SKILL.md` — read-only route and overlap evidence; neither is edited by this plan.

## Out of scope

- Adding or changing root `AGENTS.md` content.
- Changing `resolve-findings`, mechanical specialist skills, review skills, delegation roles, or finalization semantics.
- Requiring a repository-wide search before every line edit; the gate applies only to new functions, types, helpers, and substantial logic blocks.
- Runtime code, builds, harness scenarios, or unit tests.

## Acceptance criteria

- The route audit proves `implement-plan` is the sole general creation path, or execution stops with the contradicting route before any skill edit.
- `implement-plan` requires identifier, responsibility, sibling-pattern, and caller/consumer searches before the scoped new constructs are written, followed by reuse or a concrete mismatch.
- The skill distinguishes discoverable repository facts from unresolved architectural or product decisions and stops shared-region edits when material uncertainty remains.
- A fresh-context dry run catches a proposed duplicate helper before editing and returns an unresolved shared-state ownership question without changing code.
- `validate-skill` passes for `implement-plan`; all skill links resolve and `git diff --check` passes.

## Notes

Future implementation is Tier 2 scoped workflow behavior. It changes when implementation may begin but not user authority, delegation, queue, review, reconciliation, or landing contracts. It has no determinism/CRC, `kiVersion`/`.pack`, replay, wire protocol, client/server guard, allocation-tracked runtime, shader, build, or live-harness exposure. Root `AGENTS.md` remains read-only.
