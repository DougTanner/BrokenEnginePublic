<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Pre-Edit Discovery Guardrails

## Context

The Change Workflow requires implementation workers to read current code before editing, and `.agents/skills/implement-plan/SKILL.md` Phase 1 step 1 already requires reading the current implementation, dependencies, helpers, and mirrored patterns before edits — but it states no concrete pre-edit search gate for introducing a new function, type, helper, or substantial logic block. The only backstops are the Phase 2 self-audit bullet "substantial new logic duplicated from an existing helper" and the later `/repo-code-review` duplication check, both of which can discover an avoidable parallel implementation only after code exists. `.agents/skills/resolve-findings/SKILL.md` already requires a falsifiable suspected root cause before editing and stops on plan/repository contradictions; this plan must not duplicate or weaken that narrower fix-mode contract.

## Design

The body is decision-complete: execute the steps below in order; nothing is deferred to the implementer beyond wording and formatting inside the one authorized region.

1. **Route audit (read-only; gates the edit).** Follow the role assignments in root `AGENTS.md` into `.agents/skills/implement-plan/SKILL.md`, `.agents/skills/resolve-findings/SKILL.md`, `.agents/skills/update-affected-code/SKILL.md`, and the mechanical specialist skills (`code-style-review`, `update-vcxproj`). Confirm `implement-plan` is the only general implementation route where a worker may introduce a new function, type, helper, or substantial logic block (`resolve-findings` is fix-mode, `update-affected-code` is search-and-update propagation only, mechanical skills are checklist edits). If another general route bypasses it, stop before editing any skill and return that exact route as a plan contradiction; extending this plan beyond `implement-plan` requires an approved plan delta.
2. **Add the pre-edit discovery gate.** In `.agents/skills/implement-plan/SKILL.md`, insert exactly one new numbered step into the `## Phase 1: Implement` list, after current step 1 (read plan items, `AGENTS.md`, current implementation) and before current step 2 (implement the smallest complete change), renumbering the following steps. The new step requires, before introducing a new function, type, helper, or substantial logic block: search with `rg` for the proposed identifier, for responsibility terms describing the behavior, for the closest sibling implementation, and for direct callers or consumers; then read plausible matches before deciding new code is necessary.
3. **Require reuse or a concrete mismatch** in the same new step: reuse an existing mechanism when it satisfies the current contract; when a plausible match does not fit, establish the concrete mismatch in the implementation reasoning, and include it in the handoff only when the rejected candidate or new duplication is non-obvious to a reviewer.
4. **Separate discoverable uncertainty from decisions** in the same new step: repository facts stay resolvable by continued read-only inspection; if ownership, invariant exposure, or shared-code blast radius remains materially uncertain after the targeted searches, do not edit the affected shared region — return the exact unknown as a residual for the manager to resolve, with the user when necessary.
5. **Preserve every other contract.** The gate changes pre-edit behavior only: the `## Handoff` schema, its reporting fields, review ownership, and the authority order for plan/repository contradictions are unchanged.

## Critical files

- `.agents/skills/implement-plan/SKILL.md` — the `## Phase 1: Implement` numbered list is the sole edit region.
- Root `AGENTS.md`, `.agents/skills/resolve-findings/SKILL.md`, `.agents/skills/update-affected-code/SKILL.md` — read-only route-audit and overlap evidence; never edited by this plan.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria and add no abstractions, configuration, refactors, or fixes to adjacent content encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities the named change requires.

In scope:

- `.agents/skills/implement-plan/SKILL.md`, `## Phase 1: Implement` section only: one inserted numbered step carrying the gate of Design steps 2–4, renumbering of the subsequent existing steps, and at most a minimal tie-in clause in existing step 1 if coherence requires it. No other wording changes inside Phase 1.

Out of scope:

- Any other region of `implement-plan/SKILL.md`: frontmatter, intro, `## Required Brief`, `## Phase 2: Same-Context Audit`, `## Handoff`, and `references/`.
- Adding or changing root `AGENTS.md` content.
- Changing `resolve-findings`, `update-affected-code`, mechanical specialist skills, review skills, delegation roles, or finalization semantics.
- Requiring a repository-wide search before every line edit; the gate applies only to new functions, types, helpers, and substantial logic blocks.
- Runtime code, builds, harness scenarios, or unit tests.

## Risk tier

Tier 2 — scoped workflow behavior in one skill. It changes when implementation may begin but not user authority, delegation, queue, review, reconciliation, or landing contracts. No determinism/CRC, `kiVersion`/`.pack`, replay, wire protocol, client/server guard, allocation-tracked runtime, shader, build, or live-harness exposure. Root `AGENTS.md` remains read-only.

## Acceptance criteria

- The route audit proves `implement-plan` is the sole general creation path, or execution stops with the contradicting route named before any skill edit.
- `implement-plan` Phase 1 requires identifier, responsibility, sibling-pattern, and caller/consumer searches before the scoped new constructs are written, followed by reuse or a concrete recorded mismatch.
- The inserted step distinguishes discoverable repository facts from unresolved architectural or product decisions and stops shared-region edits when material uncertainty remains, returning the exact unknown as a residual.
- A fresh-context dry run of the updated skill catches a proposed duplicate helper before editing and returns an unresolved shared-state ownership question without changing code.
- `/validate-skill` passes for `implement-plan`; all skill links resolve and `git diff --check` passes.
