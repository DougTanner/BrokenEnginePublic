# Architecture: Render Ordering-Contract Doc Gaps

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Render` (non-recursive). The Ordering Contract in
`Engine/Source/Graphics/Render/CLAUDE.md:11-14` was verified against code: the documented orderings hold, but the
contract omits the strongest global→main dependency and overstates a CPU-side ordering that is actually GPU-side.
Doc-only plan; zero code change.

## Design

### Engine/Source/Graphics/Render/CLAUDE.md — Ordering Contract section
- Add the missing global→main dependency: `RenderWindGlobal` flips the ping-pong index `giWindTextureIndex` each
  frame (`WindUniforms.cpp:66`), and the **main** pass consumes it to route wind deposits
  (`Engine/Source/Frame/Collections/WindTrails/WindTrailsRender.cpp:159-160`,
  `WindRadials/WindRadialsRender.cpp:84-85`). Unlike the documented `fElapsedTime` dependency, running main before
  global would silently deposit into the wrong ping-pong texture — this is the most consequential
  global-before-main ordering and the contract doesn't mention it. [~5m]
- Clarify `:14` ("Wind reads (does not write) the smoke world-area / previous-area uniforms, so smoke must populate
  them first"): the read is GPU-side only — the wind *shaders* read `f4SmokeArea` / `f4PreviousSmokeArea`
  (`Engine/Data/Shaders/Wind/WindSpreadOne.comp:56-58`, `WindSpreadTwo.comp:57-59`,
  `WindOccupancyDilate.comp:44-46`); `RenderWindGlobal` performs no CPU-side read (`WindUniforms.cpp:70-74` is
  comment-only). Both populates land in the same mapped buffer before the Global submit, so the CPU-side
  smoke-before-wind order is currently convention/documentation-only. State the dependency as GPU-side. [~5m]

## Critical files
- `Engine/Source/Graphics/Render/CLAUDE.md`

## Out of scope
- Adding runtime enforcement (asserts/frame-counter checks) for the ordering contract — see Notes.
- The `:22` "only resolved floats reach the shader" nuance — resolved by deleting the dead `f4SunMoonNormal.w`
  write in `Graphics/Architecture_RenderRegionOwnership.md`; no doc edit needed once that lands.
- All other Render/CLAUDE.md claims — verified accurate against source this run (entry points, ownership exception
  co-gating, fmod-reduction invariants, latch/reset-flag sites, profile readouts, scale-aware spread).

## Acceptance criteria
- The Ordering Contract lists the `giWindTextureIndex` global→main dependency and labels the smoke→wind dependency
  as GPU-side.

## Notes
- Doc-only: no determinism/CRC/network/`kiVersion`/behavior exposure.
- Considered and rejected for this plan: a debug assert enforcing global-before-main (e.g. frame-counter check).
  The orderings are structurally fixed by single call sites in `GameBase.cpp:411/:418` and `Graphics.cpp:222/:232`;
  an assert would guard against a reorder no current code path can produce (YAGNI). Revisit only if the render loop
  gains multiple entry points.

## Verification Notes

Verified 2026-06-11 against current source; both items confirmed, no drops.
- `giWindTextureIndex` flip confirmed at `WindUniforms.cpp:66`; main-pass consumers confirmed at
  `WindTrailsRender.cpp:159-160` and `WindRadialsRender.cpp:84-85` (indirect-count routing by index parity —
  main-before-global would deposit into the texture the wind sim is about to overwrite). It is the only
  global-pass output the main pass consumes besides `fElapsedTime`.
- GPU-side-only claim confirmed: the cited `.comp` line ranges cover both the `f4SmokeArea` reads
  (`WindSpreadOne.comp:56-57`, `WindSpreadTwo.comp:57-58`, `WindOccupancyDilate.comp:44-45`) and the
  `f4PreviousSmokeArea` reads (`:58`/`:59`/`:46` respectively — citation wording extended to name both uniforms);
  `RenderWindGlobal` contains no CPU read of either field (`WindUniforms.cpp:70-74` comment-only); smoke then wind
  populate the same mapped buffer inside `RenderFrameGlobal` (`GlobalUniforms.cpp:610-611`) before submit.
- Rejected-assert rationale call sites confirmed: `GameBase.cpp:411` (`RenderGlobal`) / `:418`
  (`RenderMainPresentAcquire`) and `Graphics.cpp:222` (`RenderFrameGlobal`) / `:232` (`RenderFrameMain`).
- Doc-only; no score adjustment suggested.
