# `/Engine/Source/Ui/Screens/TweaksScreen/` - Tweaks Parameter UI (Base)

Multi-section ImGui parameter adjustment screen base class for runtime control over rendering parameters via Wrapper globals. Guarded by `if constexpr (kbEnableDebugInput)` with an early return when the ImGui overlay is hidden.

## File Organization

- **TweaksScreenBase.h** - Base class declaration (`engine::TweaksScreenBase`) with `TweakSection` enum (14 sections) and all render method signatures
- **TweaksScreenBase.cpp** - Core logic: constructor, `Render()`, toggle bar, section window rendering, slider helpers, and `RenderWaveCountRadioButtons()`
- **TweaksSliderMap.h/.cpp** - Standalone static class holding the slider-to-Wrapper lookup map; `Get()` returns the shared map instance. Includes lighting entries for all pipeline phases: deposit multiplier/energy normalize, radial spread directionality/distance/ring count/jitter/decay/pass count, and combine exposure/power/linear-clamp
- **TweaksScreen\<Section\>.cpp** - One file per section, each implementing a single `Render*Section()` method. Sections cover test, PBR, terrain, water (specular, low, medium, lighting), lighting, shadow, misc, smoke, and wind. Each file uses `static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::k*)` for the section index

## Architecture Notes

A full-width toggle bar lets users show/hide any combination of sections, each rendered as its own auto-resizing ImGui window. Uses a data-driven design with a slider-to-Wrapper lookup map (`TweaksSliderMap`) and function pointer table for section rendering.

`RenderHexShieldSection()` and `RenderWindDepositsSection()` are **pure virtual** — the game provides these implementations via `game::TweaksScreen`.

**Slider map**: `TweaksSliderMap` is a standalone static class in `TweaksSliderMap.h/.cpp` that holds the engine-side slider-to-Wrapper lookup map. `TweaksSliderMap::Get()` returns a reference to the static map. Game-specific entries are inserted by `game::TweaksScreen`'s constructor rather than via virtual override.

`RenderWaveCountRadioButtons(Wrapper& rCountWrapper)` is a shared helper that renders an inline radio button row for selecting wave count (15/31/63/127/255). Used by water section files that expose a wave count parameter.

**Auto-hide behavior**: When dragging a slider, all other UI elements fade to alpha=0 while preserving layout, so the user can see the visual effect of the parameter change without UI clutter.

## See Also

- [Game override](../../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/) - `game::TweaksScreen` with hex shield and wind deposit section implementations
