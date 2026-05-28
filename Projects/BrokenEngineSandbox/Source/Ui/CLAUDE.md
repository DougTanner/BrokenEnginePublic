# `/Projects/BrokenEngineSandbox/Source/Ui/` - Game UI

Game-side ImGui HUD, menus, localization, and `game::`-scoped `engine::Wrapper` globals. Client-only. Extends [engine UI](../../../../Engine/Source/Ui/CLAUDE.md) — Wrapper invariants and tweaks-layout-ordering coupling are documented there and not repeated.

## Game-specific notes

- **Localization**: UTF-32 table (`Localization.h`), six languages. Per-string buffer is a hard **256-char cap**. `TranslatedString()` falls back to English when the selected translation is empty — a genuine game-layer behavior not present in the engine. UTF-8 encoding for menu screens lives in the [Screens](Screens/CLAUDE.md) child.
- **Wrapper grouping**: Game wrappers are split per Tweaks tab into `HexShieldWrappers`, `LightingWrappers`, `ParticleWrappers`, `SmokeWrappers`, `SoundWrappers`, `WindDepositsWrappers` pairs. Each pair's declaration order must mirror its matching `Screens/TweaksScreen/TweaksScreen*.cpp` tab. The wrappers whose pointers are read by server-compiled frame collection / type-registration code (`ParticleWrappers`, `HexShieldWrappers`, `WindDepositsWrappers`) are in **both** vcxprojs; the rest are client-only by file inclusion. So any frame code reading a game-side wrapper must either stay on a client-only path or have its wrapper file added to the server vcxproj.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md)
