# Architecture: Wrapper Undocumented Contracts (Threading, Float Storage)

## Context

Source: /external-architecture-review on `Engine/Source/Ui` (non-recursive). The `Wrapper` class
(`WrapperBase.h:10-209`) has no atomics or synchronization; safety relies on an *unstated* single-writer /
sequenced-reader convention — writes happen on the main thread during `ImGuiManager::Prepare`
(`Graphics/Managers/ImGuiManager.cpp:242-286`), reads happen later in the same thread's frame (uniform
builders, `Graphics::Refresh`). Dispatch workers *do* read wrappers during parallel frame ticks
(`Frame/NavQuery.cpp:583` via the `Dispatch()` fan-out) — currently benign solely because `gBaseHeight` has
no runtime writers, but the same pattern applied to any Tweaks-bound wrapper would be a cross-thread
unsynchronized read with no comment warning against it. Separately, `Wrapper` stores every flavor (bool,
discrete-enum, int64 indices) in a single `float` (`WrapperBase.h:201-208`) — and the `< 2^24`
integer-exactness ceiling is an undocumented invariant that is **already violated by one allowed-set
member**: `gPresentMode`'s allowed set (`GraphicsSettingsWrappersBase.cpp:7`) contains
`VK_PRESENT_MODE_FIFO_LATEST_READY_KHR` = 1000361000, which rounds to 1000361024.0f in float, so
`Get<VkPresentModeKHR>()` would return an invalid enumerator if that mode were ever stored. Latent today:
no UI exposes the fourth mode (`GraphicsMenuScreen.cpp:58-62` offers only Immediate/Mailbox/FIFO) and
`SwapchainManager.cpp:161-176` resolves to FIFO/Mailbox/Immediate and `Reset`s the wrapper before the
swapchain consumes `Get<VkPresentModeKHR>()` at `:246` — but the ceiling needs documenting before someone
binds it.

## Design

### Engine/Source/Ui/WrapperBase.h
- Document the threading contract on the `Wrapper` class comment (`:10`): single writer (main-thread ImGui
  `Prepare`), readers sequenced later in the same frame on the main thread; wrappers read inside
  `gpMultithreading->Dispatch()` regions must have no runtime writers (cite the `gBaseHeight` precedent /
  its replacement constant once `Frame/Architecture_BaseHeightWrapperDeterminism.md` lands). [~10m]
- Document the float-storage exactness invariant at the value members (`:201-208`): all flavors round-trip
  through `float`; integer-backed wrappers (discrete-enum `mAllowed` values, int64 indices) must stay below
  2^24 or typed `Get<T>()` round-trips lose exactness. Cite the live cautionary example:
  `VK_PRESENT_MODE_FIFO_LATEST_READY_KHR` (1000361000 → 1000361024.0f) in `gPresentMode`'s allowed set is
  only safe because `SwapchainManager` normalizes the wrapper to FIFO/Mailbox/Immediate before any Vulkan
  consumption. [~5m]
- Document the `Toggle()` flavor constraint (`:66-69`): bool wrappers only — it writes `mfCurrent` raw,
  bypassing `Snap` (`:196-199`), `Set(float)`'s clamp (`:123`), and the discrete-enum allowed-set check via
  `GetIndex()` (`:131-138`). Both current callers are bool wrappers (`Game.cpp:604` `gFullscreen`,
  `Game.cpp:760` `gDebugTexture`). A runtime ASSERT was assessed and rejected: the bool ctor leaves no
  flavor tag to test, and adding one solely for enforcement lands on the wrong side of the repo's
  no-defensive-validation policy — comment only. [~5m]

## Critical files
- `Engine/Source/Ui/WrapperBase.h` (comments only)

## Out of scope
- Adding atomics/synchronization to `Wrapper` — no observed race; the contract is the design, the gap is
  documentation.
- Enforcing the 2^24 ceiling with an assert — boundary validation between our own functions is against repo
  policy; the one over-ceiling allowed-set member (`gPresentMode`'s FIFO_LATEST_READY) is unreachable into
  Vulkan today (see Context). If FIFO_LATEST_READY is ever to be selectable, that is a bugfix plan of its
  own (store the index, not the enum value), not a comment.
- `Wrapper::GetIndex` fail-loud behavior (`Engine/WrapperGetIndexFailLoud.md`) and the `gBaseHeight`
  conversion (`Frame/Architecture_BaseHeightWrapperDeterminism.md`) — same file, different symbols; this
  plan is comment-only and must not collide with their edits (see Order.md File Groups).

## Notes
- Comment-only change; zero behavior, determinism, or build exposure. No grill decisions.

## Verification Notes (2026-06-10)
- Threading claims re-derived: `Wrapper` (`WrapperBase.h:10-209`) has no atomics; wrapper writes happen via
  the Tweaks/menu screens rendered inside `ImGuiManager::Prepare` (`ImGuiManager.cpp:242-286`,
  `mpTweaksScreen->Render()` at `:278`) on the main thread; `Frame/NavQuery.cpp:583` (and `:603`) read
  `gBaseHeight.Get()` from frame code that runs under `gpMultithreading->Dispatch()` per the Engine hub's
  Tick Flow. Repo-wide grep confirms `gBaseHeight` has zero runtime writers (only `.Get()` reads plus the
  definition `WrapperBase.cpp:9`).
- `Toggle()` (`:66-69`) bypass claims confirmed against `Snap` (`:196-199`), `Set(float)` clamp (`:123`),
  and `Set<T>`→`GetIndex()` (`:131-138`). Exactly two callers repo-wide: `Game.cpp:604`
  (`engine::gFullscreen.Toggle()`) and `Game.cpp:760` (`engine::gDebugTexture.Toggle()`); both are bool-ctor
  wrappers (`GraphicsSettingsWrappersBase.cpp:6`, `WrapperBase.cpp:24`).
- Rewrite during verification: the original "exact for every value currently stored / current values max out
  at 255 / VK enum range" claim was wrong — `VK_PRESENT_MODE_FIFO_LATEST_READY_KHR` (1000361000) in
  `gPresentMode`'s allowed set exceeds 2^24 and is not float-representable (rounds to 1000361024). Verified
  it is unreachable into Vulkan today (`SwapchainManager.cpp:161-176` normalizes + `Reset`s before the
  `Get<VkPresentModeKHR>()` at `:246`; no UI exposes it), so the plan stays comment-only, but the doc text
  now cites this as the cautionary example instead of asserting universal exactness.
- File-collision checks: `Engine/WrapperGetIndexFailLoud.md` (edits `GetIndex` `:172-187`) and
  `Frame/Architecture_BaseHeightWrapperDeterminism.md` (deletes `gBaseHeight` `:214`/`.cpp:9`) both touch
  `WrapperBase.h` — different symbols, no textual overlap with the three comment sites, but co-schedule per
  Order.md File Groups. If the BaseHeight plan lands first, cite its replacement constant
  (`kfBaseHeight`) in the threading comment instead of the wrapper.
