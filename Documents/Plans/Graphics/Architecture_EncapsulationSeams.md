# Architecture: Encapsulation Seams (Graphics lifecycle)

## Context

Source: /external-architecture-review on `Engine/Source/Graphics` (non-recursive). `Graphics` is a healthy
composition root (manager construction order, tiered destroy state machine), but one seam leaks: a
two-method ownership-transfer invariant spanning another manager's internals.

## Design

### SwapchainManager handle handoff — Engine/Source/Graphics/Graphics.cpp + Managers/SwapchainManager.h
- `Graphics::Destroy` reaches into `SwapchainManager` internals to keep the old swapchain alive across
  recreation: it stashes and nulls `mpSwapchainManager->mVkSwapchainKHR` (`Graphics.cpp:684-688`), consumed
  by the next `SwapchainManager` construction (`Graphics.cpp:285-286`, ctor parameter declared
  `SwapchainManager.h:17`). Add `SwapchainManager::ReleaseHandleForRecreation()` (returns the handle, nulls
  the member) so the ownership-transfer invariant lives in one named method instead of spanning two
  functions and a raw member write. [~15m]

## Critical files
- `Engine/Source/Graphics/Graphics.cpp`
- `Engine/Source/Graphics/Managers/SwapchainManager.h` (and `.cpp`)

## Out of scope
- Converting the engine's `game::LoadTweaksSettings()` / `SaveTweaksSettings()` calls (`Graphics.cpp:338,694`,
  `Main.cpp:200,325`) to `GameBase` virtuals — dropped at verification; see Verification Notes.
- Relocating the `FullDetail` / `WaterFullDetail` / `SmokeSimulationPixels` / `SmokeSimulationPixelsY`
  free functions (`Graphics.h:51-54`, `Graphics.cpp:15-82`) next to their manager consumers — dropped at
  verification as organizational-only; see Verification Notes.
- Relocating `Islands` into `Managers/` — it matches the manager definition (`gp*` global, constructed in
  `Graphics::Create` order) but the move is organizational only, with vcxproj/filter churn and no behavioral
  or interface win; not filed.
- The three-predicate descriptor-patch drain gate (`Graphics.cpp:197`) — well-commented and clear as
  written; wrapping it in a named predicate would only move the comment.
- `RecreateResources`' two-level `mRenderTargetTextures` reach-through — sanctioned by
  `Managers/CLAUDE.md` ("owned-by-value structs are reached through their manager").
- Any change to destroy-tier semantics, manager construction order, or settings polling.

## Acceptance criteria
- The swapchain handle transfer is a single `SwapchainManager` method; no raw `mVkSwapchainKHR` member write
  remains in `Graphics.cpp`; client builds clean and the recreate path behaves identically.

## Notes
- Client-only lifecycle code; no determinism/CRC, replay, or network exposure. Destroy/recreate paths need a
  settings-change smoke test (toggle multisampling or resolution) after the swapchain-handle change.
- No grill decisions remain (method name is a trivial choice).

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- Kept item: stash/null at `Graphics.cpp:686-687` (inside the `:684-688` recreation-only guard), consumption
  at `:285-286`; `mVkSwapchainKHR` is a public member (`SwapchainManager.h:38`) and the ctor takes
  `VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE` (`SwapchainManager.h:17`). Item is mechanical and verified.
- Dropped: tweaks-settings `GameBase` virtuals. The cited call sites are all real
  (`game::LoadTweaksSettings` declared `ClientSettings.h:17`, `SaveTweaksSettings` `:16`; calls at
  `Graphics.cpp:338` and `:694` under `kbDebugInput` + null gates, `Main.cpp:200` and `:325`), and no
  existing `GameBase` virtual covers them — but `Engine/Source/CLAUDE.md` Hub Conventions explicitly narrows
  engine→game findings worth fixing to engine *types* naming game concepts; engine→game free-function calls
  are pervasive and accepted (`Main.cpp` also calls `game::LoadClientState`, `game::SaveSoundSettings`,
  `game::SaveGraphicsSettings`, `game::ServerUpdateDisplayStats`). Converting two of ~six such calls adds a
  virtual-hook API without eliminating the pattern — decoupling churn of the kind the root CLAUDE.md says
  not to file.
- Dropped: resolution-policy relocation. Consumer sites verified (`TextureManager.cpp:20,51,67`,
  `RenderTargetTextures.cpp:215,243`, `Profile/ProfileScreens.cpp:201` — note `Graphics.cpp:516`'s Refresh
  LOG also calls `SmokeSimulationPixels()`), but the move is purely organizational — no interface or
  behavior win, same rationale the plan already used to not-file the `Islands` relocation. The original
  item even marked itself "Optional — take or drop at grill".
