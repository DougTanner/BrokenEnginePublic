# Refactor: API + Hot-Path Cleanups (Graphics top-level)

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics` (non-recursive). Small mechanical cleanups:
a dead boolean parameter whose false-path is also a latent hazard, a zero-value wrapper, and one main-loop
heap allocation at the directory's primary call-in site.

## Design

### Engine/Source/Graphics/OneShotCommandBuffer.{h,cpp}
- Remove the `bWait` parameter from `Execute(bool bWait)` (`OneShotCommandBuffer.h:13`) — every call site in
  the repo passes `true` (`ProfileManagerBase.cpp:63`, `Objects/Texture.cpp:235,306`,
  `Objects/Buffer.cpp:245`, `Managers/TextureManager.cpp:904`, `Managers/TextureCache.cpp:110,201`;
  grep-verified). The `false` path (submit with `VK_NULL_HANDLE` fence, no wait,
  `OneShotCommandBuffer.cpp:39-42,56-61`) is dead — and hazardous if ever used: the destructor
  `vkFreeCommandBuffers` (`OneShotCommandBuffer.cpp:32`) would free a still-pending command buffer.
  `Execute()` becomes unconditional reset + fenced submit + wait. [~15m]

### Engine/Source/Graphics/CameraBase.h
- Delete `AabbIntersectsVisibleArea` (`CameraBase.h:55-58`) — a zero-value pass-through to
  `common::AabbIntersectsArea` (identical signature, `Common/Math/MathUtils.h:237`); convert its sole call
  site (`Engine/Source/Frame/Collections/AreaLights/AreaLightsRender.cpp:103`,
  `game::gpCamera->AabbIntersectsVisibleArea(area, min, max)` → `common::AabbIntersectsArea(...)`).
  Re-grep at execution to confirm no new sites appeared. [~10m]

### Projects/BrokenEngineSandbox/Source/Game.cpp (:268-278, the UpdateActiveIslands feed)
- `std::vector<engine::GridCoord> subscribedCoords` + `reserve` (`Game.cpp:268-269`) heap-allocates every
  client frame in the main loop — the documented pattern is `common::gpThreadLocal->mWorkbuffer`
  (root `CLAUDE.md` Workbuffer rule). The block already sits under the function-wide
  `ScopedSuppressAllocationTracking` (`Game.cpp:173`), so the tracker does not fire — this is a
  per-frame-allocation/pattern cleanup, not a tracker fix. Build the filtered coord list in the workbuffer
  (`PushBuffer` sized to `mActiveCoords.size()`, count tracked separately or shrunk via
  `ShrinkLastPushBuffer`) and pass it through; change `Islands::UpdateActiveIslands` (`Islands.h:46`) to
  accept `std::span<const GridCoord>` (sole call site is `Game.cpp:278`). Nesting is safe:
  `UpdateActiveIslands`' own `PushBuffer` (`Islands.cpp:121`) is a `ScopedWorkbufferAllocation` that pops
  (LIFO) before the function returns, while the caller's outer allocation stays live. [~30m]

## Critical files
- `Engine/Source/Graphics/OneShotCommandBuffer.h`, `OneShotCommandBuffer.cpp` + the seven `Execute(true)`
  call sites
- `Engine/Source/Graphics/CameraBase.h` + `Engine/Source/Frame/Collections/AreaLights/AreaLightsRender.cpp`
- `Engine/Source/Graphics/Islands.h`, `Islands.cpp`
- `Projects/BrokenEngineSandbox/Source/Game.cpp`

## Out of scope
- The `OneShotCommandBuffer` thread-identity assert — `Graphics/Architecture_InvariantHardening.md`.
- `SaveScreenshot`'s allocations — `Graphics/Architecture_ScreenshotFenceDeadlock.md`.
- `InVisibleArea`'s four trailing positional adjust floats (`CameraBase.h:43-53`) — dropped at verification;
  see Verification Notes.
- Any visibility-test behavior change — comparison logic is untouched in every item.
- Other call-in sites of the visible-area helpers (collections render code) beyond mechanical signature
  updates.

## Acceptance criteria
- `Execute()` has no parameter and no unfenced-submit path; `AabbIntersectsVisibleArea` has zero references;
  no `std::vector` construction remains in `Game.cpp`'s island-update block; client + server build clean.

## Notes
- No determinism/CRC exposure: `subscribedCoords` feeds client-only island rendering (`Game.cpp:268-278` is
  the `#else` branch of `#if defined(BT_SERVER)` in `ComputeActiveSet`), and the camera helper is
  render-side. Compile-checked except the workbuffer conversion, which needs a client smoke run.
- One grill decision: `std::span` vs pointer+count for `UpdateActiveIslands`.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- `Execute(bool bWait)` at `OneShotCommandBuffer.h:13`; repo-wide `.Execute(` grep (excluding ThirdParty)
  returns exactly the seven cited sites, every one passing literal `true`. False-path lines `:39-42,56-61`
  and dtor free `:32` exact. Item stands as written.
- `AabbIntersectsVisibleArea` at `CameraBase.h:55-58`, forwarding to
  `common::AabbIntersectsArea(XMFLOAT4, FXMVECTOR, FXMVECTOR)` (`MathUtils.h:237`) — signatures match, the
  conversion is mechanical. Correction: the original "~4 files" call-site estimate was wrong — there is
  exactly ONE call site (`AreaLightsRender.cpp:103`); item updated accordingly.
- Dropped: the optional `InVisibleArea` adjust-parameter fold. Its premise ("call sites that pass two or
  three of them read ambiguously") is false — the only three call sites are `SpaceshipsRender.cpp:103` and
  `MissilesRender.cpp:73` (no adjusts) and `HexShieldsRender.cpp:64` (all four, same `kfAdjust` value). No
  partial-adjust site exists, so the fold is signature churn with no readability gain (the one adjust user
  would become an `XMFLOAT4 {k, k, k, k}`).
- `Game.cpp:268-278` exact (`subscribedCoords` decl `:268`, `reserve` `:269`, call `:278`); the block is
  client-only (the `#else` of `#if defined(BT_SERVER)` at `Game.cpp:169-171`, closed `:279`). Added the
  precision that `ScopedSuppressAllocationTracking` at `Game.cpp:173` already covers the block (the plan's
  benefit is the per-frame heap allocation, not a tracker trip). Workbuffer nesting feasibility confirmed
  against the API (`Common/Workbuffer.h`): `PushBuffer` returns RAII `ScopedWorkbufferAllocation`, frames
  are strictly LIFO, and the callee's `Islands.cpp:121` allocation pops before return — no reentrancy
  hazard. Note `ShrinkLastPushBuffer` asserts it targets the current top frame
  (`Workbuffer.h:57-63`), so if used it must run before calling `UpdateActiveIslands`.
- `UpdateActiveIslands` declared `Islands.h:46`; sole call site `Game.cpp:278` confirmed by repo grep.
