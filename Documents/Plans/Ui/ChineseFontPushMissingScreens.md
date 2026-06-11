# Chinese Font Push Missing in Pause / Sound Menu Screens

## Context

The localized menu screens render translated strings via `ImGui::Button(AppendUtf8(rWorkbuffer,
TranslatedString(kString...)))`. For CJK locales the glyphs only render if the Chinese font is pushed, because
`Engine/Source/Graphics/Managers/ImGuiManager.cpp` loads the EFIGS default font and the Chinese font as **two
separate atlases** (`AddFontFromMemoryTTF` for `kRawRobotoMediumttfCrc` then for `kRawNotoSansSCLightotfCrc`,
captured as `mpChineseFont`) — there is **no glyph merge** (`ImFontConfig::MergeMode` is not set), so the default
EFIGS font has no CJK glyphs.

`Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md` documents the pattern as general: "Chinese font
(`gpImGuiManager->mpChineseFont`) is pushed around text when the selected language requires it." But only
`MainMenuScreen.cpp` actually pushes it (`ImGui::PushFont(engine::gpImGuiManager->mpChineseFont)` at two sites):

- **`PauseMenuScreen.cpp`** renders five translated buttons — `TranslatedString(kStringResume / kStringGraphics /
  kStringSound / kStringMainMenu / kStringQuit)` (both the `CalcTextSize` width-measure loop and the buttons) —
  with **no** font push.
- **`SoundMenuScreen.cpp`** renders `TranslatedString(kStringDefaults)` (its "Back" button is the literal ASCII
  `"Back"`, which is fine) — with **no** font push.

So under a Chinese locale, the Pause and Sound menus render their translated strings in the EFIGS-only default
font → missing/box glyphs (and `CalcTextSize` mis-measures the button widths in `PauseMenuScreen`). Either those
two screens are **missing the font push** (a Chinese-locale rendering bug) or the documented pattern is
overstated / those screens are intentionally exempt.

This plan resolves which: confirm whether Pause/Sound are reachable while a CJK language is selected (they are —
the Sound and Pause menus are language-independent UI), then either add the missing `PushFont`/`PopFont` around
the translated text (matching `MainMenuScreen`) or narrow the doc pattern if there is a real reason these screens
skip it.

## Design

1. **Confirm the locale reachability.** The language selector and the Pause/Sound menus are all reachable in any
   locale; a CJK language selected in the Sound/Graphics menu persists into the Pause menu. So both screens *can*
   render CJK translated strings. (Verify the translation tables actually carry non-empty Chinese values for the
   `kString*` IDs these screens use — they do for the menu strings used by MainMenuScreen, which overlap.)
2. **Mirror `MainMenuScreen`'s push pattern.** `MainMenuScreen.cpp` pushes `mpChineseFont` around its translated
   text only when the selected language requires it (gated on the locale, per the doc). Apply the same gated
   push/pop in `PauseMenuScreen.cpp` (around the width-measure loop **and** the button render block — the
   `CalcTextSize` must measure under the same font that renders, or the button widths are wrong) and in
   `SoundMenuScreen.cpp` (around the `kStringDefaults` button). Use whatever locale predicate `MainMenuScreen`
   uses so the three screens stay consistent.
3. **Audit the other localized screens for the same gap** while here (read-only): `DeathMenuScreen`,
   `GraphicsMenuScreen`, `ModalScreen`, `HudScreen` — any that render `TranslatedString(...)` without a CJK font
   push have the same bug. Fix the same way or note exemptions. (Keep this within the "translated-text screens
   missing the push" scope — do not restyle.)
4. **If a screen is intentionally English-only** (none obvious), narrow the `Screens/CLAUDE.md` pattern wording
   instead of forcing a push.

## Out of scope

- **Switching to a merged glyph atlas** (loading the Chinese font with `MergeMode = true` into the default font
  so no per-screen push is ever needed) — that is a larger ImGuiManager change with atlas-size/memory
  implications; this plan fixes the per-screen push to match the established pattern, not the font-loading
  architecture. (If the user prefers the merge approach, that is a separate Engine ImGuiManager plan.)
- **The `MainMenuScreen` push** — already correct; unchanged (it is the template).
- **Non-translated literal UI** (e.g. SoundMenuScreen's ASCII `"Back"`, debug overlays, the profiler text) —
  EFIGS-only by design; no push needed.
- **The localization tables / `TranslatedString` / `AppendUtf8` machinery** — unchanged; this is purely the
  missing font scoping around existing translated calls. (Note `AppendUtf8`'s dangling-pointer lifetime is the
  separate `Ui/MenuUtilsAppendUtf8DanglingPointer.md` plan — do not fold it in here; if both land in one session,
  coordinate the PauseMenuScreen edits.)

## Acceptance criteria

- Pause and Sound menu translated strings render with CJK glyphs under a Chinese locale (font pushed around the
  text), matching `MainMenuScreen`; `PauseMenuScreen`'s `CalcTextSize` width loop measures under the same font it
  renders (button widths correct in CJK).
- Any other `TranslatedString`-rendering screen found with the same gap is fixed or documented as exempt.
- The `Screens/CLAUDE.md` Chinese-font pattern statement matches what every localized screen actually does.
- Client-only; no determinism/CRC/network exposure.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Ui/Screens/PauseMenuScreen.cpp` (translated-button width loop + render —
  add gated push/pop).
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/SoundMenuScreen.cpp` (`kStringDefaults` button — add gated
  push/pop).
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/MainMenuScreen.cpp` — read-only template (the gated
  `PushFont(mpChineseFont)` pattern).
- Read-only context: `Engine/Source/Graphics/Managers/ImGuiManager.cpp` (`:64-74`, the two-atlas no-merge font
  load), `Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md` (the documented pattern),
  `Projects/BrokenEngineSandbox/Source/Ui/Localization.*` (locale predicate / `TranslatedString`).
- Audit-only: `DeathMenuScreen.cpp`, `GraphicsMenuScreen.cpp`, `ModalScreen.cpp`, `HudScreen.cpp`.

All edited files are client-only menu screens (the `Ui/Screens` TUs compile client-only).

## Notes

- Client-only UI; no CRC/determinism/network exposure (Risks ~1 — a visual fix, playtest-verifiable by selecting
  Chinese and opening Pause/Sound).
- One grill decision: confirm the locale-gate predicate to reuse from `MainMenuScreen` (so the three screens push
  identically), and whether to also fix any sibling screens found in the audit in the same pass.
- Mechanical once the predicate is settled — mirror an existing, working pattern.
