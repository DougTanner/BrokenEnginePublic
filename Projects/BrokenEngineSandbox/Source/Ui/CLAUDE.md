# `/Projects/BrokenEngineSandbox/Source/Ui/` - Game UI

Game-side ImGui HUD, menus, localization, and `game::`-scoped `engine::Wrapper` globals. Client-only. Extends [engine UI](../../../../Engine/Source/Ui/CLAUDE.md) — Wrapper invariants and tweaks-layout-ordering coupling are documented there and not repeated.

## Game-specific notes

- **Localization**: UTF-32 table, six languages. Per-string buffer is a hard **256-char cap**. `TranslatedString()` falls back to English when the selected translation is empty — a genuine game-layer behavior not present in the engine. Menu screens encode to UTF-8 via workbuffer in `MenuUtils`.
- **Wrapper grouping**: Game wrappers are split per Tweaks tab into `HexShieldWrappers`, `LightingWrappers`, `ParticleWrappers`, `SmokeWrappers`, `SoundWrappers`, `WindDepositsWrappers` pairs. Each pair's declaration order must mirror its matching `Screens/TweaksScreen/TweaksScreen*.cpp` tab. `ParticleWrappers` is in **both** vcxprojs (per-explosion-type pointers are referenced unconditionally at type-registration sites in `Missiles.cpp`/`Players.cpp`/`Spaceships.cpp`); the others are client-only by file inclusion. Frame collection code that reads game-side wrappers must therefore live in client-only paths or the wrapper file must be added to the server vcxproj.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md)
