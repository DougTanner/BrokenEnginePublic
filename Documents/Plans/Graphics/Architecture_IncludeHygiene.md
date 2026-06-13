# Architecture: Include Hygiene (Engine/Source/Graphics top-level)

## Context

Source: /external-architecture-review on `Engine/Source/Graphics` (non-recursive). The seven top-level file
pairs are fully compliant with the ExternalHeaders.h rule (zero std-lib includes) and the aggregation-header
pattern (headers carry zero includes), but the .cpp include blocks have drifted: two unused includes, one
wrong-header include, one redundant PCH include, two files riding `Engine.h`/PCH ordering for symbols they
use directly, and two stale forward declarations. Each is grep-verified against current source.

## Design

### Engine/Source/Graphics/CameraBase.cpp
- Remove `#include "Pch.h"` (`:1`) — the PCH is force-included via `<ForcedIncludeFiles>` in every vcxproj;
  no other .cpp in this directory carries the explicit include. [~5m]
- Remove `#include "Graphics/Camera.h"` (`:5`) — no `game::` symbol is referenced anywhere in this file
  (grep-verified). The root base-class rule ("include game versions") binds *consumers* of camera state, not
  the base-class TU itself. [~5m]
- Replace `#include "Ui/GraphicsSettingsWrappersBase.h"` (`:6`) with `#include "Ui/WrapperBase.h"` — the
  wrappers this file actually uses (`gFov` `:41`, `gVisibleAreaExtraTop` `:108-110`, `gVisibleAreaExtraBottom`
  `:111`) are declared in `WrapperBase.h:212,217,218`; none of `GraphicsSettingsWrappersBase.h`'s own wrappers
  appear in the file. [~5m]

### Engine/Source/Graphics/GraphicsUtils.cpp
- Remove `#include "Game.h"` (`:8`) — the file's only game-layer symbol is `game::gpCamera`
  (`:79`, `:85`), declared in the game's `Graphics/Camera.h`, which `Game.h` does not include; the symbol
  actually arrives via the PCH (`Engine.h` → `GameBase.h` → game `Graphics/Camera.h`). The include
  contributes nothing this file uses. [~5m]

### Engine/Source/Graphics/Graphics.cpp
- Add `#include "Ui/WrapperBase.h"` — the file uses five `WrapperBase.h` wrappers (`gWireframe` `:427`,
  `gDebugTexture` `:434`, `gTerrainElevationTextureMultiplier` `:500`, `gSmokeTrailPower` `:510`,
  `gSmokeTrailAlpha` `:511`) that currently arrive only because each included `*WrappersBase.h` happens to
  include `WrapperBase.h`. The Ui wrapper headers are deliberately not aggregated into `Engine.h`
  (`Engine/Source/Ui/CLAUDE.md`), so the convention is to include the specific header you consume. [~5m]

### Engine/Source/Graphics/Screenshot.cpp
- Add a direct include for `TextureCache` — `TextureCache::CopyImageToHostMemory` (`:20`) arrives
  transitively via `Engine.h` → `Graphics/Managers/TextureManager.h` → `TextureCache.h`; `TextureCache.h` is
  not itself aggregated in `Engine.h`. Matches the repo precedent of directly including non-aggregated
  headers (cf. `Islands.cpp:5-6`). [~5m]

### Engine/Source/Graphics/Islands.h
- Remove the stale forward declarations `game::Frame` (`:5-10`) and `engine::Texture` (`:15`) — neither type
  is named anywhere in `Islands.h` or `Islands.cpp` (the interface uses `CoordFrames`/`GridCoord` only).
  Verify with a grep at execution before deleting. [~5m]

## Critical files
- `Engine/Source/Graphics/CameraBase.cpp`
- `Engine/Source/Graphics/GraphicsUtils.cpp`
- `Engine/Source/Graphics/Graphics.cpp`
- `Engine/Source/Graphics/Screenshot.cpp`
- `Engine/Source/Graphics/Islands.h`

## Out of scope
- `GraphicsUtils.cpp:5` `Memory/GlobalAllocator.h` removal — already item 1 of
  `Engine/DeadCodeAndUnusedIncludesSweep.md` (the ~12-TU stale-include sweep).
- `BT_CLIENT` guard normalization across the unwrapped files — documented policy
  (`Graphics/CLAUDE.md` guard-scope note) and subject of the open decision plan
  `Common/AggregationAndScopeRuleQualifiers.md`.
- `Screenshot.cpp:3`'s relative `../../../ThirdParty/stb/...` path style — cosmetic; the same relative form
  is used by `ThirdParty/Prebuilts/Source/Engine/Stb.cpp` (the engine's stb implementation TU);
  `DataPacker/Source/ExportJobs/Texture/Texture.cpp:50` uses the include-dir form `"stb/stb_image_write.h"` instead.
- `Graphics.h`'s `game::FrameInterpolate` forward declaration — owned by
  `Graphics/Architecture_RenderInterpolatesOwnership.md`.

## Acceptance criteria
- Client (and server, unaffected) builds clean; every removed include/forward-decl has zero remaining
  references in its TU.

## Notes
- Pure include/forward-decl mechanics, compile-checked; no determinism/CRC, `kiVersion`, replay, or guard-scope
  exposure. No grill decisions.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- `CameraBase.cpp` — `Pch.h` include at `:1`, game `Graphics/Camera.h` at `:5`, `GraphicsSettingsWrappersBase.h`
  at `:6`; zero `game::` references in the file; `gFov` `:41`, `gVisibleAreaExtraTop` `:108-110`,
  `gVisibleAreaExtraBottom` `:111`; declarations at `WrapperBase.h:212,217,218`; none of
  `GraphicsSettingsWrappersBase.h`'s 18 wrappers appear. `CameraBase.h` is aggregated at `Engine.h:56`, so the
  class declaration still arrives via the PCH after the removals. `<ForcedIncludeFiles>Pch.h` confirmed in all
  three configs of both BrokenEngineSandbox vcxprojs; only CameraBase.cpp carries the explicit include in this
  directory.
- `GraphicsUtils.cpp` — `Game.h` at `:8`; only game symbol is `game::gpCamera` (`:79`, `:85`); `Game.h`
  includes ClientSettings/Audio/Fleet/FleetSelection/Network only (no game `Graphics/Camera.h`); the symbol
  arrives via `Engine.h:96` → `GameBase.h:5-7` (BT_CLIENT-guarded game `Graphics/Camera.h` include).
- `Graphics.cpp` — `gWireframe` `:427`, `gDebugTexture` `:434`, `gTerrainElevationTextureMultiplier` `:500`,
  `gSmokeTrailPower` `:510`, `gSmokeTrailAlpha` `:511`; no `WrapperBase.h` include; each included
  `*WrappersBase.h` pulls `WrapperBase.h` (e.g. `GraphicsSettingsWrappersBase.h:3`). Ui CLAUDE.md confirms
  wrapper headers are deliberately non-aggregated.
- `Screenshot.cpp` — `TextureCache::CopyImageToHostMemory` at `:20`; `TextureCache.h` absent from `Engine.h`
  (arrives via `Engine.h:51` → `TextureManager.h:6`); precedent `Islands.cpp:5-6` confirmed.
- `Islands.h` — `game::Frame` fwd decl `:5-10`, `engine::Texture` `:15`; neither type named anywhere else in
  `Islands.h` or `Islands.cpp`.
- Out-of-scope cross-references confirmed to exist: `Engine/DeadCodeAndUnusedIncludesSweep.md` item 1 covers
  the stale `Memory/GlobalAllocator.h` includes (GraphicsUtils.cpp's `:5` include is unused — its
  `ScopedSuppressAllocationTracking` `:60` comes from `Common/AllocationTracking.h` via `Common.h`);
  `Common/AggregationAndScopeRuleQualifiers.md` exists. Corrected the stb-path out-of-scope bullet: the
  "same form" consumer is `ThirdParty/Prebuilts/Source/Engine/Stb.cpp`, not DataPacker's `Texture.cpp`.
