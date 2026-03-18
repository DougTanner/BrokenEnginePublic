# Architecture: Render Function Split

Source: /external-architecture-review on Engine/Source/Frame/Collections/SmokeTrails

## Changes

### Engine/Source/Frame/Collections/SmokeTrails/SmokeTrailsRender.cpp
- Extract quad geometry calculation from Render() (lines 142-168) into a static helper function, e.g. `BuildTrailQuad()` [~15m]
  - Inputs: vecPosition, vecSmoothedPosition, fWidth, fStartTime, fCurrentTime, fJitterOne, fJitterTwo, sRandomEngine&
  - Outputs: four XMVECTOR corner positions (vecPointOne through vecPointFour), fLength, bool indicating whether to skip
  - Tweak globals (gSmokeTrailsWidthCurrent, gSmokeTrailsWidthPrevious, gSmokeTrailsLength, gSmokeTrailsLengthJitter) accessed directly
  - This reduces Render() from 113 lines to ~85 lines and isolates the geometry math

## Verification Notes
- Render() line count verified at 113 lines (lines 81-193), 13 lines over the soft 100-line guideline
- Parameter count is high (8 non-global inputs + outputs) which partially offsets the readability benefit
- fLength is computed inside the extraction range (line 161), not an input; fLengthScale is derived from vecToSmoothed (line 147)
- Benefit is modest — consider whether extraction is justified vs. the parameter complexity
