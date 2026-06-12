# Architecture: Render Include Hygiene

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Render` (non-recursive). The directory is
`ExternalHeaders.h`-compliant (zero direct `<...>` includes) with no include cycles, but carries dead forward
declarations, an ill-formed forward declaration of a type that no longer exists, six strictly unused includes,
three `Game.h` includes whose only used symbols actually come from the game `Graphics/Camera.h`, and two
used-but-transitive dependencies worth making direct. Same defect classes as `Profile/Architecture_IncludeHygiene.md`.

## Design

### Engine/Source/Graphics/Render/Render.h
- Remove the dead forward declarations `game::Frame` (`:8`) and `game::FrameInput` (`:9`) — only
  `game::FrameInterpolate` (`:10`) is referenced in this header (the `RenderFrameMain` signature, `:20`). [~2m]
- Remove `enum CpuCounters;` (`:17`) — the type exists nowhere in the repo (the real enums are
  `EngineCpuCounters`, `Engine/Source/Profile/ProfileManagerBase.h:46`, and game `GameCpuCounters`,
  `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.h:6`). It is also an unscoped-enum forward
  declaration without a fixed underlying type — ill-formed C++ that MSVC tolerates as an extension. [~2m]

### Strictly unused includes (zero referenced symbols — remove)
- `GlobalUniforms.cpp:9` — `Ui/SmokeWrappersBase.h` (no `gSmoke*` reference in the TU). [~1m]
- `GlobalUniforms.cpp:13` — `Ui/WindWrappersBase.h` (no `gWind*` reference). [~1m]
- `MainUniforms.cpp:7` — `Ui/LightingWrappersBase.h` (no `gLighting*`/`gCombine*`/`gSpread*` reference). [~1m]
- `MainUniforms.cpp:8` — `Ui/PbrWrappersBase.h` (no `gPbr*` reference). [~1m]
- `MainUniforms.cpp:9` — `Ui/SunMoonWrappersBase.h` (no `gSunMoon*` reference). [~1m]
- `WindUniforms.cpp:5` — `Game.h` (zero `game::` symbols anywhere in the TU). [~1m]

### Wrong-provider `Game.h` includes (replace with `#include "Graphics/Camera.h"`)
The only game symbols these TUs use are `game::gpCamera` / `game::Camera` constants, declared in the game
`Graphics/Camera.h` (`gpCamera` at `Camera.h:79`); `Game.h` pulls `ClientSettings.h`, `Fleet.h`,
`FleetSelection.h`, and `Network/Client/ClientSession.h` for nothing (`gpCamera` actually arrives today via the
PCH chain `Pch.h` → `Engine.h:96` → `GameBase.h:6` → `Graphics/Camera.h` — `Game.h` never provided it). Precedent:
`Profile/Architecture_IncludeHygiene.md`; the engine already includes this exact game header directly
(`GameBase.h:6`, `Graphics/CameraBase.cpp:5`), and no `Engine/Source/Graphics/Camera.h` exists to shadow the
resolution.
- `GlobalUniforms.cpp:5` (`gpCamera` reads + `kfShadowHeadroomMultiplier`/`kfLightingHeadroomMultiplier`/`kfCameraEyeHeightDefault`). [~3m]
- `LightingUniforms.cpp:5` (`gpCamera` at `:71`, `:118`; `kfCameraEyeHeightDefault` at `:117`). [~3m]
- `SmokeUniforms.cpp:5` (`gpCamera->f4RenderVisibleArea` at `:43`). [~3m]
- `MainUniforms.cpp:5` keeps `Game.h` — `game::gpGame->mCoordFrames` / `RenderFrame()` need the complete `game::Game`.

### Used-but-transitive dependencies (make direct)
- Add `#include "Ui/WrapperBase.h"` to `GlobalUniforms.cpp` (`gFov` `:248`/`:362`, `gTerrainEarlyOut` `:431`,
  `gBaseHeight` `:619`, `gDebugTextureIndex` `:637-638`) and `MainUniforms.cpp` (`gBaseHeight` `:19`/`:50`/`:127`) —
  these symbols are declared in `Ui/WrapperBase.h` (`gFov` `:212`, `gBaseHeight` `:214`, `gTerrainEarlyOut` `:219`,
  `gDebugTextureIndex` `:230`) and currently arrive only
  because every `*WrappersBase.h` happens to include it. Load-bearing for `MainUniforms.cpp` once its three unused
  wrapper includes are removed (sole remaining provider would be the incidental `WaterWrappersBase.h`). [~3m]
- Add `#include "Ui/HeightLerpWrapperQuartet.h"` to `GlobalUniforms.cpp` and `LightingUniforms.cpp` — free
  `engine::LerpAtHeight` (`HeightLerpWrapperQuartet.h:21`) is called at `GlobalUniforms.cpp:547` and
  `LightingUniforms.cpp:118` but arrives only via `LightingWrappersBase.h:3`. [~3m]

### Common/ExternalHeaders.h
- Add `<cmath>` — `std::lerp` (C++20, declared only in `<cmath>`) is used at `GlobalUniforms.cpp:343`/`:548` and
  currently compiles only via MSVC's transitive `<cmath>` pulls from other std headers; `<corecrt_math.h>` (`:107`)
  supplies only the global-namespace C functions. One line; makes the convention airtight repo-wide. [~2m]

## Critical files
- `Engine/Source/Graphics/Render/Render.h`
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp`
- `Engine/Source/Graphics/Render/LightingUniforms.cpp`
- `Engine/Source/Graphics/Render/MainUniforms.cpp`
- `Engine/Source/Graphics/Render/SmokeUniforms.cpp`
- `Engine/Source/Graphics/Render/WindUniforms.cpp`
- `Common/ExternalHeaders.h`

## Out of scope
- `WindUniforms.cpp:7`'s `Ui/SmokeWrappersBase.h` include — verified used (the Smoke tab owns the Wind
  Displacement sliders: `gWindToSmokeStrength`/`gWindSmokeRetention`/`gWindToSmokePower`/
  `gWindDisplacementNoiseScale`/`gWindSmokeAdvection`).
- Render.h's reliance on `Engine.h`/PCH ordering for `GridCoord`/std containers — documented load-bearing
  aggregation convention (`Engine/Source/CLAUDE.md`).
- Inline-global placement in Render.h — audited, all correct (every global has ≥2 consuming TUs).
- The game `Ui/HexShieldWrappers.h` PCH route into `MainUniforms.cpp:385-397` — see Notes.

## Acceptance criteria
- Client builds clean with the removed/replaced includes; no TU in the directory relies on a wrapper header it
  doesn't reference for `WrapperBase.h`/`HeightLerpWrapperQuartet.h` symbols.

## Notes
- No determinism/CRC/network/`kiVersion` exposure — include-graph only, zero behavior.
- Wrapper headers are deliberately NOT aggregated into `Engine.h` (`Engine/Source/Ui/CLAUDE.md`), so per-file
  wrapper includes are the sanctioned pattern; the additions here follow it.
- One grill decision: whether `MainUniforms.cpp` should also directly include the game `Ui/HexShieldWrappers.h`
  (currently PCH-only via the game `Pch.h`). The PCH route is plausibly the sanctioned convention for game wrapper
  headers — recommend leaving as-is unless the grill says otherwise.

## Verification Notes

Verified 2026-06-11 against current source; all items confirmed executable.
- Unused-include claims re-verified by grep: zero `gSmoke*`/`gWind*` in `GlobalUniforms.cpp`; zero
  `gLighting*`/`gCombine*`/`gSpread*`/`gPbr*`/`gSunMoon*` in `MainUniforms.cpp`; zero `game::` in `WindUniforms.cpp`.
- `enum CpuCounters` repo-wide grep: the only `\bCpuCounters\b` hits are `Render.h:17` itself and a doc-text naming
  example in `C++StyleGuide.txt:32`; real enums confirmed at `ProfileManagerBase.h:46` (`EngineCpuCounters`) and game
  `ProfileManager.h:6` (`GameCpuCounters`). Removal safe.
- Forward-decl removal safety: every engine header using `game::Frame`/`game::FrameInput` carries its own forward
  declaration (`GameBase.h:12/:14`, `AudioManager.h:11`, `StaticVoices.h:13`, `Client.h:14`, `Collection.h:6-8`,
  etc.) — nothing relies on `Render.h`'s copies via `Engine.h` ordering.
- `Game.h`→`Graphics/Camera.h` replacement: confirmed the three TUs use only `game::gpCamera` (incl. `SunAngle()`,
  declared `Camera.h:44`) and `game::Camera` constants (`:23`, `:32-33`); `gpCamera` declared `Camera.h:79`. Direct
  include precedent and shadow-free resolution confirmed (correction folded into the Design section above).
- `<cmath>` claim confirmed: `ExternalHeaders.h` has `<corecrt_math.h>`/`<corecrt_math_defines.h>` (`:107-108`)
  only; `std::lerp` used at `GlobalUniforms.cpp:343`/`:548`.
- `WrapperBase.h` declaration-line pairing corrected (`gBaseHeight` is `:214`, `gTerrainEarlyOut` is `:219` — the
  original draft had them swapped); confirmed every `*WrappersBase.h` includes `WrapperBase.h` at `:3`(-`:4`), and
  `LerpAtHeight` is `HeightLerpWrapperQuartet.h:21`, pulled in only via `LightingWrappersBase.h:3`.
- Out-of-scope `WindUniforms.cpp:7` claim confirmed: the five wind-displacement sliders are declared in
  `SmokeWrappersBase.h:27-31`.
- No items dropped; no score adjustment suggested.
