<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-24T14:53:01.000Z","dependsOn":[]} -->
# Graphics-settings screen exempt from the Opaque UI option

## Context

The Opaque UI option (`engine::gOpaqueUi`) improves FPS while UI is on-screen in two ways:

1. It forces ImGui window/child/popup background alpha to `1.0` (no alpha blending of panel backgrounds).
2. It feeds a UI depth pre-pass (`kPipelineUiDepthPrepass`): `ImGuiManager::RegisterOpaqueRect` records each panel's screen rectangle, `UpdateUiRectBuffers` uploads them and sets the indirect draw `instanceCount`, and the pre-pass writes depth so the 3D world behind the panels is depth-culled and never shaded.

When the player is tweaking graphics options in the graphics-settings screen, they want to observe **worst-case** FPS so the numbers reflect gameplay, not the cheaper opaque-UI case. Therefore, while `game::UiState::kGraphicsSettings` is the active UI state, the UI must render as fully transparent — no forced background alpha and no depth pre-pass occlusion — regardless of the Opaque UI setting. The persisted setting value is untouched; this is a runtime display exemption only. The Opaque UI checkbox on that same screen continues to toggle and persist the setting; it simply has no visible effect while that screen is open, which is the intended worst-case view.

This mirrors the existing per-screen graphics override precedent `Camera::SunAngle()` (`Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp:317-331`), which overrides a render value while `meUiState == UiState::kGraphicsSettings`.

## Design

Add a client-only game hook reporting whether the UI must be forced transparent, and consume it at the three opaque-UI decision sites in `ImGuiManager`. The effective opaque state becomes `gOpaqueUi.Get<bool>() && !ShouldForceTransparentUi()`.

### 1. New game hook

- `Engine/Source/GameBase.h`: inside the existing `#if defined(BT_CLIENT)` hook block (alongside `ShouldShowInGameUi()` at line 147), add pure virtual `virtual bool ShouldForceTransparentUi() = 0;`.
- `Projects/BrokenEngineSandbox/Source/Game.h`: declare the override next to `ShouldShowInGameUi()` (line 63), inside the existing `#if defined(BT_CLIENT)` block.
- `Projects/BrokenEngineSandbox/Source/Game.cpp`: define the override next to the other `Should*` overrides (after `ShouldShowInGameUi()`, before the `#endif // BT_CLIENT` at line 549), in its own `#if defined(BT_CLIENT)` block matching the sibling pattern:
  ```cpp
  bool Game::ShouldForceTransparentUi()
  {
      return meUiState == kGraphicsSettings;
  }
  ```

`game::Game` is the only `engine::GameBase` subclass (`Projects/BrokenEngineSandbox/Source/Game.h:48`; confirmed no other derivations), so the new pure virtual requires exactly this one override.

### 2. Consume the hook in ImGuiManager

All three sites and the new members are client-only; the whole subtree is already `BT_CLIENT`-guarded. `game::gpGame` and the `GameBase` interface are already reachable here (`ImGuiManager.cpp:511` already calls `game::gpGame->ShouldShowInGameUi()`), so no new includes are needed.

- `Engine/Source/Graphics/Managers/ImGuiManager.h`: add private member `bool mbPreviousForceTransparentUi = false;` and private method declaration `bool OpaqueUiActive() const;`.

- `Engine/Source/Graphics/Managers/ImGuiManager.cpp`, new helper (place near `ApplyThemeColors`):
  ```cpp
  bool ImGuiManager::OpaqueUiActive() const
  {
      // gpGame is null while this manager is constructed (Main.cpp constructs Graphics before Game); the graphics
      // screen is never the startup state, so the raw setting governs the construction-time theme apply.
      return gOpaqueUi.Get<bool>() && !(game::gpGame != nullptr && game::gpGame->ShouldForceTransparentUi());
  }
  ```
  The `game::gpGame != nullptr` guard is required, not defensive gold-plating: `Main.cpp:262` constructs `Graphics` (which constructs `ImGuiManager`, whose constructor calls `ApplyThemeColors` at `ImGuiManager.cpp:155`) before `Main.cpp:266` constructs `game::Game` that publishes `gpGame`. The value is legitimately null on that one call.

- `ApplyThemeColors` (`ImGuiManager.cpp:230`): change `float fAlpha = gOpaqueUi.Get<bool>() ? 1.0f : gUiOpacity.Get();` to `float fAlpha = OpaqueUiActive() ? 1.0f : gUiOpacity.Get();`. This matters because `ApplyThemeColors` also re-runs on a theme change (`ImGuiManager.cpp:435`), and the theme dropdown lives on the graphics-settings screen — without this, changing theme there would restamp opaque backgrounds.

- `Prepare` per-frame alpha block (`ImGuiManager.cpp:438-448`): extend the change detection so entering/leaving the graphics screen retriggers the alpha rewrite (the state change is not tracked by any `Wrapper`). `gpGame` is always valid in `Prepare`:
  ```cpp
  auto [bOpaqueUi, bPreviousOpaqueUi, bOpaqueUiChanged] = gOpaqueUi.Changed<bool>();
  auto [fUiOpacity, fPreviousUiOpacity, bUiOpacityChanged] = gUiOpacity.Changed<float>();
  bool bForceTransparent = game::gpGame->ShouldForceTransparentUi();
  bool bForceTransparentChanged = bForceTransparent != mbPreviousForceTransparentUi;
  mbPreviousForceTransparentUi = bForceTransparent;
  if (bOpaqueUiChanged || bUiOpacityChanged || bForceTransparentChanged)
  {
      ImGuiStyle& rStyle = ImGui::GetStyle();
      float fAlpha = (bOpaqueUi && !bForceTransparent) ? 1.0f : fUiOpacity;
      rStyle.Colors[ImGuiCol_WindowBg].w = fAlpha;
      rStyle.Colors[ImGuiCol_ChildBg].w = fAlpha;
      rStyle.Colors[ImGuiCol_PopupBg].w = fAlpha;
  }
  ```

- `RegisterOpaqueRect` (`ImGuiManager.cpp:574`): change the early-return guard from `if (!gOpaqueUi.Get<bool>() || miOpaqueRectCount >= kiMaxUiRects)` to `if (!OpaqueUiActive() || miOpaqueRectCount >= kiMaxUiRects)`. This is a global gate covering every screen's rect registration, so on the graphics screen no rects are recorded; `UpdateUiRectBuffers` then writes `instanceCount = 0` and the depth pre-pass draws nothing (full world shaded behind panels). No pre-pass, buffer, pipeline, or shader changes are needed.

Update the adjacent comments at `ImGuiManager.cpp:229` and `:438` to note the graphics-screen exemption.

### Known, accepted latency

`meUiState` transitions during the screens' `Render()` calls, which run after `Prepare`'s alpha block within the same frame, so the background-alpha change lags the screen transition by one frame. This is imperceptible and consistent with the existing per-frame flow; no mitigation.

## Out of scope

In scope is exactly: the one new `GameBase` pure virtual and its single `game::Game` override, and the four edits in `ImGuiManager` (`.h` member + method declaration; `.cpp` helper, `ApplyThemeColors` alpha line, `Prepare` alpha block, `RegisterOpaqueRect` guard) plus the two adjacent comment updates. The listed scope is both target and ceiling — make the smallest complete change and add no abstractions, configuration, refactors, or fixes to adjacent code encountered.

Naming a file grants no permission to touch regions beyond those named above plus the mechanical necessities (member declaration, method declaration) the change requires. Specifically do **not** change:

- Persistence: `Projects/BrokenEngineSandbox/Source/ClientSettings.cpp` (`GraphicsSettingsFlags::kOpaqueUi`, save/load/reset) — the setting value is untouched, no version bump.
- The Opaque UI checkbox and any other control in `Projects/BrokenEngineSandbox/Source/Ui/Screens/GraphicsMenuScreen.cpp`.
- The pre-pass draw (`CommandBufferRecordMain.cpp`), pipeline (`PipelineManager`), rect storage buffers (`BufferManager`), or the shader `Engine/Data/Shaders/Ui/UiDepthPrepass.vert`.
- The `gOpaqueUi` wrapper definition/default (`Engine/Source/Ui/GraphicsSettingsWrappersBase.{h,cpp}`).

## Risk

Tier 2 — scoped client-only render behavior in one subsystem (`ImGuiManager`) plus a single game hook. No determinism/CRC surface (all UI rendering is client-only and outside the CRC), no wire/protocol, serialization, or data-layout change, no threading change, no trust boundary. All touched code is `BT_CLIENT`-only; `GameBase.h` is shared but the new symbol is inside its `BT_CLIENT` block.

Invariants to preserve: the new pure virtual must have its one override or the client will not link; the `game::gpGame` null guard in `OpaqueUiActive()` must remain (construction-order dependency); the `Prepare` state-delta tracking must update `mbPreviousForceTransparentUi` every frame so a stale cached value cannot suppress a needed alpha rewrite.

## Acceptance criteria

- Client and server both compile (`GameBase.h` is shared; confirm the `BT_CLIENT`-guarded addition does not affect the server build).
- Runtime (agent harness): with Opaque UI enabled, open the graphics-settings screen — panel backgrounds render translucent (alpha = `gUiOpacity`, not opaque) and the 3D world is visible and shaded behind the panels; navigate back to the pause menu — backgrounds render opaque again and world occlusion behind panels resumes. Screenshot evidence of the world visible behind the graphics-settings panel is sufficient to confirm the depth pre-pass is disabled on that screen.
