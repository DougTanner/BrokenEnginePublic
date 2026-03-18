# Tech Debt: Code Duplication

Source: /external-tech-debt on Engine/Source/Ui/Screens/TweaksScreen

## Changes

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreen.h
- Add a private helper method `void RenderWaveCountRadioButtons(Wrapper& rCountWrapper)` to deduplicate the radio button wave count logic shared between WaterLow and WaterMedium sections [~5m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreen.cpp
- Implement `RenderWaveCountRadioButtons` — extract the shared radio button loop from WaterLow/WaterMedium (identical pattern: Text, SameLine, loop over {15,31,63,127,255}, RadioButton, SameLine, NewLine) [~5m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterLow.cpp
- Replace radio button block (lines 11-25) with call to `RenderWaveCountRadioButtons(gLowCount)` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterMedium.cpp
- Replace radio button block (lines 11-25) with call to `RenderWaveCountRadioButtons(gMediumCount)` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenTerrain.cpp
- Add `static constexpr int kiSection = static_cast<int>(TweakSection::kTerrain);` at top and replace all 16 `static_cast<int>(TweakSection::kTerrain)` arguments with `kiSection` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterSpecular.cpp
- Add `static constexpr int kiSection = static_cast<int>(TweakSection::kWaterSpecular);` at top and replace all 19 `static_cast<int>(TweakSection::kWaterSpecular)` arguments with `kiSection` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterLow.cpp
- Add `static constexpr int kiSection = static_cast<int>(TweakSection::kWaterLow);` at top and replace all 12 `static_cast<int>(TweakSection::kWaterLow)` arguments with `kiSection` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterMedium.cpp
- Add `static constexpr int kiSection = static_cast<int>(TweakSection::kWaterMedium);` at top and replace all 8 `static_cast<int>(TweakSection::kWaterMedium)` arguments with `kiSection` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp
- Add `static constexpr int kiSection = static_cast<int>(TweakSection::kLighting);` at top and replace all 17 `static_cast<int>(TweakSection::kLighting)` arguments with `kiSection` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWaterLighting.cpp
- Add `static constexpr int kiSection = static_cast<int>(TweakSection::kWaterLighting);` at top and replace all 10 `static_cast<int>(TweakSection::kWaterLighting)` arguments with `kiSection` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenShadow.cpp
- Add `static constexpr int kiSection = static_cast<int>(TweakSection::kShadow);` at top and replace all 19 `static_cast<int>(TweakSection::kShadow)` arguments with `kiSection` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenMisc.cpp
- Add `static constexpr int kiSection = static_cast<int>(TweakSection::kMisc);` at top and replace all 6 `static_cast<int>(TweakSection::kMisc)` arguments with `kiSection` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenHexShield.cpp
- Add `static constexpr int kiSection = static_cast<int>(TweakSection::kHexShield);` at top and replace all 11 `static_cast<int>(TweakSection::kHexShield)` arguments with `kiSection` [~2m]

### Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenTest.cpp
- Add `static constexpr int kiSection = static_cast<int>(TweakSection::kTest);` at top and replace both `static_cast<int>(TweakSection::kTest)` arguments with `kiSection` [~1m]

## Verification Notes

Radio button extraction: Verified — WaterLow lines 11-25 and WaterMedium lines 11-25 are identical except for `gLowCount` vs `gMediumCount`. Clean DRY improvement.

kiSection extraction: All occurrence counts verified correct. This is cosmetic (compiler already folds the casts to integer literals) but improves consistency — 4 files already use this pattern, 10 do not. Low priority.
