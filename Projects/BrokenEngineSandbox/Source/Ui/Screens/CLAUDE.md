# `/Projects/BrokenEngineSandbox/Source/Ui/Screens/` - Game UI Screens

ImGui screens rendered by engine's `ImGuiManager`. The `.cpp` bodies are fully `#if defined(BT_CLIENT)`-wrapped and client-vcxproj-only, except `DeathMenuScreen` and `MenuUtils`, which are unguarded and compile in both vcxprojs. Headers stay unguarded so they parse in any TU (`HudScreen.h` guards its members; `TweaksScreen.h` is fully guarded — its base exists only on the client).

## Shared Patterns

Children do not re-document these:

- **Menu scale**: every screen calls `ImGui::SetWindowFontScale(kfMenuUiScale)` after `Begin()` (TweaksScreen scales via the engine base's `kfUiScale` instead); `ScopedMenuScale` scales padding/spacing uniformly (see `MenuUtils`).
- **Visibility**: screens self-gate on `gpGame->meUiState` plus game flags, early-returning when inactive, and transition by writing `meUiState` directly in button handlers — no state-machine object (MainMenu/Pause partition `kPause` on `InMainMenu()`). Self-gating is mandatory: ImGuiManager outer-gates the in-game screens on `ShouldShowInGameUi()` but renders MainMenu and Modal unconditionally, so a client with Tweaks persisted-open can still reach Connect.
- **Positioning**: proportional screen percentages for resolution independence.
- **Opaque-UI occlusion**: opaque-background screens register their rect via `gpImGuiManager->RegisterOpaqueRect()` unconditionally (`gOpaqueUi` gating lives in ImGuiManager); transparent-window screens (MainMenu, Pause) don't register.
- **Networked controls**: disable via `NetworkUiControl` while awaiting server confirmation.
- **Localization**: UTF-32 table converted to UTF-8 via workbuffer helpers in `MenuUtils`. Chinese font (`gpImGuiManager->mpChineseFont`) is pushed around text when the selected language requires it.
- **Wrapper binding**: settings controls (checkbox, float slider, step buttons) bound to `engine::Wrapper` globals via helpers in `MenuUtils`.

## Screens

- **TweaksScreen** - Game override of `engine::TweaksScreenBase` (mechanics, slider-map registrar convention, and subtab/column rules documented at the [engine hub](../../../../../Engine/Source/Ui/Screens/TweaksScreen/CLAUDE.md), not repeated here). `Render()` early-returns under `kbDebugInput` unless `mbShowImGui`. Fills the base's game extension hooks: the two pure-virtual sections (Hex Shield; Particles — Missile/Player/Spaceship subtabs) and the default-empty tab hooks hosted inside engine sections (Smoke Deposits in Smoke, Wind Deposits in Wind, Visible + Lighting tabs in Lighting Effects, Sound Effects in Sound). One section/hook per file.
- **HudScreen** - In-game overlay: left fleet panel (fleet navigation, member list, spawn/respawn/delete, nav-delay slider), right focused-player controls (weapon-mode toggle). Both slide on/off screen from their off-screen edge via exponential interpolation toward a 0..1 openness target. Mouse proximity to either anchor opens both (strict sync); a force-open target additionally fires when the focused fleet has no presence in any subscribed frame — held for a grace period to absorb cell-boundary hand-offs, logged once on its rising edge. The right panel gates on a focused player existing in the current snapshot, so when none exists only the left panel can force-open. Server-confirmed actions disable via `NetworkUiControl` (and `game::Game`'s weapon-mode / nav-delay controls) while pending; the nav-delay slider commits only on drag release (`IsItemDeactivatedAfterEdit`), never per drag frame. Focus changes also call `gpClientSession->UpdateDesiredCoords()` so cell subscriptions follow the UI.
- **MainMenuScreen** - Local/remote server selection, settings, quit, language. Auto-starts LAN discovery whenever shown; the Graphics button seeds `engine::gSunAngleOverride` from the live camera sun angle (why GraphicsMenuScreen's Time-of-Day slider is main-menu-only). Compile-time `Pch.h` toggles gate auto-launch/auto-connect.
- **ModalScreen** - Centered modal for connection-rejection and desync messages.
- **PauseMenuScreen** - In-game pause overlay.
- **GraphicsMenuScreen** - Rendering settings bound to engine Wrappers.
- **SoundMenuScreen** - Volume sliders with defaults reset.
- **DeathMenuScreen** - Empty `Render()` stub, placeholder for a future death flow.

## MenuUtils

Shared helpers: `kfMenuUiScale` constant, `ScopedMenuScale`, UTF-32 to UTF-8 conversion, and ImGui-to-`engine::Wrapper` control bindings (distinct from `engine::TweaksScreenBase::WrapperSlider` despite the shared name). The UTF-8 conversion returns a pointer into a popped workbuffer arena — valid only until the next workbuffer write; pass it inline to the consuming ImGui call (which copies), never store it.
