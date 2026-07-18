# `/Projects/BrokenEngineSandbox/Source/Ui/` - Game UI

Game-side localization and `game::`-scoped `engine::Wrapper` globals; ImGui screens live in [Screens](Screens/AGENTS.md). Wrapper class invariants and tab slider-ordering rules are documented at the [engine UI hub](../../../../Engine/Source/Ui/AGENTS.md) and not repeated here.

## Localization

- `Localization.h` is header-only (`inline` table and functions): UTF-32 strings, six languages, hard 256-char cap per string. The table carries a trailing all-empty sentinel row past the last string; a `static_assert` ties its outer extent to the string count so a dropped row is a compile error rather than a silent shift of every later string's translations. `TranslatedString()` falls back to English when the selected translation is empty. UTF-8 conversion for ImGui lives in [Screens](Screens/AGENTS.md).
- `InitializeLocalization()` runs in the `Game` constructor on both builds and uppercases the whole table in place — the mixed-case literals are authoring-only; everything renders uppercase.
- The live language selection (`geLanguage`) is not persisted via `ClientSettings`; it resets to English every launch.

## Wrappers

- One pair per Tweaks tab: `HexShieldWrappers`, `LightingWrappers`, `ParticleWrappers`, `SmokeWrappers`, `SoundWrappers`, `WindDepositsWrappers`. Exception: `LightingWrappers` feeds two Lighting Effects tabs (Visible + Lighting); its `.cpp` is sectioned per tab while the header interleaves per effect.
- `Pch.h` includes client-only `HexShieldWrappers.h` and shared `WindDepositsWrappers.h` before `Frame.h`/`Engine.h`, so client engine/frame code sees both sets of externs while server code sees only the shared set; consumers of the other four pairs include them explicitly.
- **Client/server linkage**: `ParticleWrappers` and `WindDepositsWrappers` are in both vcxprojs because unguarded server-compiled frame code references their symbols (explosion-type registration, blaster spawn info) — left unwrapped by design. `HexShieldWrappers`/`LightingWrappers`/`SmokeWrappers`/`SoundWrappers` are whole-file `#if defined(BT_CLIENT)`-wrapped and client-vcxproj-only. `Pch.h` includes `HexShieldWrappers.h` only under `BT_CLIENT`; its consumers are client-only `MainUniforms.cpp` and `TweaksScreenHexShield.cpp`. Every frame-code include of the other client-only wrappers already sits inside its own `#if defined(BT_CLIENT)` block. Frame code reading a game wrapper must stay on a client-only path or have its wrapper file added to the server vcxproj.

## See Also

- `Screens/AGENTS.md` - Menus, HUD, TweaksScreen tabs
