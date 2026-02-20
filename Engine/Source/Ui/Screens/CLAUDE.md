# `/Engine/Source/Ui/Screens/` - Engine Debug UI Screens

ImGui-based debug overlays rendered by ImGuiManager. Game-specific menu screens are located in the game project's Ui/Screens folder.

## Architecture

Each screen class encapsulates its own state and rendering logic. ImGuiManager owns screen instances and calls their `Render()` methods during the ImGui frame. Screens check game state and early-return when not visible.

## TweaksScreen

Multi-section parameter adjustment UI providing runtime control over rendering parameters via Wrapper globals from WrapperBase.h. Uses 1.5x UI scaling for improved readability.

**Toggle Bar**: Full-width bar at top of screen with selectable buttons for each section and a double-height Sun Angle slider spanning the full bar width. Multiple sections can be visible simultaneously. Section windows use a fixed initial X position and are positioned at `mfToggleBarBottom` vertically.

**Sections** (14 total, defined in `TweakSection` enum):
- **Test**: Test One/Two sliders
- **Pbr (Model)**: 2-column ImGui table layout. Left column: Engine Variables (Sun), BRDF (Diffuse/Specular with multiplier and power each), Tone Mapping (Exposure, Gamma), Post Lighting (Lighting Specular, Lighting with power each). Right column: IBL (Ambient, Diffuse/Specular with multiplier and power each, Shadow Blend, Ambient Color Blend, Cubemap Lod, Shadow Floor), Smoke, Emissive
- **Terrain**: Beach (snow, sand, normals) and Rock (height, size, normals)
- **Water Specular**: Normals, Skybox, Height Darken
- **Water Low**: Wave Count (radio buttons), Wave, Adjustments, Beach Fade
- **Water Medium**: Wave Count (radio buttons), Wave, Adjustments
- **Lighting**: Blur, Combine, Directional
- **Water Lighting**: Specular parameters
- **Shadow**: Feather, Object Shadows
- **Misc**: Island Height, Water Depth, Water Terrain
- **Hex Shield**: Edge, Wave, Direction
- **Smoke**: 2-column ImGui table layout. Left column: Decay, Color, Noise, Wind Displacement (smoke-wind interaction and swirl), Smoke Object Height. Right column: Trails (quantity, width current/previous, length, length jitter, side jitter, intensity falloff, follow)
- **Wind**: Time & Global (time scale, threshold low/high), Propagation as 2-column ImGui table with Low/High pairs mixed by magnitude factor (advection, swirl scale/amount/speed, vorticity confinement, decay, momentum, diffusion), Particles (wind strength, intensity decay)
- **Wind Dep**: Per-entity deposit subsections for Player, Spaceships (each with width, intensity, length multiplier), Player Blasters (width, intensity, length multiplier), Spaceships Blasters (width, intensity), Explosions (width, intensity)

**WrapperSlider**: Renders a Wrapper-backed slider with auto-hide behavior, configurable width multiplier (default 2x, Pbr and Wind sections use 1x for table columns).

**Auto-hide behavior**: When dragging a slider, all other UI elements become invisible (alpha=0) while preserving layout. Window decorations also become transparent when the active slider is in that section.

**Conditional compilation**: Guarded by `if constexpr (kbEnableDebugInput)` with early return when `game::gpGame->mbShowImGui` is false.

## See Also

- [../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md) - Game-specific menu screens
