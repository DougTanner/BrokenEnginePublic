# Refactor: Render Function Simplification

Source: /external-architecture-review on Engine/Source/Frame/Collections/WindTrails

## Changes

### Engine/Source/Frame/Collections/WindTrails/WindTrailsRender.cpp
- Extract the per-trail quad-building loop body (lines 97-165) into a static helper function (e.g., `BuildWindTrailQuad`) that takes the trail data and writes a single `QuadLayout` entry. This reduces `Render()` from ~97 lines to ~25 lines and makes the geometry logic independently readable [~15m]

## Verification Notes

### BuildWindTrailQuad extraction -- VALID WITH CAVEATS
- **Line numbers confirmed**: The for-loop at line 96 iterates `idToIndexMap`, and lines 97-165 are the loop body that builds one quad per trail. Line count is accurate.
- **Function size**: `Render()` is ~97 lines (lines 77-173). The CLAUDE.md guideline says "aim for 50-100 lines" which makes this a soft violation. Extracting the loop body is reasonable.
- **Parameter count concern**: The extracted function would need many parameters: `vecPosition`, `fIntensity`, `fWidth`, `fLengthMultiplier`, `previousPositions` map + id, `pQuadLayouts`, and `siRendered`. This is 7-8 parameters. Consider whether a struct or keeping it inline is cleaner. The function also has two `continue` early-returns (visibility culling at line 106, distance check at line 125) which would need to become a return value indicating "skip this trail" -- adding complexity.
- **Recommendation**: Valid refactor, but the early-return handling means the extracted function should return a bool (true = quad written, false = skipped), and the caller increments `siRendered` only on true. Alternatively, pass `siRendered` by reference. Either way, this is a net improvement for readability.
