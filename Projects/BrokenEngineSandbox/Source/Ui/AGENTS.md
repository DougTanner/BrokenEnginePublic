# Game UI - Localization and Wrapper Storage

Game-owned localization and wrapper storage. Engine wrapper semantics are defined by the Engine UI hub (`../../../../Engine/Source/Ui/AGENTS.md`); ImGui screens live in Screens (`Screens/AGENTS.md`).

## Localization

- Localization is a shared, header-only UTF-32 table initialized in the `Game` constructor on both builds. Missing translations fall back to English, and a sentinel plus compile-time extent check prevents shifted rows.
- Initialization uppercases the table in place. Language selection is client runtime state, chosen on the Game Settings screen and persisted with the UI font scale in `GameSettings.bin`; a file whose stored index falls outside the Language enum falls back to English.
- Players see the audio menu labeled AUDIO, while its screen, UI state, wrappers, and settings file keep the internal Sound name. The split is deliberate: renaming the internals would rewrite a persisted filename for no player-visible gain.
- UTF-8 conversion and workbuffer lifetime belong to Screens (`Screens/AGENTS.md`).

## Wrapper Affinity

- Wrappers read by shared Frame code compile into both client and server projects even when only the client UI changes them.
- Rendering-only settings and their consumers remain whole-file `BT_CLIENT`-guarded and client-project-only.
- A new Frame dependency on a game wrapper must either stay on a client-only path or make that wrapper available to the server build.
- Game wrapper declaration order follows the matching `../../../../Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md` slider registration order.
