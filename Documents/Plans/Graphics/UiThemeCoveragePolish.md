# UI Theme Coverage Polish

## Context

This session added a 3-theme UI system spanning engine chrome and game chrome:

- Engine: the `engine::UiTheme` enum plus `ImGuiManager::ApplyThemeColors(UiTheme eTheme)` (`Engine/Source/Graphics/Managers/ImGuiManager.cpp`, ~line 194), which derives the full `ImGuiStyle::Colors[]` set from an 8-hue `ThemePalette` row (`f4Text`, `f4TextDisabled`, `f4Bg`, `f4BgElevated`, `f4Accent`, `f4AccentHover`, `f4AccentActive`, `f4Border`) in the `kThemePalettes[]` table (~line 40; one row each for `kNavalSteel`/`kDarkAmber`/`kMonochrome`).
- Game: chrome helpers `DrawPanelBackground`, `DrawPanelAccents`, `MenuButton` and the `kMenuChromes` table in `Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.cpp`/`.h`.

Two audit findings on that new system are out of scope for the facelift itself and collected here. Both are pure appearance polish — no behavioral or functional change.

## Design

### Finding A — extend `ApplyThemeColors` palette coverage (engine)

`ImGuiManager::ApplyThemeColors` explicitly maps ~45 `ImGuiCol_` indices from the palette but leaves several at ImGui's built-in dark-style defaults, so they do not track the active theme. The uncovered indices:

- `ImGuiCol_TextLink`
- the `ImGuiCol_Table*` family (`TableHeaderBg`, `TableBorderStrong`, `TableBorderLight`, `TableRowBg`, `TableRowBgAlt`)
- `ImGuiCol_DragDropTarget`
- `ImGuiCol_NavWindowingHighlight`, `ImGuiCol_NavWindowingDimBg`
- `ImGuiCol_ModalWindowDimBg`
- `ImGuiCol_TabDimmedSelectedOverline`

Most read acceptably by coincidence, but `TextLink` and `DragDropTarget` render as ImGui's default blue — visibly off-theme under Dark Amber and Monochrome. Extend the mapping to derive all uncovered indices from the existing 8 `ThemePalette` hues (mirror the established `WithAlpha(...)` derivation pattern already used for the accent/border-tinted entries): link/drag-target from `f4Accent`; the table family from `f4Bg`/`f4BgElevated`/`f4Border`; the nav/modal dim overlays as a low-alpha `f4Bg`; `TabDimmedSelectedOverline` from a muted `f4Accent`. No new palette fields — reuse the eight existing hues.

### Finding B — GraphicsMenuScreen / SoundMenuScreen chrome consistency (game, cosmetic decision)

`GraphicsMenuScreen` and `SoundMenuScreen` each register an opaque rect for depth-prepass occlusion (`engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize())` — `GraphicsMenuScreen.cpp` ~line 30, `SoundMenuScreen.cpp` ~line 28) exactly like the HUD, but draw neither `DrawPanelAccents` nor `DrawPanelBackground`, so they read as stock themed windows rather than the new chrome language. `HudScreen.cpp` (~lines 208-213, 401-406) is the reference treatment: `RegisterOpaqueRect` followed by `DrawPanelAccents(ImGui::GetWindowDrawList(), vPanelMin, vPanelMax)` — border + accent strip only, leaving the opaque themed `WindowBg` intact for the occlusion rect.

Decide one of:

- **Apply the HUD treatment**: add a single `DrawPanelAccents(...)` call to each screen after its `RegisterOpaqueRect`, matching the `HudScreen.cpp` pattern (one line each; do not add `DrawPanelBackground` — the opaque themed `WindowBg` must stay intact for the occlusion rect, per the HUD comment).
- **Document as intentionally stock**: if the two config screens are meant to stay plain, add a short comment at each `RegisterOpaqueRect` site noting the deliberate omission and close the finding.

Lean toward applying `DrawPanelAccents` for visual consistency unless the user prefers the config screens stay stock; surface the call at grill.

## Critical files

- `Engine/Source/Graphics/Managers/ImGuiManager.cpp` — `ImGuiManager::ApplyThemeColors` (~194) and the `kThemePalettes[]`/`ThemePalette` table (~27-76). Finding A extends the `pColors[...]` assignment block.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/GraphicsMenuScreen.cpp` — `RegisterOpaqueRect` site (~30); Finding B adds `DrawPanelAccents` or a stock-intent comment.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/SoundMenuScreen.cpp` — `RegisterOpaqueRect` site (~28); same as above.
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MenuUtils.h`/`.cpp` — `DrawPanelAccents` declaration/definition (reference for Finding B; not modified).
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/HudScreen.cpp` — the reference `RegisterOpaqueRect` + `DrawPanelAccents` pattern (Finding B model; not modified).

## Out of scope

- Adding new `ThemePalette` hue fields — Finding A reuses the existing eight.
- Retuning any already-mapped `ImGuiCol_` value or the palette hues themselves — coverage only, not color tuning.
- Restructuring the theme selection / `GetUiTheme()` / persistence path or `MenuChrome` / `kMenuChromes` mapping.
- Any other menu screen's chrome (PauseMenu, MainMenu, Modal) — those already carry the intended treatment this session.

## Notes

- Client/graphics-only appearance polish. No determinism/CRC/`kiVersion`/`.pack`/wire/allocation-tracked-path exposure; no shader repack.
- Finding A is a mechanical mapping extension (compile-checked). Finding B is a one-line cosmetic addition or a comment — the only open decision is the apply-vs-stock call, pre-staged for grill.
- Both findings are independent; either can land without the other.
