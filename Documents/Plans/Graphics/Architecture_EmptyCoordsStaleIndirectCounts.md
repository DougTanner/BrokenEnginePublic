# Architecture: Empty-Active-Coords Early Return Skips Indirect Zero-Writes

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Debug` (the finding generalizes past the target — it affects every indirect-count writer in the main pass, not just DebugRender).

`RenderFrameMain` early-returns when `rActiveCoords.empty()` (`MainUniforms.cpp:163-166`), but `Graphics::RenderMainPresentAcquire` still submits the record-once Main command buffer unconditionally afterwards (`Graphics.cpp:232` → `SubmitMainCommandBuffer` at `:237`). The early return skips every per-frame indirect-count write for that framebuffer index:

- `game::FrameInterpolate::BeginRender`/`EndRender` (`MainUniforms.cpp:195`/`:218`) — all collection instance counts
- `DebugRender::BeginRender`/`EndRender` (`:238-239`) — the four debug-primitive counts
- the water / water-skybox LOD `WriteIndirectBuffer` writes (`:180-182`)
- also skipped: `RenderLightingMain` (`:170`) and `gpBufferManager->ResetSkinningAllocations` (`:171`)

Under the CB re-record ban (`Graphics/CLAUDE.md`), record-once CBs depend on these per-frame host-visible writes; `Debug/CLAUDE.md` documents `EndRender`'s unconditional zero-write as load-bearing for exactly this reason. Consequence: on a populated→empty active-set transition, up to framebuffer-count frames re-draw the *previous* populated frame's instance counts against stale uniforms (ghost collection draws; ghost debug primitives in Debug config) until coords become active again.

## Design

### Step 0 — reachability: RESOLVED — verified unreachable (2026-06-10 verification pass)

A populated→empty `rActiveCoords` transition cannot occur on the client (the only build with Graphics):

- `Game::ComputeActiveSet()` (client branch, `Game.cpp:167-280`) never leaves the set empty: the gameplay path unconditionally appends `mClientGridCoord` if missing (`:192-195`); the main-menu path is `clear()` + `push_back(mClientGridCoord)` (`:252-253`).
- `Game::Reset()` (session reset, `Game.cpp:458-460`) ends with `SetClientGridCoord(kOriginCoord)` + `clear()` + `push_back(mClientGridCoord)` — non-empty across disconnect/reset.
- The boot prerender (`Main.cpp:222-226`) passes a hard-coded non-empty `bootActiveCoords = {kOriginCoord}`, not `mActiveCoords`.
- In the main loop, `ClientUpdate` (`Main.cpp:285`) precedes `Render` (`:293`) every iteration; `ClientUpdate` → `PrepareActiveSet` (`GameBase.cpp:58`) → `ComputeActiveSet` (`GameBase.cpp:446`) populates the set before the first loop Render. The only skip path is `IsStalled()` (`GameBase.cpp:44`), which is `mDesyncDebugState.iTick >= 0` (`ClientDesyncManager.h:15`) — a desync-active state impossible before the first reconcile, and when it trips mid-session it merely leaves the previous non-empty set in place.
- `GameBase::Render` itself already guards on `!rActiveCoords.empty()` (`GameBase.cpp:277`, `:285`), so even the theoretical empty case never had a populated predecessor frame whose counts could "ghost".

Per the original contingency, the fix therefore downgrades to documenting the invariant.

### Engine/Source/Graphics/Render/MainUniforms.cpp — `RenderFrameMain` [~5m]
- At the `rActiveCoords.empty()` early return (`:163-166`), add a comment stating: (a) the return exists because `rRenderInterpolates.at(cameraCoord)` (`:168`) would throw on an empty map; (b) skipping the indirect-count flush (`FrameInterpolate::BeginRender`/`EndRender`, `DebugRender::BeginRender`/`EndRender`, water LOD writes) is safe **only because** client `mActiveCoords` is never empty here — `Game::ComputeActiveSet` always includes `mClientGridCoord` and the boot prerender passes a non-empty list; (c) if that invariant ever breaks, the record-once Main CB re-draws the prior frame's instance counts (ghost draws) for up to framebuffer-count frames, and the empty path must then flush counters before returning.

### Alternative (grill may still choose it): defensive flush
- Verified viable if defense-in-depth is preferred over a comment: collection `BeginRender` resets its counters *before* the zero-capacity early return (e.g. `BillboardsRender.cpp:23-32`) and `EndRender` writes the indirect count unconditionally (`:113`), mirroring `DebugRender::EndRender`'s unconditional zero-write; `ResizeDynamicBufferIfNeeded` (`BufferManager.cpp:442-452`) no-ops at capacity 0 (`0 >= 0` size check returns nullptr) — no empty-list edge case. Shape: when empty, run `game::FrameInterpolate::BeginRender`+`EndRender` and `DebugRender::BeginRender`+`EndRender`, then return before `:168`.

## Critical files

- `Engine/Source/Graphics/Render/MainUniforms.cpp`
- Read-only references: `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` (`FrameInterpolate::BeginRender`/`EndRender`), `Engine/Source/Graphics/Debug/DebugRender.cpp` (`BeginRender`/`EndRender`), `Engine/Source/Graphics/Graphics.cpp` (`RenderMainPresentAcquire` submit path)

## Out of scope

- Any change to `DebugRender` itself — its zero-write contract is correct; the gap is the caller's early return.
- The Global pass (`RenderFrameGlobal`) — separate entry point, not gated by this early return.
- Collection `Render`-phase logic; per-coord interpolation.
- Gating or re-recording command buffers (banned path).

## Acceptance criteria

- The early return at `MainUniforms.cpp:163-166` carries a comment stating the never-empty invariant it relies on, why it is currently guaranteed (`Game::ComputeActiveSet` / boot prerender), and the ghost-draw consequence if it breaks. (If the grill instead picks the defensive flush: a frame with `rActiveCoords` empty submits zero instance counts for all collection and debug indirect draws.)

## Notes

- Render-side only: no CRC/determinism/network/`kiVersion` exposure.
- Step 0 reachability is resolved (unreachable — evidence in Design); the single remaining grill decision is comment-only (recommended; matches the trust-boundary directive — internal callers are assumed valid) vs the verified-viable defensive flush.
- No playtest needed for the comment path; the flush path would need only a trivial-revert sanity run.

## Verification Notes

Independently verified 2026-06-10 (external-deep-analysis verification pass):

- **All cited file:line locations verified exact**: `MainUniforms.cpp:163-166` (early return), `:168` (`rRenderInterpolates.at(cameraCoord)`), `:170` (`RenderLightingMain`), `:171` (`ResetSkinningAllocations`), `:180-182` (water/water-skybox `WriteIndirectBuffer`), `:195`/`:218` (`FrameInterpolate::BeginRender`/`EndRender`), `:238-239` (`DebugRender::BeginRender`/`EndRender`); `Graphics.cpp:229-241` (`RenderMainPresentAcquire` calls `RenderFrameMain` at `:232`, submits the Main CB unconditionally at `:237`); `Frame.cpp:490-531`; `BufferManager.cpp:442-452`.
- **Reachability resolved as unreachable** — full trace recorded in Step 0 above (ComputeActiveSet both branches, Game::Reset, boot prerender, ClientUpdate-before-Render ordering, IsStalled semantics). The plan was REWRITTEN from "fix + verify reachability" to "document invariant (comment-only), flush as grill alternative".
- **Fix-shape sanity check passed**: counters reset before the zero-capacity early return in collection `BeginRender` (`BillboardsRender.cpp:23`), unconditional indirect write in `EndRender` (`:113`), `ResizeDynamicBufferIfNeeded` safe at capacity 0 — so the alternative flush has no empty-list edge case if ever needed.
- **No plan overlap**: no other live Graphics plan covers this early return (`DisabledPassGatingPerfAudit.md`'s "empty" note concerns lighting deposits, unrelated).
- **Score impact**: provisional Small E2/I2/R2=2 should drop to Quick Win E1/I1/R0=0 (comment-only change per scoring anchors: Impact 1 "comment cleanup", Risks 0 "comment-only").
