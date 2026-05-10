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

- **TweaksScreen** - Game override of `engine::TweaksScreenBase`; implements the hex-shield section, per-explosion-type Particles tabs (3 sub-tabs: Missile / Player / Spaceship), and per-sound effects (Blasters / Missiles / Explosions / Hits) inside the engine Sound section, and provides game tabs hosted inside engine sections (Deposits inside Wind; Visible/Lighting inside Lighting Effects). Split into per-tab files; each per-tab `.cpp` registers its slider labels via an anonymous-namespace `engine::TweaksSliderMapRegistrar` co-located with its `Render*` function (no central registration in the screen class).
- **HudScreen** - In-game overlay: left fleet panel, right focused-player controls. Panels are synced — mouse proximity to either anchor opens both, and a shared timed auto-un-hide opens both on triggering events. The right panel additionally gates on a focused player being present in the snapshot, so when no focused player exists only the left panel can force-open. Left panel force-opens when the camera has no follow target or the focused fleet has no presence in any subscribed frame.
- **MainMenuScreen** - Local/remote server selection, settings, quit, language. Compile-time `Pch.h` toggles gate auto-launch/auto-connect.
- **ModalScreen** - Centered modal for connection-rejection and desync messages.
- **PauseMenuScreen** - In-game pause overlay.
- **GraphicsMenuScreen** - Rendering settings bound to engine Wrappers.
- **SoundMenuScreen** - Volume sliders with defaults reset.
- **DeathMenuScreen** - Stub; unique in compiling into both builds.

## MenuUtils

Shared helpers: `kfMenuUiScale` constant, `ScopedMenuScale`, UTF-32 to UTF-8 conversion, and ImGui-to-`engine::Wrapper` control bindings.
