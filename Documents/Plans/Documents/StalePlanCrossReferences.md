<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Live Plans Cite Plan Files That Do Not Exist

## Context

Plan bodies cross-reference each other by path to route out-of-scope work, but a completed plan's file is removed at completion and its counterparts' citations are not always updated. The drift is repo-wide, not isolated: a sweep of every `.md` under `Documents/Plans/` and `Documents/Features/` on 2026-07-21 found 18 dangling citations of the `<Area>/<Name>.md` shape, across 11 plan files, naming 13 distinct plan paths absent from the repository (this plan's own descriptive citations excluded). A second pass for bare `<Name>.md` citations — the shape `Documents/Plans/Network/WireFormatPairingGameSide.md` uses — added roughly 30 more instances, dominated by historical references to removed `Graphics/` island-residency plans and to `ShaderReview/` sub-documents that were never created.

**The sweep, not any list in this plan, is authoritative** — re-run it at implementation time and work from its output, because the set moves every time a plan lands or completes. The drift has already been demonstrated twice: two of the original 18 instances lived in a since-completed `TweakSection` registry plan and vanished with it, and a 2026-07-24 re-check found four more snapshot citing files gone the same way (`Engine/HeaderCompileFirewallSweep.md`, `Graphics/Architecture_PipelineRegistrationOwnership.md`, `Graphics/DisabledPassGatingPerfAudit.md`, `Graphics/WindowedLightingShadowDispatch.md`).

Citations re-verified live on 2026-07-24, grouped by the determination classes in Design:

- `Documents/Plans/Frame/PlayerTransferUuidPreservation.md:132` and `:147` — `Network/DeadMachinerySweep.md`. Line 147 is an `## Out of scope` routing target, so a reader following it to find the owning plan finds nothing; line 132 is a "consider removing it, or route it through …" pointer inside `## Superseded claims`.
- `Documents/Plans/Network/ServerSpawnRateBounding.md:30` — the same target plus `Agent/AgentHarness3_FrameQueriesAndInjection.md`, both as "has landed" notes. Historically accurate, but they name paths that no longer resolve.
- `Documents/Plans/Frame/CollectionReadIndexHardening.md:32` and `Documents/Plans/Network/WireFormatPairingGameSide.md:52` — `Network/StatusChangeCodecHardening.md`, cited in both as the plan that owns the StatusChange batch codec. That work is currently unowned: the routing target does not exist, so the scope those two plans pushed away is silently dropped. `WireFormatPairingGameSide.md:52` writes it as the bare filename `StatusChangeCodecHardening.md`, so an area-prefixed-only sweep misses it.
- Remaining `Graphics/` snapshot files — `TerrainMeshLodChain.md:64`, `ModelSpecularAntialiasing.md:120` — route to absent `Graphics/` plans; the bare-name pass adds `Graphics/ShaderReview/00_Overview.md` and the island-residency historical cluster. Re-derive the full set from the sweep.

The concrete acceptance gap is the `Network/StatusChangeCodecHardening.md` case, and every other citation that routes work away rather than merely narrating history: a live plan declares work out of scope by delegating it to a plan that was never created or has since been removed, so a `/next-plan` execution leaves the scope orphaned.

`.agents/scripts/Find-PlanClosureReferences.ps1` already reports references to a plan being completed (given `-Worktree`, `-Baseline`, and the `-CompletedPlan` path, it scans both planning trees for full-path, area-relative, and bare-name terms), so the mechanism to prevent recurrence exists; these citations predate or bypassed its use.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change and add no abstractions, configuration, refactors, or fixes to adjacent prose or code encountered along the way.

**In scope** — only these regions:

- In each `.md` file under `Documents/Plans/` and `Documents/Features/` that the implementation-time sweep flags: the specific sentences containing a dangling plan-path citation, edited per the Design determinations below. A file appearing in the sweep output grants permission to touch only those citing sentences — nothing else in the file, and no structural, metadata, or scope changes to any plan.
- The session report naming each determination and any "still real and unowned" scope.

**Out of scope:**

- Executable metadata, dependency edges, and any WorktreeCli claim mutation — this is Plan-body prose only and needs no scheduler mutation.
- Creating the follow-up plans that a "still real and unowned" determination produces; that routes through `/create-follow-up-plans` as separate work.
- Non-plan documents, AGENTS.md files, skills, scripts (including `Find-PlanClosureReferences.ps1` itself), and code comments.
- Broken relative links that resolve to existing files — only citations of absent plan files are in scope.
- This plan's own Context — it deliberately names the absent paths and is removed at completion.

## Design

- Sweep every `.md` under `Documents/Plans/` and `Documents/Features/` for citations of plan paths, and resolve each against the files actually on disk. Match all three citation shapes in use — `Documents/Plans/<Area>/<Name>.md`, `<Area>/<Name>.md`, and the bare `<Name>.md` — and exclude `AGENTS.md`/`SKILL.md`/`CLAUDE.md`. The sweep's output is the work list; do not work from the snapshot in Context. `Find-PlanClosureReferences.ps1` demonstrates the term shapes and tree walk to reuse; it takes one completed plan at a time, so drive it per absent target or adapt its matching inline rather than reimplementing the shapes from scratch.
- For each dangling citation, choose one of exactly three determinations per instance and record the choice:
  - the referenced work is done — drop the citation, keeping the surrounding sentence coherent;
  - the referenced work is still real and unowned — say so in place of the dead path, and name it in the session report so `/create-follow-up-plans` can give it an executable plan rather than a prose promise;
  - the reference is historical narrative — rewrite it to state the fact without a path that implies a live plan.
- `Network/StatusChangeCodecHardening.md` in particular needs a determination of whether StatusChange batch-codec hardening is still wanted; if it is, it needs a real plan, not a citation. Ground the determination in whether the StatusChange batch codec in `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` still carries the hardening gap the citing plans describe; if that cannot be established from the code and the citing sentences, classify it "still real and unowned" — never silently drop it.
- Change only the citing sentences. Do not restructure plans or alter any plan's scope or executable metadata beyond the dangling reference.

## Critical files

The sweep selects which files get edited. The verified-live citing files as of 2026-07-24 (re-derive line numbers; expect the sweep to add and remove files):

- `Documents/Plans/Frame/PlayerTransferUuidPreservation.md` (`:132`, `:147`)
- `Documents/Plans/Frame/CollectionReadIndexHardening.md` (`:32`)
- `Documents/Plans/Network/ServerSpawnRateBounding.md` (`:30`)
- `Documents/Plans/Network/WireFormatPairingGameSide.md` (`:52`)
- `Documents/Plans/Graphics/TerrainMeshLodChain.md` (`:64`)
- `Documents/Plans/Graphics/ModelSpecularAntialiasing.md` (`:120`)
- `Documents/Plans/Graphics/ShaderReview/00_Overview.md` (bare-name pass)
- `.agents/scripts/Find-PlanClosureReferences.ps1` — existing reference-detection mechanism; reuse, read-only.

## Risk tier

Tier 1 — mechanical documentation-only prose edits. No build, no code, no runtime verification; no determinism/CRC, wire, `.pack`, or client/server exposure. Plan-body prose is an ordinary tracked edit per `Documents/Plans/AGENTS.md`; executable metadata lines are preserved byte-for-byte.

## Acceptance criteria

- Every plan-path citation in `Documents/Plans/` and `Documents/Features/` resolves to a file that exists, verified by re-running the sweep after the edits. This plan's own Context and Critical-files snapshot is exempt — it deliberately names the absent paths and is removed at completion.
- Each removed or rewritten citation has a recorded determination (done / still real / historical).
- Any "still real and unowned" scope is named in the session report so `/create-follow-up-plans` can create tracked executable metadata, rather than leaving it as prose.
- `git diff --check` passes.

## Notes

- **Sizing.** The 2026-07-21 shape was ~18 area-prefixed instances in 11 files against 13 absent targets, plus ~30 bare-name instances; four citing files have since completed, so expect fewer. Still mechanical prose edits, but each distinct absent target needs its own done / still-real / historical determination.
