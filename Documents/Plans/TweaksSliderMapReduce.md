# Plan: Split TweaksSliderMap.cpp Per-Section

## Context

`Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` has grown to ~315 entries — one giant initializer flat-listing every label/wrapper pair across every tweaks subsection (Sun/Moon, Water, Terrain, Smoke, etc.). The per-target Sun/Moon Intensity work (see `~/.claude/plans/tweaks-menu-sun-moon-virtual-bentley.md`) added 8 more entries and the trend continues with each new control.

Project `CLAUDE.md` says files exceeding 500–1000 lines should consider `/reduce-file`. The map file is approaching that threshold and, more importantly, sits structurally out-of-step with its neighbours: the screen rendering side already splits per-section into `TweaksScreenSunMoon.cpp`, `TweaksScreenWater.cpp`, etc., but the slider-map side keeps everything in one file. That asymmetry forces every slider addition to touch two unrelated files in different directories' worth of context.

## Approach

Mirror the existing `TweaksScreen*.cpp` per-section split on the map side. Each section gets its own `TweaksSliderMap<Section>.cpp` file that contributes its entries to a shared registry.

Two viable shapes:

**Option A — static registrar pattern.** Each `TweaksSliderMap<Section>.cpp` defines a file-scope `static SliderMapRegistrar sRegistrar([](auto& rMap){ rMap.emplace("Sun Terrain", &gSunMoonSunIntensityTerrain); ... });` that runs at static-init time and inserts its entries into the global map. The original `TweaksSliderMap.cpp` shrinks to just the `gSliderMap` definition + the registrar plumbing (~30 lines).

**Option B — explicit aggregation.** Each section file exposes a `void RegisterSunMoonSliders(SliderMap&)` free function. `TweaksSliderMap.cpp` keeps the map definition and a single `BuildMap()` that calls each registrar in order.

Option B is simpler and avoids static-init-order surprises; option A scales to N sections without touching the aggregator. KISS favours B.

Whichever option is picked, follow the per-section division already established by the screen files (one file per `TweaksScreen<Section>.cpp`). Put new files alongside their screen siblings in `Engine/Source/Ui/Screens/TweaksScreen/`.

## Files to Modify

- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.cpp` — slim down to map definition + aggregation glue
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksSliderMap.h` — declare the per-section registration functions (option B) or registrar type (option A)
- New: `TweaksSliderMapSunMoon.cpp`, `TweaksSliderMapWater.cpp`, `TweaksSliderMapTerrain.cpp`, `TweaksSliderMapSmoke.cpp`, etc. — one per existing `TweaksScreen<Section>.cpp` file
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/*.vcxproj` and matching `.filters` — add the new files under the existing TweaksScreen filter

## Risks / Open Questions

- **Label uniqueness across sections**: Today the giant initializer makes label collisions visually obvious. After the split, two sections could each declare `"Brightness"` and the conflict surfaces only at runtime. Add a debug-build assertion in the aggregator that the count after each registrar matches the count-before plus the registrar's expected size, or do a single duplicate-key sweep at the end.
- **Map-entry ordering**: If the slider-map insertion order matters anywhere (e.g., iteration order for save/load, or slider rendering order), option B's explicit ordered call list preserves it; option A's static-init order is implementation-defined across translation units. Verify whether order matters before choosing.
- **Worth the split now?**: At 315 entries the file is below the 500-line threshold. The file may not warrant `/reduce-file` until it crosses 500. Hold this plan until the file actually trips the threshold or until the next batch of per-target controls (per `WrapperArrayCollapse.md`) is approved — whichever comes first.
- **Tweak section CLAUDE.md doc**: `Engine/Source/Ui/Screens/TweaksScreen/CLAUDE.md` documents the current single-file convention; update it if the split ships.
