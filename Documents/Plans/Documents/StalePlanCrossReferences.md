# Live Plans Cite Plan Files That Do Not Exist

## Context

Plan bodies cross-reference each other by path to route out-of-scope work, but a completed plan's file is removed at completion and its counterparts' citations are not always updated. The drift is repo-wide, not isolated: a sweep of every `.md` under `Documents/Plans/` and `Documents/Features/` on 2026-07-21 found **18 dangling citations of the `<Area>/<Name>.md` shape, across 11 plan files, naming 13 distinct plan paths absent from the repository** (this plan's own descriptive citations excluded). Two of those 18, both in the since-completed game-extensible `TweakSection` registry plan, were resolved when that plan's file was removed at completion; the snapshot below has been trimmed accordingly. A second pass for bare `<Name>.md` citations — the shape `Documents/Plans/Network/WireFormatPairingGameSide.md:51` uses — adds roughly 30 more instances, dominated by historical references to removed `Graphics/` island-residency plans and to `ShaderReview/` sub-documents that were never created. **The sweep, not any list in this plan, is authoritative** — re-run it at implementation time and work from its output, because the set moves every time a plan lands or completes.

The 2026-07-21 area-prefixed snapshot, grouped by the determination classes in Design below:

- `Documents/Plans/Frame/PlayerTransferUuidPreservation.md:131` and `:146` — `Network/DeadMachinerySweep.md`. Line 146 is an `## Out of scope` routing target, so a reader following it to find the owning plan finds nothing; line 131 is a "consider removing it, or route it through …" pointer inside `## Superseded claims`.
- `Documents/Plans/Network/ServerSpawnRateBounding.md:29` — the same target plus `Agent/AgentHarness3_FrameQueriesAndInjection.md`, both as "has landed" notes. Historically accurate, but they name paths that no longer resolve.
- `Documents/Plans/Frame/CollectionReadIndexHardening.md:31` and `Documents/Plans/Network/WireFormatPairingGameSide.md:51` — `Network/StatusChangeCodecHardening.md`, cited in both as the plan that owns the StatusChange batch codec. That work is currently unowned: the routing target does not exist, so the scope those two plans pushed away is silently dropped. `WireFormatPairingGameSide.md:51` writes it as the bare filename `StatusChangeCodecHardening.md`, so an area-prefixed-only sweep misses it.
- The `Graphics/` cluster is the largest — `Architecture_PipelineRegistrationOwnership.md:79-81`, `DisabledPassGatingPerfAudit.md:79,:80,:85`, `WindowedLightingShadowDispatch.md:77`, `TerrainMeshLodChain.md:64`, `ModelSpecularAntialiasing.md:120` — routing to six absent `Graphics/` plans (`Architecture_DescriptorSetIndexDedup.md`, `Architecture_ObjectsShaderCpuConsistency.md`, `InlineAreaMappingDryToHelper.md`, `Refactor_WriteDescriptorDecomposition.md`, `WaterDisplacementIndirectCompute.md`, `WindHistoryResetOnRecreate.md`) plus `File/MeshRecommitFailureObservability.md` and `Meta/ReviewSweepQuickWins.md`.
- `Documents/Plans/Engine/HeaderCompileFirewallSweep.md:62` — `Engine/PchProvidedHeaderReincludeSweep.md`.

The concrete acceptance gap is the `Network/StatusChangeCodecHardening.md` case, and every other citation that routes work away rather than merely narrating history: a live plan declares work out of scope by delegating it to a plan that was never created or has since been removed, so a `/next-plan` execution of that plan leaves the scope orphaned with no queue row.

`.agents/scripts/Find-PlanClosureReferences.ps1` already reports references to a plan being completed, so the mechanism to prevent recurrence exists; these citations predate or bypassed its use.

## Design

- Sweep every `.md` under `Documents/Plans/` and `Documents/Features/` for citations of plan paths, and resolve each against the files actually on disk. Match all three citation shapes in use — `Documents/Plans/<Area>/<Name>.md`, `<Area>/<Name>.md`, and the bare `<Name>.md` — and exclude `AGENTS.md`/`SKILL.md`/`CLAUDE.md` and the machine-local queue identities (`Order.md`, `Plans-Order.md`, `Features-Order.md`), which are not plan files. The sweep's output is the work list; do not work from the snapshot in Context.
- For each dangling citation, choose per instance and record the choice:
  - the referenced work is done — drop the citation, keeping the surrounding sentence coherent;
  - the referenced work is still real and unowned — say so in place of the dead path, and route it through `/create-follow-up-plans` so it gets a queue row rather than a prose promise;
  - the reference is historical narrative — rewrite it to state the fact without a path that implies a live plan.
- `Network/StatusChangeCodecHardening.md` in particular needs a determination of whether StatusChange batch-codec hardening is still wanted; if it is, it needs a real plan, not a citation.
- Change only the citing sentences. Do not restructure plans, re-score rows, or alter any plan's scope beyond the dangling reference.

## Critical files

Every `.md` under `Documents/Plans/` and `Documents/Features/` is in scope; the sweep selects which ones get edited. The 2026-07-21 area-prefixed snapshot named these eleven citing files — re-derive the line numbers rather than trusting them, and expect the bare-name pass to add more files (notably `Documents/Plans/Graphics/ShaderReview/00_Overview.md` and the island-residency cluster):

- `Documents/Plans/Frame/PlayerTransferUuidPreservation.md` (`:131`, `:146`)
- `Documents/Plans/Frame/CollectionReadIndexHardening.md` (`:31`)
- `Documents/Plans/Network/ServerSpawnRateBounding.md` (`:29`)
- `Documents/Plans/Network/WireFormatPairingGameSide.md` (`:51`)
- `Documents/Plans/Engine/HeaderCompileFirewallSweep.md` (`:62`)
- `Documents/Plans/Graphics/Architecture_PipelineRegistrationOwnership.md` (`:79`, `:80`, `:81`)
- `Documents/Plans/Graphics/DisabledPassGatingPerfAudit.md` (`:79`, `:80`, `:85`)
- `Documents/Plans/Graphics/WindowedLightingShadowDispatch.md` (`:77`)
- `Documents/Plans/Graphics/TerrainMeshLodChain.md` (`:64`)
- `Documents/Plans/Graphics/ModelSpecularAntialiasing.md` (`:120`)
- `.agents/scripts/Find-PlanClosureReferences.ps1` — existing reference-detection mechanism; reuse rather than reimplement.

## Out of scope

- Queue rows, scores, dependency edges, and any WorktreeCli mutation — this is plan-body prose only, which needs no queue request.
- Creating the follow-up plans that a "still real and unowned" determination produces; that routes through `/create-follow-up-plans` as separate work.
- Non-plan documents, AGENTS.md files, skills, and code comments.
- Broken relative links that resolve to existing files — only citations of absent plan files are in scope.

## Acceptance criteria

- Every plan-path citation in `Documents/Plans/` and `Documents/Features/` resolves to a file that exists, verified by re-running the sweep after the edits. This plan's own Context and Critical-files snapshot is exempt — it deliberately names the absent paths and is removed at completion.
- Each removed or rewritten citation has a recorded determination (done / still real / historical).
- Any "still real and unowned" scope is named in the session report so it can be queued, rather than left as prose.

## Notes

- **Sizing.** This plan body states no score; the live queue row's Effort predates the sweep and was set against a four-citation reading. The real shape is ~18 area-prefixed instances in 11 files against 13 absent targets, plus ~30 bare-name instances — still mechanical prose edits, but each distinct absent target needs its own done / still-real / historical determination. Re-check the row's Effort against a fresh sweep before claiming, and route any change through a `plan order update`; the row itself stays out of scope per above.
- Documentation-only Tier 1 work: no build, no code, no runtime verification. `git diff --check` passes.
- No determinism/CRC, wire, `.pack`, or client/server exposure.
- Plan-body prose is an ordinary tracked edit per `Documents/Plans/AGENTS.md`; no `plan order update` request is needed unless a row field changes, which this plan does not do.
