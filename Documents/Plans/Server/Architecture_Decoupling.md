# Architecture: Render Optimization & Entity Count Accessor

Source: /external-architecture-review on Engine/Source/Server

## Changes

### Projects/BrokenEngineSandbox/Source/Frame/Frame.h
- Add a `TotalEntityCount()` method to `game::FrameInterpolate` that sums pPlayers->iCount + pSpaceships->iCount + pBlasters->iCount + pMissiles->iCount + pTargets->iCount + explosions.iCount. This is a game-level struct since 5 of 6 fields are game-specific [~10m]

### Engine/Source/Server/ServerDisplay.cpp
- Replace the 6-field iCount summation at lines 278-283 (PaintServerDisplay grid labels) with `rFrame.interpolate.TotalEntityCount()` call [~2m]
- Pre-compute active-cell lookup (e.g., flat_set or sorted vector) and client-per-cell counts before the grid rendering loop (lines 214-301) to eliminate O(n) linear searches per cell. Current code does O(cells * activeCoords) at lines 226-233 and O(cells * clients) at lines 235-241 [~15m]

## Verification Notes
- FrameInterpolate file path corrected from Engine/Source/Frame/FrameInterpolate.h to Projects/BrokenEngineSandbox/Source/Frame/Frame.h
- "Remove 5 collection includes" item deleted — includes are still required by ServerUpdateDisplayStats() which dereferences collection pointers for per-type counts (lines 37-42)
- "Replace summation at lines 37-42" item deleted — that code needs per-type counters individually, not a total
- "Extract PaintGridMap()" item removed (duplicate of TechDebt_Duplication.md)
- TotalEntityCount() has only one call site (line 278); benefit is readability and future-proofing if collections change
- Pre-compute optimization: performance gain is negligible for a debug GDI display, but readability improves
