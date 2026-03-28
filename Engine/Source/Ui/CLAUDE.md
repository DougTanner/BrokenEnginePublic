# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes for UI rendering.

## Key Systems

- **Wrapper** (`WrapperBase.h/.cpp`) - Type-safe value container for UI-bound settings. Stores current/previous values with min/max bounds, supports float, bool, and discrete enum types. Change detection via `Changed()` method. Supports percent-based get/set for normalized slider control, toggle for booleans, and index-based access for discrete allowed-value lists
- **NetworkUiControl** (`NetworkUiControl.h`) - Templated helper that tracks pending state for network-confirmed UI controls (toggles, dropdowns, sliders). Disables a control while awaiting server confirmation, then resets when the confirmed value arrives

## Architecture Notes

Wrapper globals are declared in `WrapperBase.h` and defined in `WrapperBase.cpp` as `engine::` namespace globals. They expose runtime-adjustable parameters consumed by shaders, audio, and rendering code throughout the engine. Lighting globals cover the full pipeline: deposit (texture multiplier), pre-blur (sigma, sample count, and edge falloff), radial spread (including a parallel set of end-value interpolation targets for the final spread pass, plus height-aware attenuation controls for spread distance and intensity), combine, and directional/indirect/terrain/object channel controls. Game-specific globals (hex shield, wind deposits) live in `game::Wrapper.h/.cpp`, not in `WrapperBase`.

**Ordering convention**: Wrapper global declaration order in `WrapperBase.h` and definition order in `WrapperBase.cpp` must match the tweaks screen UI layout (e.g., `TweaksScreenLighting.cpp` for lighting globals).

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays (TweaksScreenBase)
