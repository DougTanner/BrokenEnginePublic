# `/Projects/BrokenEngineSandbox/Source/Ui/Screens/` - Game UI Screens

ImGui screens rendered by engine's `ImGuiManager`. The `.cpp` bodies are fully `#if defined(BT_CLIENT)`-wrapped and client-vcxproj-only, except `DeathMenuScreen` and `MenuUtils`, which are unguarded and compile in both vcxprojs. Headers stay unguarded so they parse in any TU (`HudScreen.h` guards its members; `TweaksScreen.h` is fully guarded — its base exists only on the client).

## Shared Patterns

Children do not re-document these:

- **Menu scale**: player screens use `ScopedMenuFont`; `ScopedMenuScale` uniformly scales geometry. TweaksScreen's engine base instead pushes its denser debug font directly inside each window.
- **Rendered output is authoritative**: judge target-resolution screenshots for optical balance, whitespace, hierarchy, and perceived weight. ImGui bounds and text metrics are diagnostics; centering a transparent window does not prove its visible content is centered.
- **Visibility**: screens early-return unless their `gpGame->meUiState`/game flags are active and transition by writing `meUiState` in handlers (MainMenu/Pause partition `kPause` via `InMainMenu()`). Self-gating remains necessary because ImGuiManager renders MainMenu and Modal unconditionally while outer-gating in-game screens.
- **Positioning / UI sizing standard** (three rules, ImGui best practice; the full layout contract — placement, size-to-content, primitives, rhythm — is [Documents/UserInterfaceDesign.txt](../../../../../Documents/UserInterfaceDesign.txt), the source of truth):
	1. *Global scale* — one uniform factor `engine::UiScale()` (defined by `ImGuiManager`, 2160-high reference) applied everywhere: fonts via `FontScaleDpi`, style paddings/spacings via theme geometry; the user Font Size preference stacks on top as `gUiFontScale`.
	2. *Content-independent dimensions* — proportional `DisplaySize` fractions (anchors, panel heights) or 4K-authored pixel constants (radii, offsets) multiplied by `engine::UiScale()` at each use site.
	3. *Text-driven dimensions* — measure representative content with `ImGui::CalcTextSize` under the pushed font, then add style padding (`PauseMenuScreen`/`HudScreen::PanelWidth`). Never magnify a measured size; doing so amplifies rounding inconsistency.
- **Opaque-UI occlusion**: opaque-background screens register their rect via `gpImGuiManager->RegisterOpaqueRect()` unconditionally (`gOpaqueUi` gating lives in ImGuiManager); transparent-window screens (MainMenu, Pause) don't register.
- **Networked controls**: disable via `NetworkUiControl` while awaiting server confirmation.
- **Localization**: UTF-32 table converted to UTF-8 via workbuffer helpers in `MenuUtils`. The default Noto Sans SC Light font covers Latin and CJK, so `ScopedMenuFont` only applies dynamic sizing and every language uses the same font path.
- **Wrapper binding**: settings controls (checkbox, float slider, step buttons) bound to `engine::Wrapper` globals via helpers in `MenuUtils`.

## Screens

- **TweaksScreen** - Game override of `engine::TweaksScreenBase` (mechanics, slider-map registrar convention, and subtab/column rules documented at the [engine hub](../../../../../Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md), not repeated here). `Render()` early-returns under `kbDebugInput` unless `mbShowImGui`. Fills the base's game extension hooks: the two pure-virtual sections (Hex Shield; Particles — Missile/Player/Spaceship subtabs) and the default-empty tab hooks hosted inside engine sections (Smoke Deposits in Smoke, Wind Deposits in Wind, Visible + Lighting tabs in Lighting Effects, Sound Effects in Sound). One section/hook per file.
- **HudScreen** - Left fleet controls and right focused-player weapon control slide in strict sync; the weapon toggle is centered in the right panel's live content. The left height hugs its member list up to `kfHudPanelMaxHeightFraction`; the right matches that live size for symmetry. A fixed-height edge strip opens both regardless of live height, while a grace-delayed force-open covers focused fleets absent from subscribed frames. Network-confirmed actions disable while pending; nav delay commits on drag release, and focus changes refresh desired cell subscriptions.
- **MainMenuScreen** - Panel-free server/settings/quit actions share one visible keyline around the first-third x anchor and optical vertical midpoint; language is a reduced bottom-center footer. LAN discovery starts whenever shown. Graphics seeds `engine::gSunAngleOverride` from the live camera, making Time of Day main-menu-only; `Pch.h` toggles gate auto-launch/connect.
- **ModalScreen** - Compact, centered connection-rejection/desync surface with a screen-local wrap width, restrained message/action gap, and centered OK action.
- **PauseMenuScreen** - Panel-free in-game pause overlay: a full-screen dim supports a centered restrained heading and uniform-width primary actions.
- **GraphicsMenuScreen** - Rendering settings bound to engine Wrappers, laid out in a responsive-width two-column `BeginTable` (`SizingStretchSame`, display/filtering settings left, Effects & UI right, with a semantic gutter — legacy `ImGui::Columns` is forbidden) capped at `kfGraphicsMaxHeightFraction`. A screen-local density curve preserves monotonic Font Size behavior while keeping every control visible through `3.0`; title, FPS, and Back share one header row.
- **SoundMenuScreen** - Centered volume controls with a restrained heading, one screen-local slider width, and defaults reset.
- **DeathMenuScreen** - Empty `Render()` stub, placeholder for a future death flow.

## MenuUtils

Shared helpers: `kfMenuUiScale`/heading-scale constants (including the restrained MainMenu/Pause/Sound title scale and MainMenu's language-footer scales), a named shared-layout block (anchors/extents as `DisplaySize` fractions and 4K-authored pixel constants multiplied by `engine::UiScale()` at use; screen-specific values stay local and named), `ScopedMenuScale`, UTF-32 to UTF-8 conversion, and ImGui-to-`engine::Wrapper` control bindings (distinct from `engine::TweaksScreenBase::WrapperSlider` despite the shared name).

`MenuButtonsWidth` measures the widest localized label under the active font, adds padding, and lets callers apply a `k*MinWidthPixels * UiScale()` floor; call it inside the window with the menu font pushed. `MenuHeading` emits a scaled title plus the standard gap. UTF-8 conversion returns a move-only workbuffer allocation valid through the consuming full-expression; do not store it beyond that expression.

Client-only menu chrome (full-screen dim, panel accents, `MenuButton`) draws via ImDrawList from a per-`engine::UiTheme` table static_asserted against the theme count. Rules the signatures don't show:

- `ScopedMenuFont` (RAII dynamic-font push: default Latin/CJK font sized off `FontSizeBase`) must be constructed before `Begin()` or fully inside the window — a font still pushed at `End()` trips ImGui's per-window error recovery, then the dtor double-pops.
- Opaque HUD and settings panels use `DrawPanelAccents` (border + accent strip only) so their WindowBg stays intact for `RegisterOpaqueRect` occlusion; panel-free Pause keeps its transparent window unregistered.
- `MenuButton` hover-anim state is a caller-owned float (screen member — no heap); its colors compose `style.Alpha` so `BeginDisabled` dims it.
