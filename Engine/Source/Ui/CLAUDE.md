# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes for UI rendering.

## Key Systems

- **Wrapper** (`WrapperBase.h/.cpp`) - Type-safe value container for UI-bound settings. Stores current/previous values with min/max bounds, supports float, bool, and discrete enum types. Change detection via `Changed()` method. Supports percent-based get/set for normalized slider control, toggle for booleans, and index-based access for discrete allowed-value lists

## Architecture Notes

Wrapper globals are declared in `WrapperBase.h` and defined in `WrapperBase.cpp` as `engine::` namespace globals. They expose runtime-adjustable parameters consumed by shaders, audio, and rendering code throughout the engine. Game-specific globals (hex shield, wind deposits) live in `game::Wrapper.h/.cpp`, not in `WrapperBase`.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays (TweaksScreenBase)
