# Ocean Shader: Tweaks Screen Plan

## Context

All ocean shader sliders (~63 across 7 phases) go into a single new TweakSection **"Water PBR"** with tabbed sub-sections and 2-column layout inside each tab. This replaces the plan of 6 separate sections.

## UI Design

One section toggle button: **"Water PBR"** in the toggle bar. When opened, the window contains:

1. **Tab bar** at the top (`ImGui::BeginTabBar`) with tabs: Color, Normals, Foam, Glitter, Caustics, SSS, Legacy
2. **2-column table** inside each tab (`ImGui::BeginTable("...", 2)`) with ~10-12 sliders per column
3. Uses `fWidthMultiplier = 1.0f` (same as PBR section) since columns halve the width

### Layout Pattern (follows existing PBR section style)

```cpp
void TweaksScreenBase::RenderWaterPBRSection()
{
    static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kWaterPBR);

    if (ImGui::BeginTabBar("WaterPBRTabs"))
    {
        if (ImGui::BeginTabItem("Color"))
        {
            if (ImGui::BeginTable("ColorColumns", 2))
            {
                ImGui::TableNextColumn();
                WrapperSeparatorText("Absorption");
                WrapperSlider("Extinction R", kiSection, 1.0f);
                // ... left column sliders ...

                ImGui::TableNextColumn();
                WrapperSeparatorText("Floor Color");
                WrapperSlider("Floor Albedo R", kiSection, 1.0f);
                // ... right column sliders ...

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Normals"))
        {
            // ... same pattern ...
            ImGui::EndTabItem();
        }
        // ... more tabs ...
        ImGui::EndTabBar();
    }
}
```

## TweakSection Enum

Add ONE value to `TweaksScreenBase.h`:
```cpp
kWaterPBR,  // replaces the planned 6 separate sections
```

## Tab Layouts

### Tab 1: "Color" (Phase 1 + Phase 7)

| Left Column | Right Column |
|-------------|-------------|
| **Absorption** | **Floor Color** |
| Extinction R [0, 20] = 4.5 | Floor Albedo R [0, 0.1] = 0.004 |
| Extinction G [0, 10] = 0.5 | Floor Albedo G [0, 0.1] = 0.016 |
| Extinction B [0, 5] = 0.05 | Floor Albedo B [0, 0.2] = 0.047 |
| Depth Scale [0.1, 10] = 1.0 | |
| | **Scatter** |
| **BRDF** | Scatter R [0, 0.3] = 0.05 |
| Fresnel F0 [0.01, 0.1] = 0.02 | Scatter G [0, 0.3] = 0.15 |
| Roughness [0.01, 1.0] = 0.15 | Scatter B [0, 0.3] = 0.20 |
| | |
| **Height Darken** (existing) | **Ambient** |
| Height Darken Top | Ambient Floor [0, 1] = 0.3 |
| Height Darken Bottom | |
| Height Darken Clamp | |

**Left: 9 sliders. Right: 8 sliders.**

### Tab 2: "Normals" (Phase 2)

| Left Column | Right Column |
|-------------|-------------|
| **Octave Scales** | **Slope Variance** |
| Octave 1 Scale [500, 2000] = 1077 | Slope Variance Enable (bool) = true |
| Octave 2 Scale [50, 500] = 154 | Slope Variance Bias [0, 0.5] = 0.05 |
| Octave 3 Scale [5, 100] = 22 | Slope Variance Scale [0.1, 5] = 1.0 |
| Octave 4 Scale [0.5, 20] = 3.1 | |
| Octave Speed Ratio [0.5, 3] = 1.2 | **Anti-Tiling** |
| | Anti-Tile Strength [0, 1] = 0.3 |
| **Existing Normals** (from Water Specular) | Anti-Tile Scale [0.001, 0.1] = 0.01 |
| Sampled Normals Size | |
| Sampled Normals Size Mod | **Skybox** (existing, from Water Specular) |
| Sampled Normals Speed | Sun Bias |
| | Normal Soften |
| | Normal Blend Wave |
| | Skybox Lod |

**Left: 8 sliders. Right: 7 sliders.**

### Tab 3: "Foam" (Phase 3)

| Left Column | Right Column |
|-------------|-------------|
| **Foam Sources** | **Foam Appearance** |
| Foam Enable (bool) = true | Foam Intensity [0, 3] = 1.0 |
| Foam Height Min [-0.5, 1] = 0.05 | Foam Tex Scale [0.001, 0.1] = 0.02 |
| Foam Height Max [0, 2] = 0.15 | Foam Scroll Speed [0, 2] = 0.3 |
| Foam Shore Min [0, 5] = 0.0 | Foam Tint [0.5, 1] = 0.9 |
| Foam Shore Max [0, 10] = 2.0 | |
| Foam Slope Threshold [0, 1] = 0.4 | |

**Left: 6 sliders. Right: 4 sliders.**

### Tab 4: "Glitter" (Phase 4)

Single column (only 5 sliders, doesn't need 2 columns):

| Sliders |
|---------|
| Glitter Enable (bool) = true |
| Glitter Threshold [0.9, 0.999] = 0.97 |
| Glitter Intensity [0, 10] = 3.0 |
| Glitter Noise Scale [0.1, 10] = 2.0 |
| Glitter Noise Strength [0, 0.5] = 0.1 |

### Tab 5: "Caustics" (Phase 5)

| Left Column | Right Column |
|-------------|-------------|
| **Depth Range** | **Layer 2** |
| Caustics Enable (bool) = true | Caustics Scale 2 [0.001, 0.1] = 0.025 |
| Caustics Depth Min [0, 5] = 0.5 | Caustics Speed 2 [0, 2] = 0.3 |
| Caustics Depth Max [1, 20] = 8.0 | |
| | **Output** |
| **Layer 1** | Caustics Intensity [0, 5] = 1.5 |
| Caustics Scale 1 [0.001, 0.1] = 0.015 | Caustics Sharpness [1, 10] = 3.0 |
| Caustics Speed 1 [0, 2] = 0.4 | |

**Left: 5 sliders. Right: 4 sliders.**

### Tab 6: "SSS" (Phase 6)

| Left Column | Right Column |
|-------------|-------------|
| **Height** | **Color** |
| SSS Enable (bool) = true | SSS Color R [0, 0.5] = 0.05 |
| SSS Intensity [0, 3] = 0.8 | SSS Color G [0, 0.5] = 0.3 |
| SSS Height Base [-0.5, 0.5] = 0.0 | SSS Color B [0, 0.5] = 0.2 |
| SSS Height Scale [0.5, 20] = 5.0 | |
| SSS Sun Power [1, 10] = 4.0 | |

**Left: 5 sliders. Right: 3 sliders.**

### Tab 7: "Legacy" (Phase 7)

| Left Column | Right Column |
|-------------|-------------|
| **Color Zones** | **Depth LUT** |
| Color Zone Enable (bool) = true | Depth LUT Blend [0, 1] = 0.0 |
| Color Zone Frequency [0.0001, 0.01] = 0.002 | Depth LUT Feather [0.01, 5] = 1.0 |
| Color Zone Amount [0, 2] = 0.5 | |
| Color Zone Tint R [0.5, 1.5] = 0.8 | **Depth Reflection** (existing) |
| Color Zone Tint G [0.5, 1.5] = 1.1 | Depth Reflection Feather |
| Color Zone Tint B [0.5, 1.5] = 1.2 | |

**Left: 6 sliders. Right: 3 sliders.**

## Existing Sections: What Happens

| Existing Section | Action |
|-----------------|--------|
| Water Specular | **Remove** - sliders migrated into Water PBR tabs (Normals + Color) |
| Water Low | **Keep unchanged** - vertex shader wave params |
| Water Medium | **Keep unchanged** - vertex shader wave params |
| Water Lighting | **Keep unchanged** - point/area light specular on water |

## Obsolete Sliders (removed entirely)

From Water Specular -> replaced by Ward BRDF:
- Skybox 1 / Skybox 1 Power / Skybox 2 / Skybox 2 Power / Skybox 3 / Skybox 3 Power

From GlobalLayout -> replaced by Beer-Lambert:
- fWaterColorNoiseFrequency / fWaterColorNoiseAmount (replaced by Color Zones)
- fWaterColorBottom / fWaterColorHeightInv (replaced by extinction)
- fWaterDepthColorFeather (replaced by extinction)
- fWaterFresnel (replaced by Fresnel F0)
- fWaterDirectional (replaced by Ward BRDF)
- fWaterDepthLutFeather (replaced by Depth LUT Feather)

## Implementation Notes

### Tab bar + active slider alpha fade

The existing `mActiveSlider` / alpha=0 pattern works transparently inside tabs. When a slider is active, non-active sliders and tab bar items will fade since they're wrapped in the same push/pop pattern. The tab bar itself should be excluded from fading - wrap it before the alpha check or handle it separately.

**Consideration:** When a slider is active and the window fades decorations, the tab bar should remain visible so the user knows which tab they're on. Solution: render the tab bar outside the alpha-fade scope, or push alpha=1.0 explicitly around `BeginTabBar/EndTabBar`.

### File organization

One file: `TweaksScreenWaterPBR.cpp` containing `RenderWaterPBRSection()` with all 7 tabs. This is large (~200 lines) but keeps all water fragment shader tweaks in one place. If it grows unwieldy, the tab contents could be extracted into helper methods in the same file.

### Incremental implementation

Each phase adds its tab:
- Phase 1: Create file with "Color" tab only
- Phase 2: Add "Normals" tab
- Phase 3: Add "Foam" tab
- Phase 4: Add "Glitter" tab
- Phase 5: Add "Caustics" tab
- Phase 6: Add "SSS" tab
- Phase 7: Add "Legacy" tab + migrate existing sliders from Water Specular

## Files Modified

- `TweaksScreenBase.h` - Add `kWaterPBR` to TweakSection, add `RenderWaterPBRSection()` declaration
- `TweaksScreenBase.cpp` - Add "Water PBR" to `kpcSectionNames[]` and `kRenderSectionFunctions[]`
- `TweaksScreenWaterPBR.cpp` - New file (the one big render method with tabs)
- `TweaksSliderMap.cpp` - Register all new sliders
- `TweaksScreenWaterSpecular.cpp` - Eventually remove (sliders migrated) or keep as stub
- `.vcxproj` / `.vcxproj.filters` - Register new .cpp file
