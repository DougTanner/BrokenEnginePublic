# Guard Tiny-Island Crop Epsilon Collapse

## Context

Surfaced during the final audit of the "read Sea Level instead of patching" change (`DataPacker/Source/BakeIslandIntermediates.cpp`, bake version 23). Now that per-island sea-floor depth scales as `fBeachOffsetMeters = fSeaLevelNormalized * elevationMeters`, very small `elevationMeters` values can make the offset smaller than `kfCropEpsilonAboveSeaFloorMeters = 1.0f`. The auto-crop cut line `-fBeachOffsetMeters + kfCropEpsilonAboveSeaFloorMeters` then lands at or above sea level, silently stripping the entire shoreline halo from elevation + AO intermediates.

Worked examples (assuming archetype Sea Level 0.1):

| elevationMeters | fBeachOffsetMeters | cut line | result |
|---|---|---|---|
| 100 m (current Island-1x1) | 10 m | -9 m | OK, 1 m halo preserved |
| 50 m | 5 m | -4 m | OK |
| 20 m | 2 m | -1 m | OK, tight |
| 10 m | 1 m | 0 m | **Beach line trimmed** |
| 5 m | 0.5 m | +0.5 m | **Above sea level — shore band gone** |

No current island hits this — `Engine/Data/Islands/02/Island.json` is 100 m. The bug is latent for future small islands.

## Recommendation

Add a precondition check in `BakeOne` immediately after `fBeachOffsetMeters` is computed:

```cpp
if (fBeachOffsetMeters <= kfCropEpsilonAboveSeaFloorMeters)
{
    throw std::runtime_error(std::format(
        "Island \"{}\" beach offset ({:.2f} m, = Sea Level {} × elevationMeters {:.2f}) is below the crop epsilon ({:.2f} m): the auto-crop would land at or above sea level and strip the shoreline halo. Raise elevationMeters, raise the archetype Sea node's Level, or lower kfCropEpsilonAboveSeaFloorMeters.",
        rIslandFolder.string(), fBeachOffsetMeters, fSeaLevelNormalized,
        dimensions.fElevationMeters, kfCropEpsilonAboveSeaFloorMeters));
}
```

Alternative considered: make `kfCropEpsilonAboveSeaFloorMeters` a fraction of `fBeachOffsetMeters` (e.g., `0.1 * fBeachOffsetMeters`, capped at 1 m). Rejected — couples two independent tunables and complicates the explanation; loud failure is better than silent halo loss.

## Files

- `DataPacker/Source/BakeIslandIntermediates.cpp` — add the check in `BakeOne` right after `fBeachOffsetMeters` is derived (currently ~line 529).

No bake-version bump needed (this is a validation-only addition; it doesn't change byte-level output for any island that passes the check).

## Verification

1. Build DataPacker via `/compile`.
2. Re-run the existing 100 m island bake — must still succeed (offset 10 m > epsilon 1 m).
3. Temporarily edit `Engine/Data/Islands/02/Island.json` to `"elevationMeters": 5`, re-run — must throw the new error. Revert.
