# Bugfix: `gWaterDepthColorFloor` Slider Orphaned in Water Tweaks

## Context

`gWaterDepthColorFloor` is a fully wired tweak parameter — declared in `Engine/Source/Ui/WaterWrappersBase.h`, defined in `Engine/Source/Ui/WaterWrappersBase.cpp`, populated into `GlobalLayout::fWaterDepthColorFloor` by `PopulateWaterParameters` in `Engine/Source/Graphics/Render/GlobalUniforms.cpp:357`, and consumed by `Water.frag:187` as the lower bound of the depth-color mix clamp. It is also registered in the `TweaksSliderMap` at `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp:95`.

What is missing: the `WrapperSlider("Water Depth Color Floor", kiSection);` call in the "Depth Color" subsection of `RenderWaterSection`. The value is therefore unreachable from the tweak UI — only its default (0.2f) is ever applied at runtime. The `kbDebugInput` first-open drift audit (described in `Engine/Source/Ui/Screens/TweaksScreen/CLAUDE.md`) would flag this as an orphan map entry in debug builds.

This was surfaced during the audit of the `gWaterDepthLutFadePower` / `gWaterDepthLutFadeIntensity` session — pre-existing latent issue not caused by that change.

## Design

Add one line to `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp` inside the `RenderWaterSection` "Depth Color" subsection, placed to match the existing wrapper declaration order in `WaterWrappersBase.{h,cpp}` (between `Water Depth Color Feather` and `Water Color Bottom`):

```cpp
WrapperSeparatorText("Depth Color");
WrapperSlider("Water Depth Lut Feather", kiSection);
WrapperSlider("Water Depth Lut Fade Power", kiSection);
WrapperSlider("Water Depth Lut Fade Intensity", kiSection);
WrapperSlider("Water Depth Color Feather", kiSection);
WrapperSlider("Water Depth Color Floor", kiSection);   // <-- add
WrapperSlider("Water Color Bottom", kiSection);
WrapperSlider("Water Color Height", kiSection);
```

While in there, do a quick `grep` of `TweaksSliderMap` entries vs `WrapperSlider` calls across all `TweaksScreen*.cpp` per-tab files to confirm no other orphan map entries exist. If any do, surface them as separate follow-up plans (do not silently fix here — each missing slider is its own scope question).

## Critical files

- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp` — add `WrapperSlider` call
- (audit pass only — no edits) `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreen*.cpp` siblings

## Out of scope

- Tuning `gWaterDepthColorFloor`'s default, min, or max — only exposing the existing slider.
- Renaming or relocating the slider within sections.
- Touching `gWaterDepthColorFloor`'s CPU population or shader consumer — both are working.
- Fixing any other orphan map entries discovered during the audit pass — each becomes its own plan.

## Notes

Single-line addition, compile-checked. Default-value behavior unchanged (slider just becomes reachable from the UI). Risk is bounded to UI presentation; runtime water rendering is unaffected by the patch itself.
