# Architecture: Recording Branch Inconsistency

Source: /external-architecture-review on Engine/Source/Frame/Collections/Billboards

## Changes

### Engine/Source/Frame/Collections/Billboards/BillboardsRender.cpp
- Remove `if constexpr (kbEnableRecording)` branch in `EndRender()` (lines 122-129) and replace with unconditional `WriteIndirectBuffer(iCommandBuffer, siRendered)` to match PointLights, Puffs, and SmokeTrails [~5m]

## Notes

Billboards is the only collection with this branch. Since `kbEnableRecording` is `false` in `Pch.h`, runtime behavior is already identical. The branch creates an unnecessary divergence from the pattern used by all other renderable collections.

Only apply this change if the Billboards collection is being kept. If the collection is being removed (see TechDebt_DeadCode.md), this plan is superseded.

## Verification Notes

Verified against source. Lines 122-129 of BillboardsRender.cpp confirmed to contain the `if constexpr (kbEnableRecording)` branch. All 7 other renderable collections (AreaLights, PointLights, Puffs, SmokeTrails, HexShields, WindTrails, WindRadials) use unconditional `WriteIndirectBuffer(iCommandBuffer, siRendered)`. Inconsistency is real. Superseded if TechDebt_DeadCode.md results in full collection removal.
