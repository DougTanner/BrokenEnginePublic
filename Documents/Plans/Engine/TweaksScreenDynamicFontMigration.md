# TweaksScreen Dynamic-Font Migration

## Context

`engine::TweaksScreenBase::Render` (`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp`) scales its dev-UI text with the obsolete `ImGui::SetWindowFontScale(kfUiScale)` call at two sites — the toggle-bar window (line ~251, right after the `"Tweaks"` `ImGui::Begin`) and the per-section window (line ~357, right after the `kpcSectionNames[iSection]` `ImGui::Begin`). `kfUiScale` is a `1.5f` file-scope `constexpr` (line ~47).

ImGui 1.92 obsoleted `SetWindowFontScale`; the sanctioned replacement is the dynamic-font API — `ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale)` with a matching `ImGui::PopFont()`. The push must land before `Begin()` or fully inside the window body: a font still pushed at `End()` trips ImGui's per-window error-recovery force-pop.

The game player screens already migrated this session to the dynamic-font API via the `game::ScopedMenuFont` RAII helper (`Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.h`/`.cpp`, ctor `explicit ScopedMenuFont(float fScale = kfMenuUiScale)`). That helper is game-layer and language/font-atlas-aware, so `TweaksScreenBase` — engine-layer dev UI — cannot reuse it. The two call sites still compile and render correctly today because the obsolete API remains in the vendored ImGui; the risk is purely future: an upstream ImGui bump on a vendored update that drops `SetWindowFontScale` breaks the build.

`Projects/.../Ui/Screens/CLAUDE.md` currently documents the split as "TweaksScreen alone scales via the engine base's `SetWindowFontScale(kfUiScale)`" — that line describes the state this plan removes and must be updated.

## Design

Replace both `SetWindowFontScale(kfUiScale)` calls with the dynamic-font push/pop pair, scaling `ImGui::GetStyle().FontSizeBase` by `kfUiScale`. Two equivalent shapes — pick the simpler at implementation:

- **Direct `PushFont`/`PopFont` pairs** at each `Begin`…`End` span. Push `ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kfUiScale)` immediately after each `ImGui::Begin` (the font is then wholly inside the window body, avoiding the `End()`-time force-pop) and pop before the matching `ImGui::End`. Two sites, symmetric.
- **A plain engine-local RAII** (an engine-layer analogue of `ScopedMenuFont` with no language/atlas awareness) if the two sites share enough setup to be worth a helper. Keep it engine-scope — do not depend on any `game::` symbol.

Prefer the direct pairs unless the RAII removes real duplication; the two spans are short.

`kfUiScale` stays as-is (still multiplies padding/spacing style vars at lines ~204-207). Only the font-scale mechanism changes.

## Critical files

- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp` — the two `SetWindowFontScale(kfUiScale)` calls in `TweaksScreenBase::Render` (toggle-bar `Begin`, ~251; per-section `Begin`, ~357) and `kfUiScale` (~47). Every `Begin` in this body has a matching `End` in the same function — pop before each.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md` — "Menu scale" bullet naming TweaksScreen's `SetWindowFontScale(kfUiScale)`; update to the dynamic-font mechanism once migrated.

## Out of scope

- The `game::ScopedMenuFont` helper and the player-screen font path (already migrated this session — do not re-touch).
- The `PushStyleVar` padding/spacing scaling at lines ~204-207 and the `kfUiScale` constant value — only the font mechanism moves.
- Any broader ImGui vendored-version bump or other obsolete-API sweep outside the two font-scale sites.
- `Engine/Source/Profile/` ImPlot graph rendering — swept this session, no `SetWindowFontScale`/legacy `PushFont` uses found; nothing to fold in.

## Notes

- Client/graphics-only, dev-UI (`BT_CLIENT`-wrapped file, further gated by `if constexpr (kbDebugInput)`). No determinism/CRC/`kiVersion`/`.pack`/wire/allocation-tracked-path exposure.
- Works today; the trigger is defensive — obsolete-API removal on a future vendored ImGui update. Not urgent, hence the low priority.
- No open design decision to pre-stage: direct pairs vs a trivial engine-local RAII is a trivial equivalent-approaches choice (pick simplest at implementation), not an architectural one.
