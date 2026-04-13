# `/Engine/Source/Ui/Screens/TweaksScreen/` - Tweaks Parameter UI (Base)

Multi-section ImGui parameter adjustment screen base class for runtime control over rendering parameters via Wrapper globals. Guarded by `if constexpr (kbDebugInput)` with an early return when the ImGui overlay is hidden. Screen state (section visibility, window positions, active subtab per section, and per-section apply flags) is persisted across restarts via `SaveState()`/`LoadState()`; the game layer calls `SaveState()` to capture state and writes it to disk.

## File Organization

- **TweaksScreenBase.h** - Base class declaration (`engine::TweaksScreenBase`) with `TweakSection` enum (14 sections), all render method signatures, public state members (section visibility, window positions, active subtab per section, active slider tracking, toggle bar bottom) used for persistence, and `SaveState()` method
- **TweaksScreenBase.cpp** - Core logic: constructor, `Render()`, toggle bar, section window rendering (applies loaded positions on first placement), slider helpers, `RenderWaveCountRadioButtons()`, `SaveState()` for capturing current screen state, and `LoadState()` for restoring persisted screen state including active subtabs and per-section apply flags
- **TweaksSliderMap.h/.cpp** - Standalone static class holding the slider-to-Wrapper lookup map; `Get()` returns the shared map instance. Includes lighting entries for all pipeline phases: deposit multiplier, pre-blur sigma/sample count/edge falloff, radial spread directionality/distance/ring count/jitter/decay/pass count plus a matching set of end-value interpolation targets for the final spread pass plus height-aware attenuation (spread distance reduction scoped to 0–islandHeight range, and intensity/decay reduction scoped to baseHeight–targetHeight range), combine (three weighted power curves: intensity+power each), pass normalize, exposure pass scale, new lighting system controls (directional, ambient, terrain/object channel intensities with add modes, below-base-height attenuation multiplier and power, separate day and night final multipliers), water specular controls, and smoke controls (decay, color, lighting multiplier, trails, noise)
- **TweaksScreen\<Section\>.cpp** - One file per section, each implementing a single `Render*Section()` method. Sections cover test, PBR, terrain, water (specular, low, medium, debug), lighting (with Write/Read tab bar), shadow, misc, smoke, and wind. Each file uses `static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::k*)` for the section index

## Architecture Notes

A full-width toggle bar lets users show/hide any combination of sections, each rendered as its own auto-resizing ImGui window. Uses a data-driven design with a slider-to-Wrapper lookup map (`TweaksSliderMap`) and function pointer table for section rendering. The Lighting section uses a Write/Read/Effects tab bar: Write covers the pipeline write phases (pre-blur, deposit, spread, combine); Read covers lighting readback controls (directional, ambient, terrain/object intensities) and water specular; Effects covers game-side lighting wrapper globals (per-effect intensity/area controls) via the virtual `RenderLightingEffectsTab()`. Sections with tab bars track the active tab index and restore it on load via `ImGuiTabItemFlags_SetSelected`. Each tabbed section also tracks an apply flag independently, set per section during load and cleared per section after the tab switch is applied.

`RenderHexShieldSection()` and `RenderWindDepositsSection()` are **pure virtual** — the game provides these implementations via `game::TweaksScreen`. `RenderLightingEffectsTab()` is **virtual** (default empty) — the game overrides it to render the Effects tab in the Lighting section.

**Slider map**: `TweaksSliderMap` is a standalone static class in `TweaksSliderMap.h/.cpp` that holds the engine-side slider-to-Wrapper lookup map. `TweaksSliderMap::Get()` returns a reference to the static map. Game-specific entries are inserted by `game::TweaksScreen`'s constructor rather than via virtual override.

`RenderWaveCountRadioButtons(Wrapper& rCountWrapper)` is a shared helper that renders an inline radio button row for selecting wave count (15/31/63/127/255). Used by water section files that expose a wave count parameter.

**Ordering convention**: Tweaks screen slider order in each `TweaksScreen<Section>.cpp` file is the source of truth for section layout. Wrapper global order in `WrapperBase.h/.cpp` (and `game::Wrapper.h/.cpp`) must match.

**Slider label formatting**: Drop redundant prefixes from slider display labels when the section header or tab already provides that context (e.g., "Sigma" not "Lighting Blur Sigma" under the Pre-Blur header). Use the `mapKey` parameter of `WrapperSlider` to map the short display label to the full slider map key.

**Auto-hide behavior**: When dragging a slider, all other UI elements fade to alpha=0 while preserving layout, so the user can see the visual effect of the parameter change without UI clutter.

## See Also

- [Game override](../../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/) - `game::TweaksScreen` with hex shield and wind deposit section implementations
