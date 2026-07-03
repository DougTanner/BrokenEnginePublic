# Architecture: Unused Includes & Aggregation Deviations

## Context
Source: /external-architecture-review on Engine/Source (recursive), re-verified against source. Six verified unused include lines (zero symbols referenced), one directness fix, and one aggregation-pattern deviation. Include graph is otherwise healthy: no cycles, no dead modules, `Common/` layer clean.

## Design

### Verified removals (zero symbols from the header referenced)
- `Engine/Source/Frame/Collections/Explosions/ExplosionsSpawn.cpp:12-13` — remove `#include "Ui/LightingWrappers.h"` and `#include "Ui/ParticleWrappers.h"` (game headers; 0 of their globals referenced — particle scaling flows through the `pParticle*Scale` `Wrapper*` members of `ExplosionType`, and the `gWindDeposit*`/`gExplosion*Trail*` globals used come from the game `Pch.h`-included `WindDepositsWrappers.h` and the kept `SmokeWrappers.h`) [~5m]
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenMisc.cpp:5` — remove `#include "Ui/TerrainWrappersBase.h"` (file uses only `gMiscDebugTextureLinearRange` from `MiscWrappersBase.h`) [~5m]
- `Engine/Source/GameBase.cpp:11-12` — remove `#include "Frame/HealthDamage.h"` and `#include "Frame/Collections/Players/Players.h"` (game headers; no symbol from either referenced anywhere in the file) [~5m]
- `Engine/Source/Graphics/Screenshot.cpp:5` — remove the relative-path `#include "../../../ThirdParty/stb/stb_image_write.h"`; `Common/ExternalHeaders.h:299` already includes it (implementation unit lives in `ThirdParty/Prebuilts/Source/Engine/Stb.cpp`) [~5m]

### Directness fix
- `Engine/Source/Main.cpp` — add a direct `#include "Ui/WrapperBase.h"`: `gBaseHeight` (Main.cpp:181,231; declared `WrapperBase.h:228`) currently arrives transitively via `GraphicsSettingsWrappersBase.h:3`, defeating the deliberate wrapper-header carve-out from `Engine.h` [~5m]

### Aggregation deviation
- `Engine/Source/Graphics/Managers/InstanceManager.h:3` — `#include "renderdoc_app.h"` bypasses `Common/ExternalHeaders.h`. Move to `ExternalHeaders.h` under `BT_CLIENT`, or keep local with a one-line comment documenting the single-consumer exception [~5m]

## Critical files
- `Engine/Source/Graphics/Graphics.cpp`, `Engine/Source/Graphics/Screenshot.cpp`, `Engine/Source/GameBase.cpp`, `Engine/Source/Main.cpp`
- `Engine/Source/Frame/Collections/Explosions/ExplosionsSpawn.cpp`
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenMisc.cpp`
- `Engine/Source/Graphics/Managers/InstanceManager.h`, `Common/ExternalHeaders.h`

## Out of scope
- `PuffsUpdate.cpp:5` / `PointLightsUpdate.cpp:5` `WrapperBase.h` removal — **verified invalid**: both TUs call `->Get()` on `Wrapper*` controller-scale arrays (`PuffsUpdate.cpp:38-39,77-78`; `PointLightsUpdate.cpp:50-51,151-152`), and their headers (`CollectionController.h:7`, `PointLights.h:11`) only forward-declare `class Wrapper` — the include supplies the complete type and is required
- Removing the redundant-transitive `WrapperBase.h` includes in `AreaLightsRender.cpp` / `Explosions.cpp` / `WindTrailsRender.cpp` / `WindRadialsRender.cpp` / `WindRadialsUpdate.cpp` — dropped: the sibling `*Wrappers*.h` headers re-include `WrapperBase.h` anyway (no rebuild edge removed), and those files use the `Wrapper` class directly, so the explicit include is the more-direct style this plan's directness fix advocates
- The 14 files with redundant explicit `#include "Pch.h"` at line 1 (Network/Profile subtrees) — harmless house-style inconsistency, not worth churn
- The sanctioned engine↔game include cycle through `GameBase.h` and the game `Pch.h` wrapper-header injection — by design
- Any include restructuring beyond removals/additions listed here

## Notes
- Invariant exposure: none — include-only edits, compile-checked; both client and server builds must compile
- Grill decision: renderdoc include — move to `ExternalHeaders.h` vs keep local with comment (recommend move, matching the consumption-header rule)
