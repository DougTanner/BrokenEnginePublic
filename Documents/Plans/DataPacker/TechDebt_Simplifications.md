# Tech Debt: Simplifications

Source: /external-tech-debt on DataPacker/Source

## Changes

### DataPacker/Source/Texture.h
- Make `PixelToUint32`, `ToBc4`, `ToBc7`, `ToR8G8B8A8`, `ToR16` static member functions (lines 32-36) — they don't access `this` [~5m]
- Remove `iHeight` parameter from `PixelToUint32` declaration (line 32) — parameter is `[[maybe_unused]]` and never read [~5m]

### DataPacker/Source/Texture.cpp
- Update `PixelToUint32` definition to remove `iHeight` parameter (line 265) and update all call sites (lines 312-315, 335) [~5m]
- In `Downsize` (lines 255-262), replace loop-erase with single range-erase: `mData.erase(mData.begin(), mData.begin() + iLevels)`. Preserve the width/height halving — compute directly as `miWidth /= (1ll << iLevels); miHeight /= (1ll << iLevels);` with assertions before the erase [~5m]

## Verification Notes
- Making methods static is low priority (cosmetic correctness, no functional change), but signals intent clearly.
- Downsize range-erase: the original loop also halves miWidth/miHeight and asserts even dimensions on each iteration. The replacement must preserve these semantics — either assert all intermediate levels have even dimensions in a separate loop, or compute the final dimensions directly.
