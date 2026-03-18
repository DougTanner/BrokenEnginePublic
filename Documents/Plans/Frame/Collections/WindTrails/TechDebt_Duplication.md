# Tech Debt: Code Duplication

Source: /external-tech-debt on Engine/Source/Frame/Collections/WindTrails

## Changes

### Engine/Source/Frame/Collections/WindTrails/WindTrailsRender.cpp
- Extract the stale-position `erase_if` lambda (lines 49-63) into a shared template utility (e.g., `EraseStaleRenderState` in Collection.h or a render utilities header) parameterized on the collection field accessor [~15m]

### Engine/Source/Frame/Collections/SmokeTrails/SmokeTrailsRender.cpp
- Replace the stale-position `erase_if` lambda with the shared `EraseStaleRenderState` template [~5m]

## Verification Notes

### MultiCopy utility -- REMOVED
The original plan proposed a `MultiCopy` template to replace manual `memcpy` in `AllocateAndCopy` across 11+ collections. Verification found this is **wrong**: collections intentionally copy only a subset of members (those that persist frame-to-frame), not all `Members()`. A blind `MultiCopy(Members())` would copy fields that are intentionally left for `Update()` to populate. Removed as low-value churn that would obscure intent.

### EraseStaleRenderState extraction -- VALID
The `erase_if` lambda in WindTrailsRender.cpp (lines 52-62) and SmokeTrailsRender.cpp (lines 57-67) are structurally identical, differing only in which collection's `idToIndexMap` is checked. Clean deduplication opportunity.
