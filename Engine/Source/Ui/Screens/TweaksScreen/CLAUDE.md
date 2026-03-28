# `/Engine/Source/Ui/Screens/TweaksScreen/` - Tweaks Parameter UI (Base)

Multi-section ImGui parameter adjustment screen base class for runtime control over rendering parameters via Wrapper globals. Guarded by `if constexpr (kbEnableDebugInput)` with an early return when the ImGui overlay is hidden.

## File Organization

- **TweaksScreenBase.h** - Base class declaration (`engine::TweaksScreenBase`) with `TweakSection` enum (13 sections) and all render method signatures
- **TweaksScreenBase.cpp** - Core logic: constructor, `Render()`, toggle bar, section window rendering, slider helpers, and `RenderWaveCountRadioButtons()`
- **TweaksSliderMap.h/.cpp** - Standalone static class holding the slider-to-Wrapper lookup map; `Get()` returns the shared map instance. Includes lighting entries for all pipeline phases: deposit multiplier, pre-blur sigma/sample count/edge falloff, radial spread directionality/distance/ring count/jitter/decay/pass count plus a matching set of end-value interpolation targets for the final spread pass plus height-aware attenuation (distance and intensity), combine exposure/power/linear-clamp, new lighting system controls (directional, ambient, terrain/object channel intensities with add modes, time-of-day multiplier), and water specular controls
- **TweaksScreen\<Section\>.cpp** - One file per section, each implementing a single `Render*Section()` method. Sections cover test, PBR, terrain, water (specular, low, medium), lighting (with Write/Read tab bar), shadow, misc, smoke, and wind. Each file uses `static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::k*)` for the section index

## Architecture Notes

A full-width toggle bar lets users show/hide any combination of sections, each rendered as its own auto-resizing ImGui window. Uses a data-driven design with a slider-to-Wrapper lookup map (`TweaksSliderMap`) and function pointer table for section rendering. The Lighting section uses a Write/Read tab bar: Write covers the pipeline write phases (pre-blur, deposit, spread, combine); Read covers lighting readback controls (directional, ambient, terrain/object intensities) and water specular.

`RenderHexShieldSection()` and `RenderWindDepositsSection()` are **pure virtual** — the game provides these implementations via `game::TweaksScreen`.

**Slider map**: `TweaksSliderMap` is a standalone static class in `TweaksSliderMap.h/.cpp` that holds the engine-side slider-to-Wrapper lookup map. `TweaksSliderMap::Get()` returns a reference to the static map. Game-specific entries are inserted by `game::TweaksScreen`'s constructor rather than via virtual override.

`RenderWaveCountRadioButtons(Wrapper& rCountWrapper)` is a shared helper that renders an inline radio button row for selecting wave count (15/31/63/127/255). Used by water section files that expose a wave count parameter.

**Ordering convention**: Tweaks screen slider order in each `TweaksScreen<Section>.cpp` file is the source of truth for section layout. Wrapper global order in `WrapperBase.h/.cpp` (and `game::Wrapper.h/.cpp`) must match.

**Slider label formatting**: Drop redundant prefixes from slider display labels when the section header or tab already provides that context (e.g., "Sigma" not "Lighting Blur Sigma" under the Pre-Blur header). Use the `mapKey` parameter of `WrapperSlider` to map the short display label to the full slider map key.

**Auto-hide behavior**: When dragging a slider, all other UI elements fade to alpha=0 while preserving layout, so the user can see the visual effect of the parameter change without UI clutter.

## See Also

- [Game override](../../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/) - `game::TweaksScreen` with hex shield and wind deposit section implementations
