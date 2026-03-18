# Tech Debt: Duplicate Texture Index Lookup

Source: /external-tech-debt on Engine/Source/Frame/Collections/PointLights

## Changes

### Engine/Source/Frame/Collections/PointLights/PointLightsRender.cpp
- Cache `gpTextureManager->mTextureDescriptors.CrcToIndex(rType.crc)` result in a local variable before line 91, then use it at both line 91 (`f4Params.x`) and line 122 (`rVisibleLayout.uiTextureIndex`) [~5m]

## Verification Notes
- Confirmed: both calls use the same `rType` reference (resolved at line 71) within the same loop iteration
- Change is safe — `rType.crc` and the texture descriptor map do not change between the two calls
