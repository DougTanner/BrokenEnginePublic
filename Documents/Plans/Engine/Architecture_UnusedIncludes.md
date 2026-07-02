# Architecture: Unused Includes & Aggregation Deviations

## Context
Source: /external-architecture-review on Engine/Source (recursive). Dependency agent verified 9 HIGH-confidence unused include lines (zero symbols referenced), 5 MEDIUM redundant-transitive `WrapperBase.h` includes, and 3 aggregation-pattern deviations. Include graph is otherwise healthy: no cycles, no dead modules, `Common/` layer clean.

## Design

### HIGH-confidence removals (zero symbols from the header referenced — spot-verified)
- `Engine/Source/Graphics/Graphics.cpp:10` — remove `#include "Ui/WaterWrappersBase.h"`; the only water-adjacent symbol used (`gWaterShapeDetail`, Graphics.cpp:447) is declared in `GraphicsSettingsWrappersBase.h` [~5m]
- `Engine/Source/Frame/Collections/Explosions/ExplosionsSpawn.cpp:12-13` — remove `#include "Ui/LightingWrappers.h"` and `#include "Ui/ParticleWrappers.h"` (game headers; 0 of their globals referenced — particle scaling flows through `Wrapper*` members of `ExplosionType`, `Explosions.h:86-99`) [~5m]
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenMisc.cpp:5` — remove `#include "Ui/TerrainWrappersBase.h"` (file uses only `gMiscDebugTextureLinearRange` from `MiscWrappersBase.h`) [~5m]
- `Engine/Source/GameBase.cpp:11-12` — remove `#include "Frame/HealthDamage.h"` and `#include "Frame/Collections/Players/Players.h"` (game headers; no symbol from either referenced) [~5m]
- `Engine/Source/Graphics/Screenshot.cpp:5` — remove the relative-path `#include "../../../ThirdParty/stb/stb_image_write.h"`; `Common/ExternalHeaders.h:299` already includes it in every TU (implementation unit lives in `ThirdParty/Prebuilts/Source/Engine/Stb.cpp`) [~5m]
- `Engine/Source/Frame/Collections/Puffs/PuffsUpdate.cpp:5` and `PointLights/PointLightsUpdate.cpp:5` — remove `#include "Ui/WrapperBase.h"` (neither file references any `g*` wrapper global or `Wrapper` token) [~5m]

### MEDIUM redundant-transitive removals (wrapper globals come from sibling headers that include WrapperBase.h themselves)
- `Engine/Source/Frame/Collections/AreaLights/AreaLightsRender.cpp:7`, `Explosions/Explosions.cpp:6`, `WindTrails/WindTrailsRender.cpp:6`, `WindRadials/WindRadialsRender.cpp:6`, `WindRadials/WindRadialsUpdate.cpp:6` — remove `#include "Ui/WrapperBase.h"`; verify each still compiles (the wrapper globals used are declared in the specific `*WrappersBase.h` headers these files also include) [~15m total]

### Directness fix
- `Engine/Source/Main.cpp` — add a direct `#include "Ui/WrapperBase.h"`: `gBaseHeight` (Main.cpp:181,231) currently arrives transitively via `GraphicsSettingsWrappersBase.h:8`, defeating the deliberate wrapper-header carve-out from `Engine.h` [~5m]

### Aggregation deviation
- `Engine/Source/Graphics/Managers/InstanceManager.h:3` — `#include "renderdoc_app.h"` bypasses `Common/ExternalHeaders.h`. Move to `ExternalHeaders.h` under `BT_CLIENT`, or keep local with a one-line comment documenting the single-consumer exception [~5m]

## Critical files
- `Engine/Source/Graphics/Graphics.cpp`, `Engine/Source/Graphics/Screenshot.cpp`, `Engine/Source/GameBase.cpp`, `Engine/Source/Main.cpp`
- `Engine/Source/Frame/Collections/` (Explosions, Puffs, PointLights, AreaLights, WindTrails, WindRadials TUs)
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenMisc.cpp`
- `Engine/Source/Graphics/Managers/InstanceManager.h`, `Common/ExternalHeaders.h`

## Out of scope
- The 14 files with redundant explicit `#include "Pch.h"` at line 1 (Network/Profile subtrees) — harmless house-style inconsistency, not worth churn
- The sanctioned engine↔game include cycle through `GameBase.h` and the game `Pch.h` wrapper-header injection — by design
- Any include restructuring beyond removals/additions listed here

## Notes
- Invariant exposure: none — include-only edits, compile-checked; both client and server builds must compile
- Grill decision: renderdoc include — move to `ExternalHeaders.h` vs keep local with comment (recommend move, matching the consumption-header rule)
