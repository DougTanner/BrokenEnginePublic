# Convert Engine Tunables to 1m = 1unit Scale

## Context

The legacy-island-removal refactor set `engine::kfMetersToUnits = 1.0f` and
`game::Frame::kfCellWidth/kfCellHeight = 600.0f` (was `0.2f` and `300.0f`). Islands
now render at their true Gaea2 dimensions (Island-1x1 = 400 m × 100 m elevation),
and the world grid is 600 × 600 engine units per cell — roughly a 5 × scale-up
versus the pre-refactor world.

The rest of the engine still has gameplay / camera / audio constants tuned for
the old 60-unit-cell regime. They keep islands and gameplay visually consistent
but are no longer "1 unit = 1 meter" semantically. This plan catalogues every
hardcoded literal that almost certainly needs adjusting and proposes the simplest
pass to bring them into the meters-everywhere convention.

The end goal is to delete `kfMetersToUnits` entirely once the whole engine reads
in meters.

## Approach

Walk the catalog below in three groups so each batch is independently testable:

1. **Camera + visible-area** — render-only; bring the camera to plausible RTS
   altitudes (e.g. 750 m, max 3000 m) and ensure the visible-area culler isn't
   evicting the cell the player is in.
2. **Gameplay tunables** — speeds, ranges, radii, spawn offsets, follow
   distances. Multiply by 5 × (the ratio of the cell-scale change). Recheck
   feel; tweak.
3. **Audio falloff** — `Set3dSettings` distance and per-voice `mfManualFadeEnd`
   defaults need to expand 5 ×. Curve-distance-scaler stays unitless.

After each batch, run the game in the sandbox and visually verify the affected
behavior (camera zoom range, AI navigation distance feel, audio falloff).

## Catalog of literals to adjust

### Camera

- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp:23-24` — `kfEyeHeightMin = 150.0f`, `kfEyeHeightMax = 600.0f`
- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.h:24,27` — `kfCameraEyeHeightDefault = 150.0f`, `kfCameraEyeHeightInitial = +48` (note: appears twice; the second is `kfCameraEyeHeightDefault + 48.0f`)
- `Engine/Source/Graphics/CameraBase.cpp:128` — `kfMinEyeHeight = 150.0f` (LOD bucket pivot)
- `Projects/BrokenEngineSandbox/Source/Game.cpp:1199`, `Game.h:153` — hardcoded `198.0f` mirror of `kfCameraEyeHeightInitial`

### Visible area

- `Engine/Source/Frame/FrameBase.h:123-124` — `kfVisibleEastWest = 65.0f`, `kfVisibleNorthSouth = 45.0f`. Now tiny vs a 600 cell; subscriptions will be over-aggressive.

### Gameplay (speeds, ranges, radii)

- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:182-183` — `kfMinPlayerDistance = 120.0f`, `kfDesiredAnchorDistance = 150.0f`
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:431` — `kfMaxTargetingRange = 45.0f`
- `Projects/BrokenEngineSandbox/Source/Frame/TerrainUtils.cpp:14` — `kfReturnToIslandDistance = 150.0f`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp:20-21` — `kfFlagshipFollowDistanceSquared = 50²`, `kfFlagshipCloseDistanceSquared = 12.5²`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp:280` — arrival threshold `100.0f`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp:354` — spawn offset `(+45, -12)` from cell center
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.h:21,30,33` — `kfPlayerBlastersSpeed = 150`, `kfPlayerAcceleration = 75`, `kfPlayerMaxSpeed = 33.33`, `kfPlayerRadius = 1.1`. Player radius is the single source of truth for derived radii via ratio multipliers (per `Players/CLAUDE.md`), so only `kfPlayerRadius` need change.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp:71` — `kfBlastersSpeed = 50.0f`

### Audio

- `Engine/Source/Audio/StaticVoices.h:87,89,94` — `mfCurveDistanceScaler = 10.0f` default, `mfManualFadeEnd = 150.0f` default
- `Projects/BrokenEngineSandbox/Source/Game.cpp:58` — `Set3dSettings(10.0f, 0.0f, 300.0f, 0.15f)` — the `300.0f` manual fade matched the old cell width exactly; should now scale with `kfCellWidth` or become an explicit "meters" literal

### Base height

- `Engine/Source/Ui/WrapperBase.cpp:9` — `gBaseHeight(6.0f, 0.0f, 20.0f)` — base flying height capped at 20 m. Likely fine on absolute scale (~20 m above sea level for low-altitude RTS units), but visually verify against the new island heights (up to ~100 m).

## Final cleanup

Once all the literals above are in meters, delete `engine::kfMetersToUnits` from
`Engine/Source/Frame/IslandTerrain.h`. Touch points:

- `Engine/Source/Frame/IslandTerrain.cpp:36` — `mfQuadFootprint = mfWorldFootprintMeters * kfMetersToUnits` becomes `mfQuadFootprint = mfWorldFootprintMeters`
- `Engine/Source/Frame/IslandTerrain.cpp:50` — `mfSeaFloorElevation = -kfOceanDepthMeters * kfMetersToUnits` becomes `mfSeaFloorElevation = -kfOceanDepthMeters`
- `Engine/Source/Frame/IslandTerrain.cpp:345` — `fDistance = 2.0f * kfMetersToUnits` becomes `fDistance = 2.0f` (meters)
