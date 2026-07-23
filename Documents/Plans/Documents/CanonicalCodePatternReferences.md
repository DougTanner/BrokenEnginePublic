<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Canonical Code Pattern References

## Context

`update-claude-docs` discourages naming newly added symbols but does not define when a stable source exemplar should replace procedural prose. `repo-code-review` consequently explains several established patterns inline even though current code already demonstrates distinct policies for corrupt network input, transactional save loading, boot-required packed data, workbuffer dispatch, and allocation-free dynamic log construction. A constrained exemplar policy can improve pattern matching without turning AGENTS.md or skills into file inventories.

## Design

1. Add a narrow canonical-exemplar rule to `update-claude-docs`: a source reference may replace procedural prose when it identifies one stable file-and-symbol exemplar, states the concern and applicability variant, and preserves the invariant in documentation. Allow at most three exemplars per concern; reject one-off code introduced by the current change as canonical until an established repository pattern supports it.
2. Reconcile the rule with the existing “do not name-drop code you just added” and high-level-documentation guidance. Source exemplars demonstrate implementation shape; AGENTS.md and skill prose remain authoritative for ownership, constraints, and reasons. When a cited symbol changes during a documentation sync, verify that it still demonstrates the labeled pattern or retarget the reference in the same edit.
3. Pilot the policy in `repo-code-review` by replacing overlapping procedural explanation with labeled references to:
   - `Client::Receive` and `Server::Receive` for parse-before-mutation and single-packet handling at the network trust boundary.
   - `GameSaveLoad::ReadGrid` for transactional cleanup and `false` return on invalid persisted state.
   - `LoadAnimationDataFromEagerChunks` for logging and rethrowing corruption in boot-required packed data.
   - `GameBase::BuildAndDispatchFrameTicks` for scoped workbuffer-backed temporary dispatch data.
   - `CrcValidateLoop` in `ReconcileReplayCrc.cpp` for `ScopedWorkbufferArena` dynamic log construction.
4. Keep the three trust-boundary policies distinct; never present one universal error-handling shape. Retain the allocation tracker, LOG safety, and trust-boundary invariants needed to choose the exemplar, while deleting duplicated mechanics that the cited source makes clear.
5. Run a reference-maintenance sweep over the two changed skills: each path resolves, each symbol exists, each label matches current behavior, and no exemplar is duplicated elsewhere in the same effective instruction chain. Neither skill may increase in bt-token-v1 size; references must replace prose rather than accumulate beside it.

## Critical files

- `.agents/skills/update-claude-docs/SKILL.md` — canonical-exemplar authoring and maintenance policy.
- `.agents/skills/repo-code-review/SKILL.md` — pilot references in trust-boundary and allocation/workbuffer review guidance.
- The six cited source symbols — read-only evidence used to validate applicability.

## Out of scope

- Adding exemplar references to root `AGENTS.md` or creating a central pattern catalog.
- Changing runtime error handling, logging, allocation behavior, source comments, or the cited exemplar implementations.
- Broadly converting every prose rule or file mention into a source link.
- Builds, harness scenarios, or unit tests.

## Acceptance criteria

- `update-claude-docs` defines selection, applicability labeling, stability, count, and maintenance rules for canonical source exemplars without weakening its no-changelog or high-level-documentation contracts.
- `repo-code-review` directs reviewers to the correct network, save, boot-pack, workbuffer-dispatch, and dynamic-log exemplar while retaining the invariant needed to choose among them.
- A fresh-context dry run assigns corrupt network input, invalid save data, and corrupt boot-required animation data to three different documented failure policies, and selects the correct workbuffer pattern for temporary dispatch data versus dynamic log text.
- All cited paths and symbols resolve and still demonstrate their labels; neither changed skill grows in bt-token-v1 size.
- `validate-skill` passes for both changed skills and `git diff --check` passes.

## Notes

Future implementation is Tier 2 scoped review/documentation behavior. It changes review and documentation guidance only, with no determinism/CRC, `kiVersion`/`.pack` layout, replay, wire protocol, client/server guard, allocation-tracked runtime, shader, build, or live-harness behavior change. The cited runtime paths are read-only exemplars. Root `AGENTS.md` remains untouched.
