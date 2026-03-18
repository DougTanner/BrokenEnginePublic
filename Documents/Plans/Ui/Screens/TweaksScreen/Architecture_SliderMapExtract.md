# Architecture: Slider Map Extraction

Source: /external-architecture-review on Engine/Source/Ui/Screens/TweaksScreen

## Changes

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreen.h
- Declare `static std::unordered_map<std::string_view, Wrapper*>& GetSliderMap();` as a private static member of TweaksScreen (currently a file-local static function in TweaksScreen.cpp) [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenSliderMap.cpp (new file)
- Create new file containing the `TweaksScreen::GetSliderMap()` definition (currently TweaksScreen.cpp lines 51-302) [~5m]
- Include only `TweaksScreen.h` (Wrapper globals are available via PCH; Game.h is NOT needed)
- Keep the `ScopedSuppressAllocationTracking` and heap allocation comment

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreen.cpp
- Remove `GetSliderMap()` function definition (lines 51-302) [~2m]
- Update `WrapperSlider` call site (line 318) to use `TweaksScreen::GetSliderMap()` instead of `GetSliderMap()` [~1m]

### BrokenEngineSandbox .vcxproj
- Add TweaksScreenSliderMap.cpp to the appropriate filter [~2m]

## Verification Notes

Linkage: `GetSliderMap()` is currently `static` (file-local) in TweaksScreen.cpp. Moving to a separate .cpp requires changing linkage. Making it a private static class member is the cleanest approach — preserves encapsulation, avoids internal headers. TweaksScreen.cpp is 517 lines; after extraction it drops to ~265 lines (well under 500 soft limit). The 252-line function also exceeds the 100-line function guideline.