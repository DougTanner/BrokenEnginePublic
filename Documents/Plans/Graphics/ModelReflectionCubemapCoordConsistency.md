# `Model.frag` Reflection Cubemap-Coord Sign Flip — Consolidate or Document

## Context

In `Engine/Data/Shaders/Model/Model.frag`, the reflection vector fed into IBL is built and then immediately transformed by two different conventions in two different locations:

- `Model.frag:252` — `vec3 reflection = -reflect(v, n);` builds the reflection direction in the engine's Z-up world space.
- `Model.frag:253` — `reflection.y *= -1.0;` inlines a cubemap-axis sign flip *outside* the helper.
- `Model.frag:254` (the next statement) — `GetIBLContribution(..., reflection, ...)` consumes it.
- `Model.frag:209` — inside `GetIBLContribution`, the reflection vector is passed through `ToCubemapCoord(reflection)` (a helper that swizzles from the engine's Z-up world space into the cubemap's Y-up axis convention).

So one half of the engine-to-cubemap-axis transform (the swizzle) lives inside the helper, and the other half (the `y *= -1`) is sprinkled at the call site one line above. That split is the readability/maintainability smell: a contributor editing `ToCubemapCoord` to handle a future cubemap-axis change will not see the `y *= -1` two lines above its only caller's `GetIBLContribution` invocation, and a contributor editing line 253 will not realize the helper is doing related work.

There are two plausible resolutions, and **this plan deliberately does not pick one** — the implementer must investigate at execution time:

- **(a) Consolidation.** The `reflection.y *= -1.0` is part of the same Z-up → Y-up cubemap-axis convention that `ToCubemapCoord` already handles. Fold it inside the helper and delete `Model.frag:253`. Verify that `ToCubemapCoord(n)` at `Model.frag:208` (which feeds `samplerIrradiance`, the diffuse irradiance cubemap) also wants the same Y-flip — if yes, the consolidation is clean; if it would break the diffuse path, the flip is reflection-specific and option (b) applies.
- **(b) Document in place.** The Y-flip is a *separate* convention — e.g., parity with how the prefiltered radiance cubemap was baked, or a handedness convention specific to the reflection vector that does *not* apply to the diffuse irradiance sample one line above. In this case, leave the code as-is and add a comment at `Model.frag:253` explaining *why* the reflection vector needs an extra Y-flip that the diffuse normal at `Model.frag:208` does not.

The grill / implementer determines which option applies by:

1. Comparing `ToCubemapCoord(n)` at `Model.frag:208` (diffuse, fed by `samplerIrradiance`) with `ToCubemapCoord(reflection)` at `Model.frag:209` (specular, fed by `prefilteredMap`). If both cubemaps were baked with the same axis convention, the flip needs to apply to both or neither; the asymmetry between lines 208 and 209-with-prefix-flip is the smoking gun.
2. Checking the DataPacker code path that bakes `samplerIrradiance` and `prefilteredMap` (likely under `DataPacker/Source/`, IBL precomputation). If one was baked with a Y-flip and the other was not, option (b) applies.
3. Visually A/B testing a reflective metallic surface after folding the flip into `ToCubemapCoord` — if reflections appear mirrored top-to-bottom, the flip is reflection-specific and option (b) applies; if they look identical to before, option (a) is correct.

## Design

At execution time, in order:

1. **Read** `ToCubemapCoord` (in whichever `*Common.h` or shared helper file under `Engine/Data/Shaders/` defines it — likely `Engine/Data/Shaders/Model/ModelCommon.h` or a `Lighting/` shared helper) and confirm the swizzle it currently applies.
2. **Read** the DataPacker IBL bake job for `samplerIrradiance` and `prefilteredMap` to determine the cubemap-axis convention each was baked with.
3. **Decide** between (a) consolidation and (b) in-place documentation based on the findings of steps 1-2.
4. **If (a)**: move the `y *= -1` into `ToCubemapCoord`; delete `Model.frag:253`; verify `Model.frag:208` (the diffuse `n` path) is consistent with the new helper behavior. If `Model.frag:208` would also receive the flip and that is correct, no further changes. If it would be a regression, option (a) is wrong — fall back to (b).
5. **If (b)**: add a comment at `Model.frag:253` explaining the reflection-only flip, citing the cubemap-bake convention that requires it. Keep the diffuse path untouched.

Either resolution is a single-file or two-file shader-side change. No C++ or pipeline-layout edits.

## Critical files

- `Engine/Data/Shaders/Model/Model.frag:252-254` — the reflection-build, sign-flip, and `GetIBLContribution` call site.
- `Engine/Data/Shaders/Model/Model.frag:208-209` — the `ToCubemapCoord(n)` (diffuse) and `ToCubemapCoord(reflection)` (specular) helper call sites inside `GetIBLContribution`.
- The shared header that defines the `ToCubemapCoord` helper — likely `Engine/Data/Shaders/Model/ModelCommon.h` or a sibling shared helper under `Engine/Data/Shaders/` (locate via `Grep` for `ToCubemapCoord` at execution time).
- DataPacker IBL bake job (likely under `DataPacker/Source/ExportJobs/`) — read-only, consulted to determine cubemap-axis convention. Not modified by this plan.

## Out of scope

- The other Z-up → Y-up conversion sites that already use `ToCubemapCoord` correctly: `Model.frag:208` (`ToCubemapCoord(n)`, diffuse irradiance) and `Model.frag:209` (`ToCubemapCoord(reflection)`, specular prefilter) are inside `GetIBLContribution` and route through the helper as intended. They are read for *context* (step 1 of the design) but not modified except as the natural consequence of option (a) folding additional logic into `ToCubemapCoord`.
- Changing the cubemap bake convention in DataPacker. If the bake is the source of the asymmetry, this plan documents it; it does not re-bake.
- Auditing other shaders (`Water.frag`, `Terrain.frag`, etc.) for similar inline axis sign flips. Scope is `Model.frag` only.
- Renaming or refactoring `ToCubemapCoord` for clarity beyond what option (a) requires.
- Performance changes. Both options are zero-cost: option (a) moves an existing multiply into a helper; option (b) adds a comment.

## Acceptance criteria

- A decision is recorded (in the commit message or in `Model.frag` as a comment) about whether the `y *= -1` was folded into `ToCubemapCoord` (option a) or documented in place (option b), with the rationale (which cubemap-bake convention drove the choice).
- If option (a): `Model.frag:253` is deleted; `ToCubemapCoord` contains the additional sign flip; the diffuse path at `Model.frag:208` produces visually identical output to before this plan (A/B against a reference scene with a reflective metallic model).
- If option (b): `Model.frag:253` is unchanged in code, but a comment immediately above or beside it explains the reflection-only flip and names the upstream convention (e.g., "prefilteredMap is baked Y-flipped relative to samplerIrradiance, see DataPacker/...").
- No regression in reflective-model rendering — top/bottom of reflections appear in the same orientation as before this plan.

## Notes

- Effort 1, Impact 1, Risks 0, Score 0. Tier Quick Win.
- The investigation step (1-2) is bounded — at most three files read. Most of the wall-clock cost is the A/B verification screenshot for option (a).
- This is a "grill the codebase, then either consolidate or document" plan by design — both outcomes are acceptable as long as the convention is no longer split silently across two locations.
