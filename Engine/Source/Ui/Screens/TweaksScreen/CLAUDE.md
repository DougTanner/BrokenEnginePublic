# `/Engine/Source/Ui/Screens/TweaksScreen/` - Tweaks Parameter UI

## Overview

Multi-section ImGui parameter adjustment screen for runtime control over rendering parameters via Wrapper globals. Uses a data-driven design with a static slider-to-Wrapper lookup map and function pointer table for section rendering. Guarded by `if constexpr (kbEnableDebugInput)` with an early return when the ImGui overlay is hidden.

## File Organization

- **TweaksScreen.h** - Class declaration with `TweakSection` enum (14 sections) and all render method signatures
- **TweaksScreen.cpp** - Core logic: constructor, `Render()`, `RenderToggleBar()`, `RenderSectionWindow()`, `WrapperSlider()`, `WrapperSeparatorText()`, section name/function pointer tables, and the static slider-to-Wrapper lookup map
- **TweaksScreen\<Section\>.cpp** - One file per section (14 total), each implementing a single `Render*Section()` method: Test, Pbr, Terrain, WaterSpecular, WaterLow, WaterMedium, Lighting, WaterLighting, Shadow, Misc, HexShield, Smoke, Wind, WindDeposits

## Architecture Notes

**Toggle bar and sections**: A full-width toggle bar at the top of the screen lets users show/hide any combination of the 14 sections. Each section opens as its own auto-resizing ImGui window. Some sections (Pbr, Smoke, Wind Propagation) use 2-column ImGui table layouts for denser parameter display.

**Auto-hide behavior**: When dragging a slider, all other UI elements fade to alpha=0 while preserving layout, so the user can see the visual effect of the parameter change without UI clutter. Window decorations also go transparent for the active section.
