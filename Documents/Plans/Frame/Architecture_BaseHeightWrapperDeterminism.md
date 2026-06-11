# Architecture: gBaseHeight Wrapper Out of the Deterministic Sim

## Context

Source: /external-architecture-review on `Engine/Source/Frame` (non-recursive); reader list completed by repo-wide grep at verification. `engine::gBaseHeight` is a Ui `Wrapper` (`Ui/WrapperBase.h:214`, constructed `Wrapper(6.0f, 0.0f, 20.0f)` at `WrapperBase.cpp:9`) — a type whose purpose is runtime mutation — but it is read inside the deterministic sim (engine nav code plus many game sim call sites; full list below). Today it is bound to no UI surface (the `WrapperBase.h:211` comment and `Ui/CLAUDE.md` both document the `WrapperBase.{h,cpp}` globals as internal-only; the server reads wrappers at defaults), so it is a constant in practice — but the first person to bind it to a Tweaks slider desyncs every client nav decision and spawn position against the server, with no wrapper-parity assert anywhere. A sim-visible value should not be runtime-tunable presentation state.

## Design

### Engine/Source/Ui/WrapperBase.h / WrapperBase.cpp
- Delete the `gBaseHeight` `Wrapper` (declaration `WrapperBase.h:214`, definition `WrapperBase.cpp:9`); replace with `constexpr float kfBaseHeight = 6.0f;` in an engine Frame-layer header (home is the pre-staged grill decision) [~15m]

### All readers — replace `gBaseHeight.Get()` with the constant
Complete list from repo-wide grep (2026-06-10):
- Engine sim/nav: `Frame/NavQuery.cpp:503` (A* waypoint Z), `:583, 603` (`NavQuerySnapToNavigable`/`NavQueryDirection` base-height reads + Z asserts); `Main.cpp:184, 234` (nav-clearance threshold fed to `WaitForElevationMaps`) [~10m]
- Game sim (CRC-fed positions/thresholds): `Frame.cpp:216` (terrain-clearance test), `:249` (grid-cell candidate Z), `:339` (spawn position Z); `Collections/Players/Players.cpp:354` (spawn Z), `:504` (Z clamp), `:710` (frame-center Z); `Collections/Players/PlayersNavigation.cpp:322` (island destination Z), `:439` (push height); `Collections/Spaceships/SpaceshipsNavigation.cpp:59, 63` (island-center / candidate Z) [~15m]
- Presentation (same value, keeps render/audio agreeing with sim): `Graphics/GraphicsUtils.cpp:85`, `Graphics/Render/MainUniforms.cpp:19, 50, 127` (and the comment at `:89`), `Graphics/Render/GlobalUniforms.cpp:619`, `Audio/StaticVoices.cpp:487` (and the comment at `:480`); game `Graphics/Camera.cpp:40, 110, 171`, `Collections/Players/PlayersRender.cpp:247, 257, 263` (debug render) [~15m]

### Engine/Source/Frame/NavQuery.cpp
- After conversion, the `Ui/WrapperBase.h` include (line 4) becomes genuinely unused — remove it. Re-check the other converted TUs for the same (most also use other wrappers or never included WrapperBase.h directly) [~5m]

## Critical files
- `Engine/Source/Ui/WrapperBase.h`, `Engine/Source/Ui/WrapperBase.cpp`
- `Engine/Source/Frame/NavQuery.cpp`, `Engine/Source/Main.cpp`
- `Engine/Source/Graphics/GraphicsUtils.cpp`, `Engine/Source/Graphics/Render/MainUniforms.cpp`, `Engine/Source/Graphics/Render/GlobalUniforms.cpp`, `Engine/Source/Audio/StaticVoices.cpp`
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp`, `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp`, `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp`, `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersRender.cpp`, `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/SpaceshipsNavigation.cpp`
- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp`

## Out of scope
- Changing the 6.0f value (value-preserving conversion — CRC'd sim values are bit-identical before/after)
- Auditing other `Wrapper`s for sim reads (the review found only this one in the non-recursive Frame directory — `NavQuery.cpp` is the sole `WrapperBase.h` consumer there; `Frame/Collections/` render/update files read many wrappers, all client-only presentation paths; a repo-wide wrapper-vs-sim audit is a separate effort if wanted)
- Wrapper-parity asserts between client and server (moot once this is a constant)

## Notes
- **Determinism exposure: removes a latent hazard; introduces none.** The replacement value is identical (`Wrapper` ctor with step 0.0f stores 6.0f unmodified), so no CRC value changes, no `kiVersion` bump, no replay reset.
- Pre-staged grill decision: (a) home for `kfBaseHeight` (suggest `Frame/FrameUtils.h` or wherever sim-wide constants live — it must be visible to engine Audio/Graphics and game Frame code, so an engine header below `Engine.h`); (b) confirm no future runtime-tuning intent — if tuning is genuinely wanted later, the value must become server-authoritative replicated state, not a Ui wrapper.

## Verification Notes (2026-06-10)
- Verified `Wrapper gBaseHeight(6.0f, 0.0f, 20.0f)` at `WrapperBase.cpp:9` and the declaration at `WrapperBase.h:214`, under the header's own "Internal-only wrappers (not bound to any UI: not Tweaks, not GraphicsMenuScreen, not SoundMenuScreen)" comment (`WrapperBase.h:211`). `Ui/CLAUDE.md` confirms the server reads wrappers at defaults and no UI mutates them there. Bound-to-no-UI claim holds.
- Verified the `Wrapper` float ctor (`WrapperBase.h:16-27`): with `fStep = 0.0f`, `Snap` is identity, so `Get()` returns exactly 6.0f — the constant conversion is bit-identical.
- **Determinism reasoning held up**: every sim read site returns the same 6.0f on client and server today; converting to `constexpr` changes no computed value and closes the bind-a-slider desync hazard.
- **Rewrite reason**: the original Design listed only 7 reader files; repo-wide grep found ~14 additional `gBaseHeight.Get()` sites, including game sim reads the plan missed entirely (`Frame.cpp:249/339`, `Players.cpp:354/504/710`, `PlayersNavigation.cpp:322`, `SpaceshipsNavigation.cpp:59/63`) and presentation reads (`Camera.cpp:40/110/171`, `PlayersRender.cpp:247/257/263`). Executing the old list would have left the build broken (deleted global with live readers). Reader list above is now complete as of verification date.
