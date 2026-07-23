<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Canonical Collection Skill Examples

## Context

The `add-collection` and `add-collection-member` skills contain a synthetic full collection header and repeated snippets that restate live collection conventions. That material has drifted: both `PushersInterpolate` and `PushersPostRender` define `kiVersion`, and `Frame::kiVersion` sums those engine-collection terms, while both skills state that engine collections have no per-collection version. The two skills currently total 8,374 bt-token-v1 (`add-collection`: 4,983; `add-collection-member`: 3,391), so replacing synthetic code with verified source exemplars can improve currency and reduce context without dropping layout-critical steps.

## Design

1. Re-inventory the live collection variants and their complete wiring before editing: header tuple shape; `FrameBase.h` or game `Frame.h`/`FrameCollections.h` registration; `Frame::kiVersion`; allocation/copy; spawn/add/remove; update; transfer; client hydration; project membership; and `AgentCommandsServerQueries.cpp` query exposure. Treat current source and the engine/game Collections hub AGENTS.md files as evidence; if any exemplar below no longer demonstrates its assigned variant, stop and report the stale assignment rather than silently choosing another exemplar.
2. Replace the synthetic full-header template and repeated lifecycle snippets with a compact applicability matrix that points to these file-and-symbol exemplars:
   - `TargetsInterpolate` / `TargetsPostRender` for a game collection whose entire SOA layout is shared.
   - `BlastersInterpolate` / `BlastersPostRender` for a game collection with `SharedMembers()` plus client-only members, `ClientInit` hydration, and cross-cell `TransferRequest` construction.
   - `PushersInterpolate` / `PushersPostRender` plus `Frame::kiVersion` for server-visible engine registration and per-collection version terms.
   - `SoundsInterpolate` / `SoundsPostRender` for an owner-synchronized, whole-file client-only collection.
   - `PuffsInterpolate` / `PuffsPostRender` for controller-driven, fire-and-forget client-only storage.
3. Correct version guidance in both skills: every game collection pair and each server-visible engine collection pair follows the live per-struct `kiVersion` pattern and contributes its terms to `Frame::kiVersion`; pure client-only engine collections do not affect the persisted shared layout. Require execution to verify this rule against all current engine `kiVersion` declarations and the `Frame::kiVersion` sum before finalizing wording.
4. Preserve the exhaustive failure-sensitive checklists. `add-collection` retains ownership/location, tuple and frame registration, version sum, construction, phase dispatch, serialization/CRC, copy, transfer, client hydration, project membership, and harness-query decisions. `add-collection-member` retains tuple placement, version bump, copy/persistence, difference logging, initialization, unconditional update stores, transfer send/receive, client hydration, identity semantics, and query-exposure decisions.
5. Remove prose and snippets that merely reproduce an exemplar. Keep invariant explanations where source shape alone cannot communicate why omission causes compilation failure, memory-layout corruption, serialization/save incompatibility, or deterministic CRC drift.
6. Measure both skills after editing and reduce their combined size from 8,374 to at most 6,280 bt-token-v1, without removing any checklist category named above.

## Critical files

- `.agents/skills/add-collection/SKILL.md` — variant selection, full collection wiring, and version guidance.
- `.agents/skills/add-collection-member/SKILL.md` — SOA layout-change checklist and exemplar references.
- `Engine/Source/Frame/FrameBase.h`, `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp`, and the five assigned collection directories — source evidence for registration and variant shape.

## Out of scope

- Changing collection C++, frame registration, `kiVersion` values, serialization, CRC behavior, save/replay compatibility, project membership, or harness commands.
- Replacing failure-sensitive checklists with source links alone.
- Introducing a central canonical-pattern registry or adding collection guidance to root `AGENTS.md`.
- General collection-framework refactors, file reduction outside the two skills, builds, harness runs, or unit tests.

## Acceptance criteria

- Every matrix entry resolves to the assigned live symbols and demonstrates all behavior claimed for that variant.
- No synthetic complete collection definition remains; code fragments remain only where they express a required edit shape not clear from the cited exemplar.
- Both skills consistently describe server-visible engine `kiVersion` terms and pure client-only exclusions, matching all current declarations and `Frame::kiVersion`.
- The combined deterministic measurement is at most 6,280 bt-token-v1 while every checklist category in Design step 4 remains present and actionable.
- Fresh-context dry runs select the correct exemplar and required wiring for shared-only game, shared/client split, server-visible engine, owner-synchronized client-only, and controller-driven client-only scenarios.
- `validate-skill` passes for both skills; cited paths and symbols resolve and `git diff --check` passes.

## Notes

Future implementation is Tier 2 workflow-documentation behavior because incorrect collection instructions expose SOA layout, deterministic CRC, save/replay compatibility, `kiVersion`, client/server membership, and transfer/hydration wiring. The implementation changes no runtime bytes or interfaces. Root `AGENTS.md` remains untouched; no build or harness run is required for the skill-only edit.
