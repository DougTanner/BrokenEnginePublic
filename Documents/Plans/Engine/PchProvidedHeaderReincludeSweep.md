# PCH-Provided Header Re-include Sweep

## Context
Follow-up from the `Architecture_UnusedIncludes` execution. The user set a governing rule (now documented in `Projects/BrokenEngineSandbox/Source/AGENTS.md`): headers directly force-included by the game `Pch.h` — `ExternalHeaders.h`, `Log/LogTypes.h`, `Common.h`, `Shaders/ShaderLayouts.h`, `Ui/HexShieldWrappers.h`, `Ui/WindDepositsWrappers.h`, `Frame/Frame.h`, `Engine.h` — are visible in every client/server TU and must not be re-`#include`d elsewhere. The parent plan applied this to the two clear/zero-risk `.cpp` cases (`TweaksScreenHexShield.cpp` / `TweaksScreenWindDeposits.cpp` re-including the injected wrapper headers). This plan handles the remaining, higher-nuance cases the sweep found — deferred because they touch **headers** (self-containment) and interact with **DataPacker's separate, leaner PCH**.

Known re-include sites of `Frame/Frame.h` (grep, current source):
- Headers: `Engine/Source/GameBase.h:6`, `Projects/BrokenEngineSandbox/Source/Frame/FrameCollections.h:3`
- `.cpp`: `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp:6`, `Engine/Source/Frame/TimeStep.cpp:3`, `Engine/Source/Frame/IslandChainPlacement.cpp:4`, `Engine/Source/Frame/FrameBase.cpp:3`, `Engine/Source/Frame/Collision.cpp:3`

Broader (transitive) question: `Ui/WrapperBase.h` (and everything the eight Pch headers transitively guarantee) is also always visible, so explicit re-includes of those — e.g. the `WrapperBase.h` includes the parent plan deliberately kept in `AreaLightsRender.cpp`/`Explosions.cpp`/`WindTrailsRender.cpp`/`WindRadialsRender.cpp`/`WindRadialsUpdate.cpp` — are candidates too. The parent plan's original "add explicit `WrapperBase.h`" directness fix was reversed by this rule.

## Design

**Decision plan (present options).** The tension: the rule says "don't re-include Pch-provided headers," but stripping `Frame/Frame.h` from **headers** (`GameBase.h`, `FrameCollections.h`) breaks the "headers are self-contained / include-what-you-use" principle, and DataPacker uses a leaner PCH (`ExternalHeaders.h`/`Log/LogTypes.h`/`Common.h` only) — so any shared header/TU that DataPacker compiles must keep explicit includes for the game-Pch-only headers (`Frame.h`/`Engine.h`/`ShaderLayouts.h`/wrappers). Present:

- **Option A — enforce the rule everywhere:** strip every re-include of a directly-Pch-provided header, in both `.cpp`s and headers. Maximal consistency with the documented rule. Risk: non-self-contained headers, and must first confirm no stripped site is (or becomes) DataPacker-compiled.
- **Option B — `.cpp`-only enforcement (recommended default):** strip the redundant re-includes only from `.cpp` TUs (the five `Frame/Frame.h` `.cpp` sites), and leave headers self-contained (headers keep `Frame/Frame.h`). Honors the rule where it's unambiguous, preserves header self-containment. Decide separately whether to also fold the transitive `WrapperBase.h` `.cpp` cases.
- **Option C — accept + refine the doc:** keep current includes; narrow the AGENTS.md rule to "don't add NEW re-includes of Pch-provided headers" rather than mandating removal of existing self-contained-header includes.

For each candidate site, verify it is NOT compiled in DataPacker (DataPacker's PCH omits `Frame.h`/`Engine.h`/wrappers, so a DataPacker-compiled TU genuinely needs the explicit include) before removing anything. Compile-check both client and server after.

## Critical files
- `Engine/Source/GameBase.h`, `Projects/BrokenEngineSandbox/Source/Frame/FrameCollections.h` (header re-includes)
- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.cpp`, `Engine/Source/Frame/TimeStep.cpp`, `Engine/Source/Frame/IslandChainPlacement.cpp`, `Engine/Source/Frame/FrameBase.cpp`, `Engine/Source/Frame/Collision.cpp`
- `Projects/BrokenEngineSandbox/Source/Pch.h`, `DataPacker/Source/Pch.h` (the two PCH definitions the decision hinges on)
- The kept `Ui/WrapperBase.h` re-include sites (transitive question) if folded

## Out of scope
- The DataPacker `#include "Pch.h"` house-style lines and any DataPacker-only re-includes (DataPacker's PCH is the leaner set; its rule is separate).
- Adding a rule-enforcement lint/clang-tidy check (possible future, not this plan).
- The `Screenshot.cpp` stb consumption centralization (separate follow-up).

## Notes
- Invariant exposure: none — include-only edits, compile-checked; both client and server builds must compile, and any header touched that DataPacker compiles must also still build.
- Grill decision: Option A vs B vs C (recommend B — `.cpp`-only, preserve header self-containment), plus whether to fold the transitive `WrapperBase.h` `.cpp` re-includes.
