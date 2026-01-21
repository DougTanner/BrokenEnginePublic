# `/Engine/Source/Ui/Screens/`

ImGui-based debug UI screens rendered by ImGuiManager.

## Architecture

Each screen class encapsulates its own state and rendering logic. ImGuiManager owns screen instances and calls their `Render()` methods during the ImGui frame.

## Screens

### TweaksScreen
Multi-section parameter adjustment UI providing runtime control over rendering parameters via Wrapper globals from WrapperBase.h. Uses 2x UI scaling for improved readability.

**Toggle Bar**: Horizontally centered near top of screen with selectable buttons for each section and a global Sun Angle slider spanning the full bar width. Multiple sections can be visible simultaneously.

**Sections** (11 total, defined in `TweakSection` enum):
- **Test**: Test One/Two sliders
- **glTF**: Tone Mapping, Lighting, BRDF, IBL, Sun, Post Lighting
- **Terrain**: Beach (snow, sand, normals) and Rock (height, size, normals)
- **Water Specular**: Normals, Skybox, Height Darken
- **Water Low**: Wave Count (radio buttons: 15/31/63/127/255), Wave, Adjustments, Beach Fade
- **Lighting**: Blur, Combine, Directional
- **Water Lighting**: Specular parameters
- **Shadow**: Feather, Object Shadows
- **Misc**: Island Height, Water Depth, Water Terrain
- **Hex Shield**: Edge, Wave, Direction
- **Smoke**: Decay, Color, Wind/Noise

**Section Windows**: Each visible section renders as its own auto-resize ImGui window with close button. Windows use staggered initial positions to prevent overlap and remember positions during the session.

**Auto-hide behavior**: When dragging a slider, all other UI elements become invisible (alpha=0) while preserving layout, allowing unobstructed scene viewing during parameter adjustment.

**Conditional compilation**: Guarded by `ENABLE_DEBUG_INPUT` define with early return when `game::gpGame->mbShowImGui` is false.
