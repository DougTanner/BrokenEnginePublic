# `/Engine/Source/Ui/Screens/TweaksScreen/` - Tweaks Parameter UI

Multi-section ImGui parameter adjustment screen for runtime control over rendering parameters via Wrapper globals. Guarded by `if constexpr (kbEnableDebugInput)` with an early return when the ImGui overlay is hidden.

## File Organization

- **TweaksScreen.h** - Class declaration with `TweakSection` enum and all render method signatures
- **TweaksScreen.cpp** - Core logic: constructor, `Render()`, toggle bar, section window rendering, slider helpers, and the static slider-to-Wrapper lookup map
- **TweaksScreen\<Section\>.cpp** - One file per section, each implementing a single `Render*Section()` method

## Architecture Notes

A full-width toggle bar lets users show/hide any combination of sections, each rendered as its own auto-resizing ImGui window. Uses a data-driven design with a static slider-to-Wrapper lookup map and function pointer table for section rendering.

**Auto-hide behavior**: When dragging a slider, all other UI elements fade to alpha=0 while preserving layout, so the user can see the visual effect of the parameter change without UI clutter.
