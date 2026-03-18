# Tech Debt: Render Optimization

Source: /external-tech-debt on Engine/Source/Frame/Collections/AreaLights

## Changes

### Engine/Source/Frame/Collections/AreaLights/AreaLightsRender.cpp
- Cache `gpTextureManager->mTextureDescriptors.CrcToIndex(rType.crc)` result in a local variable at ~line 78 (after `rType` is loaded), then reuse at lines 116 and 138 instead of calling CrcToIndex twice per element [~5m]

## Verification Notes
- **Correctness**: File path and line numbers verified. `CrcToIndex(rType.crc)` is called at lines 116 and 138 with identical arguments inside the per-element loop (line 67). `rType` is a const ref loaded at line 77; caching after that line is the correct placement.
- **Benefit**: Valid optimization. Eliminates a redundant hash-based lookup per loop iteration in a hot render path. No risk introduced; the result is deterministic within the iteration since `rType.crc` does not change.
- **Completeness**: The change description is specific and actionable: cache the return value in a local variable after line 77, replace usages at lines 116 and 138. Note that line 116 needs a `static_cast<uint32_t>` on the cached value and line 138 assigns to a float, so the cached variable should be the raw `int64_t` return value with casts at each use site.
