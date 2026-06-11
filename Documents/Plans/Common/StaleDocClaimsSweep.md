# Stale-Doc Claims Sweep (CLAUDE.md / Architecture / Plan Docs)

## Context

The CLAUDE.md refresh found documentation that is stale against current code but lived **outside** the agents'
per-file edit scope (a doc describing code in a *different* directory, or a plan/architecture doc the update
agents don't touch). These need a dedicated doc-correction pass. All items are **documentation-only** (no code
change); grouped because they are the same kind of work and several are one-liners. Each is independent.

### Group A — CLAUDE.md claims describing code in another directory

1. ~~**`Engine/Source/Graphics/CLAUDE.md` line 14** claims the elevation prepass RTT is `R32_SFLOAT`; code
   (`Engine/Data/Shaders/ShaderLayoutsBase.h:35`) defines `keElevationFormat = VK_FORMAT_R16_SFLOAT` (GLSL
   `r16f`). Correct to R16_SFLOAT.~~ **RESOLVED (done during the audit)** — `Graphics/CLAUDE.md:14` now reads
   "the elevation prepass (R16_SFLOAT, `shaders::keElevationFormat`)". No action remaining for this item. (Note:
   the per-island *heightmap* is genuinely `R32_SFLOAT` per `Frame/CLAUDE.md`, and `Terrain/CLAUDE.md` still says
   `R32_SFLOAT` for the composite elevation G-buffer in two spots — a *separate* stale claim if confirmed, but
   the Graphics-doc elevation-RTT format item this entry tracked is fixed.)
2. **`BufferManager.CLAUDE.md:28`** describes `mLightOccupancyVkBuffers[]` as "per-level… derived fresh each
   pass from the source texture"; code creates only index 0 (`BufferManager.cpp:627-629`), binds it to deposit
   pipelines only, and nothing reads or re-derives it. Reword to the actual single-buffer, write-only reality
   (the report's Lighting note also flagged the tile-occupancy bitmask as write-only).
3. **`PipelineManager.CLAUDE.md`** says descriptor staleness is verified "at the top of
   `CommandBufferRecord*::Record`"; code calls `VerifyAllDescriptorGenerations` only in
   `CommandBufferRecordGlobal.cpp:13` — never in `CommandBufferRecordMain`. Narrow the claim to the Global
   recorder.
4. **`CommandBufferManager.CLAUDE.md` "Selective Re-recording"** claims the per-framebuffer recorded flag
   "enables runtime command buffer updates", contradicting the parent's CB re-record ban; in code `kRecorded`
   is an idempotence guard cleared only by destroy/recreate. Reword to "idempotence guard, not runtime
   re-record".
5. **`ParticleManager.CLAUDE.md`** says "Wind force applied from the wind velocity field texture";
   `ParticlesUpdate.comp` samples only the elevation prepass texture — no wind references in
   `ParticleManager.cpp` (wind exists only in feature plans). Remove the wind-force claim. **Also** (from the
   Graphics/Managers `/external-deep-analysis` run): the doc says `Spawn` culls "into per-framebuffer spawn
   buffers" — in code `Spawn` writes a single member staging layout (`ParticleManager.h:23-24`); the
   per-framebuffer copy happens later in `RenderGlobal` (`ParticleManager.cpp:64-70`). Fix the wording in the
   same edit.
6. **`Engine/Source/Profile/CLAUDE.md`** says "the required position of that timer are documented game-side",
   but the verified invariant is enum-name **existence**, not position. Reword (the report's game Profile note
   confirms: engine compiles against the timer enums *by name*, not position).
7. **`Engine/Source/Network/CLAUDE.md`** `SendSimplePacket` "(see children)" pointer is dangling — the hub
   itself fully documents the constraint and neither child adds anything. Drop the parenthetical.
8. **`Projects/.../Source/Network/CLAUDE.md` (hub)** debug-control bullet says the block including the server
   timescale broadcast is "decoded server-side (see Server/CLAUDE.md)", but `kServerTimespeedUpdate` is decoded
   **client-side** (`ClientSession.cpp:81`); only the client→server requests are decoded server-side. Correct
   the direction. (Note: the timespeed flow was also reclassed to `GamePacketType` in a landed Network plan —
   keep the correction consistent with that.)
9. **`DataPacker/Source/CLAUDE.md` ↔ `ExportJobs/CLAUDE.md` circular delegation**: the parent says the full
   bake/split contract "lives in ExportJobs/CLAUDE.md", but the bake code and its documentation live in the
   parent's own TUs/doc, and ExportJobs points back. Make the parent claim its own bake paragraphs (break the
   loop).
10. **Root `CLAUDE.md`** describes `Documents/` as "Mermaid architecture diagrams (`Architecture/`)", but
    `Network.md` there is prose. Soften to "architecture docs". **NOTE:** root `CLAUDE.md` is *out of this
    plan's edit scope* (the task forbids CLAUDE.md edits in this batch) — this item is recorded here so it is
    tracked, but its edit must be made when the root doc is next touched under the normal process, not as part
    of executing this plan. (Carry as a tracked note; do not edit root CLAUDE.md from this plan.)

15. **`Engine/Source/Graphics/CLAUDE.md` "See Also" Debug bullet** says "Wireframe debug visualization (BT_DEBUG
    only)", but the actual gate is the game-layer constexpr `kbDebugRender` (`Projects/.../Source/Pch.h:35/52/69`
    — true only in the Debug config *today*, silently breakable by flipping the constant in Profile/Release).
    The child `Debug/CLAUDE.md` states the gate correctly. Reword to "(`kbDebugRender`-gated)". (From the
    Graphics/Debug `/external-deep-analysis` run.)
16. **`Engine/Source/Graphics/Debug/CLAUDE.md`** says staging is "pre-allocated to a large fixed reserve on
    first use"; code does `layouts.resize(kiInitialDebugRender)` (`DebugRender.cpp:43`) — sized *elements* for
    indexed writes, not `reserve`d capacity. Reword ("resized to a large initial element count" or similar).
17. **`Engine/Source/Graphics/Debug/CLAUDE.md`** says `BeginRender`/`EndRender` "run back-to-back at the end of
    `RenderFrameMain`"; they run after all debug submissions (`MainUniforms.cpp:238-239`) but ~160 lines of
    MainLayout population follow (`:241-397`). Tighten to "after all debug submissions in `RenderFrameMain`"
    (the load-bearing "after all submissions" claim is already correct).

### Group B — Stale plan / architecture documents

11. **`Documents/Architecture/FrameUpdatePipeline.md` line 118** labels the server fixed-timestep loop
    "64 Hz"; code (`Engine/Source/Frame/TimeStep.h`, `kiTickRate = 32`) and sibling docs say 32 Hz. **This fix
    belongs to the `update-architecture-diagrams` skill** (it owns `Architecture/`); execute that skill against
    `FrameUpdatePipeline.md` to correct the rate (and re-verify the rest of the diagram against current phase
    order while open). **Two concrete corrections for that pass** (verified against current code): (a) the
    `physics_loop` subgraph label `Fixed Timestep Loop (64 Hz)` → `(32 Hz)` (`kiTickRate = 32`); (b) the
    **Server Main Loop diagram (`:113-130`) omits steps that `GameBase::ServerUpdate` runs** — `Engine/Source/
    CLAUDE.md` designates this diagram as the record of main-loop ordering and lists "save/load/replay …
    resends … autosave; quickload may early-return the whole update", none of which appear in the diagram (it
    shows only `PreTickNetwork` → `WaitForTick` → the physics loop). Add the missing quickload / save-load-replay
    / resends / autosave steps (and the quickload early-return edge) so the diagram matches `ServerUpdate`.
    **Two further corrections for the same pass** (from the Network `/external-deep-analysis` run): (c) the
    client diagram shows a nonexistent `ClientSession::PostRender()` node inside `GameBase::Render`
    (`FrameUpdatePipeline.md:86-88`) — no such method exists on `ClientSession`/`ClientSessionBase`
    (grep-verified); remove or replace with the real call. (d) the client loop omits the real network phases
    `UpdateSubscriptions` (unsubscribe-stale → rebuild → subscribe, `ClientSessionSubscriptions.cpp:175-177`)
    and the sim-tick ceiling clamp (`GetTargetSimTick`) — add them where they run (between the stall gate and
    tick advance).
12. **`Documents/Plans/Graphics/ShaderReview/00_Overview.md`**: item-7 flags a `Log.vert` `gl_Position.w = 0.0`
    bug **already fixed** in code (`w = 1.0f`, landed — Order.md's Shader Debt Score confirms `Log.vert:38`
    landed). Remove/annotate the fixed item. (The Overview already states its per-subsystem plans are
    executed-and-deleted; just reconcile the cross-cutting list with what landed.)
13. **`Documents/Plans/Order.md`**: the "Per-frame multi-island system… all 5 phases LANDED" Dependencies entry
    is historical record kept *inconsistent* with the executed-work-is-deleted convention — but per
    `Plans/CLAUDE.md` landed-entry annotations **are** the historical record ("annotate the affected entry as
    landed-and-removed rather than deleting it"). So this is likely *correct as-is*; the action is to **confirm**
    the entry matches the annotation convention (it appears to) and leave it, OR tidy its wording if it predates
    the convention. Low-touch.
14. **`Documents/Plans/Graphics/ocean-fragment-shader-overview.md`** references a **per-user path outside the
    repo** (`C:\Users\dougt\.claude\plans\...`). Replace the absolute per-user path with a repo-relative
    reference or remove the dangling pointer. (Note: this file is not in the current `Glob` of
    `Documents/Plans/**/*.md` — **verify it still exists** at execution; if it was already deleted, this item is
    moot.)

## Design

For each Group A item, edit the named CLAUDE.md to match current code — **except item 10 (root CLAUDE.md)**,
which is recorded-but-not-edited here (root CLAUDE.md edits are out of this batch's scope; fold it in when the
root doc is next touched). Verify each claim against the cited code line before editing (line numbers drift).

For Group B:
- **Item 11** → run `/update-architecture-diagrams` on `FrameUpdatePipeline.md` (the skill owns the
  `Architecture/` tree); do not hand-edit the Mermaid if the skill is the canonical path.
- **Items 12-14** are edits *under `Documents/Plans/`* — directly editable. Reconcile the ShaderReview overview
  with landed fixes (12), confirm/tidy the Order.md LANDED entry against the annotation convention (13), and
  fix/remove the per-user path (14, after confirming the file exists).

Keep every edit narrow; do not rewrite surrounding doc prose beyond the stale claim.

## Out of scope

- **Any code change** — every item is documentation. (Where a doc was stale because the *code* changed, the
  code is already correct; only the doc lags.)
- **Root `CLAUDE.md` edits** — item 10 is tracked here but must be applied under the normal process when the
  root doc is next touched, not from this plan (this batch forbids CLAUDE.md changes).
- **The stale *source-code comments*** (FileManager "pre-faulted", terrain indirect-draw, etc.) — those are the
  separate `StaleCodeCommentsSweep.md`; this plan is *doc files* only. (The two overlap thematically but touch
  different files.)
- Restructuring the `Architecture/` diagrams beyond the 64→32 Hz correction (and a same-pass sanity check) —
  not a redesign.
- Re-running the shader review or re-scoring Order.md — only the stale FrameUpdatePipeline rate, the fixed-item
  reconciliation, and the per-user path are touched.

## Acceptance criteria

- Each Group A CLAUDE.md claim (1-9, 15-17) matches current code; the root-CLAUDE.md item (10) is tracked for
  the next root-doc touch (not edited here). Item 1 is already **RESOLVED** (Graphics/CLAUDE.md says R16_SFLOAT)
  — no edit.
- `FrameUpdatePipeline.md` says 32 Hz (via the architecture-diagrams skill), consistent with `kiTickRate = 32`
  and the sibling docs, **and** its Server Main Loop diagram includes the quickload / save-load-replay / resends
  / autosave steps `GameBase::ServerUpdate` runs (11).
- `ShaderReview/00_Overview.md` no longer flags the already-landed `Log.vert` `w` fix as open (12).
- The Order.md "5 phases LANDED" entry is confirmed consistent with the landed-annotation convention (or tidied)
  (13); the per-user absolute path in `ocean-fragment-shader-overview.md` is removed/repo-relative (or the item
  is closed because the file no longer exists) (14).
- No code or behavior change anywhere.

## Critical files

- `Engine/Source/Graphics/CLAUDE.md` (`:14`, "See Also" Debug bullet),
  `Engine/Source/Graphics/Managers/BufferManager.CLAUDE.md`
  (`:28`), `PipelineManager.CLAUDE.md`, `CommandBufferManager.CLAUDE.md`, `ParticleManager.CLAUDE.md`,
  `Engine/Source/Profile/CLAUDE.md`, `Engine/Source/Network/CLAUDE.md`,
  `Projects/BrokenEngineSandbox/Source/Network/CLAUDE.md`, `DataPacker/Source/CLAUDE.md`,
  `DataPacker/Source/ExportJobs/CLAUDE.md`, `Engine/Source/Graphics/Debug/CLAUDE.md` (items 16-17) — Group A
  targets.
- `Documents/Architecture/FrameUpdatePipeline.md` (`:118`) — via the architecture-diagrams skill (Group B 11).
- `Documents/Plans/Graphics/ShaderReview/00_Overview.md`, `Documents/Plans/Order.md`,
  `Documents/Plans/Graphics/ocean-fragment-shader-overview.md` (verify existence) — Group B 12-14.
- Read-only code references for verification: `ShaderLayoutsBase.h:35`, `BufferManager.cpp:627-629`,
  `CommandBufferRecordGlobal.cpp:13`, `ClientSession.cpp:81`, `Engine/Source/Frame/TimeStep.h` (`kiTickRate`).

## Notes

- Documentation-only — Risks = 0, no build/runtime/CRC impact.
- Mixed ownership: Group A + Group B 12-14 are direct doc edits; Group B 11 routes through
  `/update-architecture-diagrams`; item 10 (root CLAUDE.md) is tracked-not-edited here.
- Item 13 may be a no-op (the LANDED entry likely already follows the annotation convention) — confirm before
  touching.
