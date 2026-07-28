<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-26T21:35:15.877Z","dependsOn":[]} -->
# Plan-closure reference sweep must cover the Investigations tree

## Context

`Documents/Investigations/` is a third tracked tree that cites Plan paths (`Documents/AGENTS.md` "Planning Trees"; `Documents/Investigations/AGENTS.md`). Four documents were relocated into it, and the tree is deliberately outside the scheduler.

The mechanism that reports references to a Plan being completed does not see it. `.agents/scripts/Find-PlanClosureReferences.ps1:38` walks exactly `@('Documents/Plans', 'Documents/Features')`; everything else in the script — the full-path, area-relative, and bare-name term derivation (`:22-35`) and the `broken-engine-plan-closure-references/v1` emission (`:56`) — is tree-agnostic. The stale Plan/Feature cross-reference sweep completed this session and used this script as its repo-wide dangling-citation mechanism, so the blind spot propagated into that sweep's coverage as well.

Investigations documents cite Plans exactly the way Plan bodies do. `Documents/Investigations/Graphics/IslandResidentMemoryScaling_Overview.md:7` states that every bare plan filename in it resolves against `Documents/Plans/Graphics/`, and it carries roughly eight such citations across the architecture findings (`:44-:47`) and the series table (`:51-:58`).

The drift is already realized, not hypothetical: the series table `:54` lists `IslandMeshArenaResidency.md` with status **Open**, and finding 1 at `:44` routes the GPU-mesh residency decision to it. No such file exists at the current tip — commit `78a367c8` removed it — while every other absent entry in that table is explicitly marked *removed — landed*. The next occurrence is already queued: `:55` cites `IslandHeightmapRouteDedup.md`, which is live, so its completion produces the same silent dangling citation.

Originating gap: the change that created the Investigations tree was scoped to the marker-less-plan-document rule, its two reporting sites, the guidance exemption, the document dispositions, and the planning documentation. Sweep coverage of the new tree was outside that boundary and is not an acceptance failure of it.

## Design

1. Add `'Documents/Investigations'` to the root list at `Find-PlanClosureReferences.ps1:38`. Nothing else in the script changes. In particular the `-CompletedPlan` guard at `:19-21` keeps rejecting anything outside `Documents/Plans/` and `Documents/Features/` — an Investigations document is never a completed plan, only a citing one — and the schema stays at `broken-engine-plan-closure-references/v1` because each hit's `path` already identifies its tree, so consumers separate trees without a schema change.
2. Run the widened sweep and resolve the dangling citations it reports under `Documents/Investigations`, editing only the citing sentences. Use the same three determinations established by the completed stale Plan/Feature cross-reference sweep, recording the choice per instance: the referenced work is done (drop the citation, keeping the sentence coherent), it is still real and unowned (say so in place of the dead path and name it in the session report so `/create-follow-up-plans` can give it a Plan), or the reference is historical narrative (state the fact without a path implying a live plan).
3. The `IslandMeshArenaResidency.md` case is decided from the tree, not assumed: establish from `78a367c8` whether the stable-handle mesh-arena work landed. If it did, the series row and finding 1 become historical in the same shape the table already uses for removed–landed entries. If it did not, classify it "still real and unowned" and report it — never silently drop it.
4. The stale Plan/Feature cross-reference sweep completed this session, so its one descriptive-sentence update requires no further action here. That Plan's metadata marker, scope contract, and every other sentence stay byte-for-byte unchanged.

## Critical files

- `.agents/scripts/Find-PlanClosureReferences.ps1` — the root list at `:38` only.
- `Documents/Investigations/Graphics/IslandResidentMemoryScaling_Overview.md` — only the citing sentences the sweep flags (`:44` and `:54` today; re-derive, line numbers move).
- The completed stale Plan/Feature cross-reference sweep's former one-sentence three-tree wording update — historical; no active file remains.
- `Documents/Investigations/AGENTS.md`, `Documents/AGENTS.md` — read-only tree definitions.

## Out of scope

- Detection or citation cleanup inside `Documents/Plans/` and `Documents/Features/` — a separate owner (see Coordination).
- The `-CompletedPlan` argument contract, term shapes, matching algorithm, output schema, new parameters, and any `AGENTS.md`/`CLAUDE.md`/`SKILL.md` filtering. Widening the roots is the whole script change.
- Wiring the script into a skill, hook, or workflow. It stays operator-invoked.
- WorktreeCli, plan metadata, `dependsOn` edges, claims, and any scheduler behaviour. `Documents/Investigations` is never a scheduler input and gains no metadata.
- Merging indexed plan content into the overview, adding executable metadata to it, or deriving new work items from it — its own role contract at `:7` forbids all three.
- Broken links that resolve to files that exist, and non-plan-path references.

## Risk tier

Tier 2. Trigger: changes one agent-tool script's reporting behaviour (what the closure sweep reports), plus documentation prose. No determinism/CRC, wire, serialization/`.pack`, save/replay, threading, allocation, shader, client/server, or build/bootstrap-coordination exposure; no C++ or GLSL compiles.

Invariants that must hold:

- The emitted JSON keeps the `broken-engine-plan-closure-references/v1` schema and its `baseline`, `completedPlan`, `terms`, and `hits` fields; only the `hits` set grows.
- The `-CompletedPlan` canonical-path guard still rejects any path outside the two planning trees.
- No file under `Documents/Investigations/` gains byte-zero metadata, and the scheduler's view of the repository is unchanged.

## Acceptance criteria

- Running `.agents/scripts/Find-PlanClosureReferences.ps1 -Worktree <checkout> -Baseline <HEAD sha> -CompletedPlan Documents/Plans/Graphics/IslandHeightmapRouteDedup.md` returns at least one hit whose `path` is `Documents/Investigations/Graphics/IslandResidentMemoryScaling_Overview.md`; the same invocation before the change returns none from that tree. Both runs parse as `broken-engine-plan-closure-references/v1` with identical field names.
- Every plan-path citation under `Documents/Investigations/` either resolves to a file present in the tree or reads unambiguously as removed/historical, verified by re-running the sweep per absent target after the edits.
- Each edited citation has a recorded determination (done / still real and unowned / historical), and any "still real and unowned" scope is named in the session report rather than left as prose.
- `WorktreeCli.exe plan validate` returns `status: valid`, `code: ok`, with the same plans count as before the change.
- `git diff --check` passes.

## Coordination

The stale Plan/Feature cross-reference sweep completed this session; its prior coordination record follows. Boundaries do not overlap: that Plan is prose-only inside `Documents/Plans/` and `Documents/Features/` and explicitly excludes this script and all non-plan documents (`:34`), while this Plan owns the script's scan roots and the Investigations tree. No ordering requirement and no metadata edge in either direction. If that Plan lands second, its sweep output will include Investigations hits its own scope excludes — those belong here, not there. If it has already completed when this Plan runs, only Design step 4 drops out and the rest is unchanged.

## Verification

1. Run the sweep against the same `-CompletedPlan` argument before and after the root change; diff the `hits` sets and confirm the Investigations entries appear only after.
2. Re-run the sweep per absent target once the citations are edited; expect no unresolved plan-path citation under `Documents/Investigations/`.
3. Run `WorktreeCli.exe plan validate`. No build is required — no compiled artifact changes.
