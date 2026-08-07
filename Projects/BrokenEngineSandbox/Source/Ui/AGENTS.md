# Game UI - Localization and Wrapper Storage

Game-owned localization and wrapper storage. Engine wrapper semantics are defined by the Engine UI hub (`../../../../Engine/Source/Ui/AGENTS.md`); ImGui screens live in Screens (`Screens/AGENTS.md`).

## Localization

- Localization is a shared, header-only UTF-32 table initialized in the `Game` constructor on both builds. Missing translations fall back to English, and a sentinel plus compile-time extent check prevents shifted rows.
- Initialization uppercases the table in place. Language selection is client runtime state, chosen on the Game Settings screen and persisted with the UI font scale, opaque-UI toggle, UI opacity, and theme in `GameSettings.bin`; a file whose stored index falls outside the Language enum falls back to English.
- Players see the audio menu labeled AUDIO, while its screen, UI state, wrappers, and settings file keep the internal Sound name. The split is deliberate: renaming the internals would rewrite a persisted filename for no player-visible gain.
- UTF-8 conversion and workbuffer lifetime belong to Screens (`Screens/AGENTS.md`).

## Wrapper Affinity

- Wrappers read by shared Frame code compile into both client and server projects even when only the client UI changes them.
- Rendering-only settings and their consumers remain whole-file `BT_CLIENT`-guarded and client-project-only.
- A new Frame dependency on a game wrapper must either stay on a client-only path or make that wrapper available to the server build.
- Game wrapper storage that mirrors engine Tweaks sliders keeps its declaration order matching the `../../../../Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md` slider registration order. Wrapper storage with no Tweaks counterpart, such as the player-facing quality levels, has no imposed order and keeps a stable local order of its own.

## Graphics Quality Levels

- The Graphics menu's player-facing quality levels are client-only game wrappers, each holding one discrete level. A level is the persisted source of truth and the engine rendering wrappers it drives are derived, so those engine values are never written to the settings file and a saved file cannot hold a level that contradicts them.
- A level writes its engine wrappers through `Set` on the change that selects it, and once at startup for both a successful and a failed settings load, so a fresh install renders what its default level claims. `Graphics::Refresh` remains the single consumer polling those engine wrappers; the apply path must not call `Changed<T>()`.
- A level read back from the settings file is opaque input, so clamp it into range before it selects the values it applies.
