# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes for UI rendering.

## Key Systems

- **Wrapper** (`WrapperBase.h/.cpp`) - Type-safe value container for UI-bound settings. Stores current/previous values with min/max bounds, supports float, bool, and discrete enum types. Change detection via `Changed()` method. Supports percent-based get/set for normalized slider control, toggle for booleans, and index-based access for discrete allowed-value lists
- **NetworkUiControl** (`NetworkUiControl.h`) - Templated helper that tracks pending state for network-confirmed UI controls (toggles, dropdowns, sliders). Disables a control while awaiting server confirmation, then resets when the confirmed value arrives

## Architecture Notes

Wrapper globals are declared in `WrapperBase.h` and defined in `WrapperBase.cpp` as `engine::` namespace globals. They expose runtime-adjustable parameters consumed by shaders, audio, and rendering code throughout the engine. Lighting globals span the full pipeline: pre-blur, deposit, radial spread (start and end interpolation targets, plus height-aware attenuation), combine (Uchimura tone curve: max brightness, contrast, linear start/length, toe, black tightness, hue preserve), directional/ambient intensities and powers for terrain and objects (with multiplicative/additive blend modes, power mode 0–1 blending luminance-based vs average-based hue-preserving scalar pow, below-base-height attenuation multiplier and power, and separate day/night final multipliers lerped by `fDayPercent` into the `fLightingTimeOfDayMultiplier` uniform), water specular highlights, and water skybox reflections. `gOpaqueUi` (bool) toggles the depth pre-pass that culls scene fragments behind registered opaque UI windows. `gUiOpacity` (float, 0–1) controls ImGui window background alpha globally, applied in `ImGuiManager::Prepare()`. Game-specific globals (hex shield, wind deposits) live in `game::Wrapper.h/.cpp`, not in `WrapperBase`.

**Ordering convention**: Wrapper global declaration order in `WrapperBase.h` and definition order in `WrapperBase.cpp` must match the tweaks screen UI layout (e.g., `TweaksScreenLighting.cpp` for lighting globals).

**Virtual extension points**: `RenderLightingEffectsTab()` is a virtual method on `TweaksScreenBase` allowing the game layer to inject an Effects tab into the Lighting Tweaks section for game-specific lighting wrapper controls.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays (TweaksScreenBase)
