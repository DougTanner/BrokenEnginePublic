# `/Projects/BrokenEngineSandbox/Source/Ui/Screens/` - Game UI Screens

ImGui screens rendered by engine's `ImGuiManager`. Client-only (`#ifdef BT_CLIENT`) except `DeathMenuScreen`, which compiles in both builds (header is pulled into the server vcxproj).

## Shared Patterns

Children do not re-document these:

- **Menu scale**: every screen calls `ImGui::SetWindowFontScale(kfMenuUiScale)` after `Begin()`; `ScopedMenuScale` scales padding/spacing uniformly (see `MenuUtils`).
- **Visibility**: screens check `gpGame->meUiState` and game flags, early-return when inactive.
- **Positioning**: proportional screen percentages for resolution independence.
- **Opaque-UI occlusion**: screens that want depth-pre-pass culling register their rect via `gpImGuiManager->RegisterOpaqueRect()` when `gOpaqueUi` is enabled.
- **Networked controls**: disable via `NetworkUiControl` while awaiting server confirmation.
- **Localization**: UTF-32 table converted to UTF-8 via workbuffer helpers in `MenuUtils`. Chinese font (`gpImGuiManager->mpChineseFont`) is pushed around text when the selected language requires it.
- **Wrapper binding**: settings controls (checkbox, float slider, step buttons) bound to `engine::Wrapper` globals via helpers in `MenuUtils`.

## Screens

- **TweaksScreen** - Game override of `engine::TweaksScreenBase` (mechanics, slider-map registrar convention, and subtab/column rules documented at the [engine hub](../../../../../Engine/Source/Ui/Screens/TweaksScreen/CLAUDE.md), not repeated here). `Render()` early-returns under `kbDebugInput` unless `mbShowImGui`. Fills the base's game extension hooks: the two pure-virtual sections (Hex Shield; Particles — Missile/Player/Spaceship subtabs) and the default-empty tab hooks hosted inside engine sections (Smoke Deposits in Smoke, Wind Deposits in Wind, Visible + Lighting tabs in Lighting Effects, Sound Effects in Sound). One section/hook per file; each `.cpp` owns an anonymous-namespace `engine::TweaksSliderMapRegistrar` for exactly its labels, co-located with the `Render*` function.
- **HudScreen** - In-game overlay: left fleet panel (fleet navigation, member list, spawn/respawn/delete, nav-delay slider), right focused-player controls (weapon-mode toggle). Both slide on/off screen from their off-screen edge via exponential interpolation toward a 0..1 openness target. Mouse proximity to either anchor opens both (strict sync); a force-open target additionally fires when the focused fleet has no presence in any subscribed frame — held for a grace period to absorb cell-boundary hand-offs, logged once on its rising edge. The right panel gates on a focused player existing in the current snapshot, so when none exists only the left panel can force-open. Server-confirmed actions disable via `NetworkUiControl` (and `game::Game`'s weapon-mode / nav-delay controls) while pending.
- **MainMenuScreen** - Local/remote server selection, settings, quit, language. Compile-time `Pch.h` toggles gate auto-launch/auto-connect.
- **ModalScreen** - Centered modal for connection-rejection and desync messages.
- **PauseMenuScreen** - In-game pause overlay.
- **GraphicsMenuScreen** - Rendering settings bound to engine Wrappers.
- **SoundMenuScreen** - Volume sliders with defaults reset.
- **DeathMenuScreen** - Stub; unique in compiling into both builds.

## MenuUtils

Shared helpers: `kfMenuUiScale` constant, `ScopedMenuScale`, UTF-32 to UTF-8 conversion, and ImGui-to-`engine::Wrapper` control bindings.
