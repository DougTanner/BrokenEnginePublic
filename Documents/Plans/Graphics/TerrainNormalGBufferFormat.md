# Terrain Normal G-Buffer Format Tightening

## Context

`mTerrainNormalTexture` was upgraded from `VK_FORMAT_R8G8B8A8_UNORM` to `VK_FORMAT_R16G16B16A16_SFLOAT` to eliminate visible quantization banding in lighting and the F2 debug viz. The upgrade works correctly but is suboptimal:

1. **Range mismatch**: `TerrainNormal.frag` writes `(0.5 + 0.5 * x_rotated, 0.5 + 0.5 * y_rotated, sqrt(1 - x² - y²), 1.0)` — all values in `[0,1]`. `Terrain.frag` and `ShaderFunctions.h::SampleNormal` decode with `2*x - 1`. The signed `SFLOAT` format has half its representable range (negative values) sitting unused.
2. **Channel waste**: B holds the assembled Z magnitude, A is constant 1.0. Z is also derivable from XY at sample time (already done in `TerrainNormal.frag` from BC5 source), so storing it consumes bandwidth without buying anything readers can't recompute.

## Two Possible Tightenings (pick one, not both)

### Option A — Drop to `R16G16B16A16_UNORM`

- Same 8 B/texel as the current R16G16B16A16_SFLOAT but better suited to `[0,1]` content.
- No shader changes required (encode/decode round-trip already targets `[0,1]`).
- Trivially mechanical; lowest risk.

### Option B — Drop to `R16G16_SFLOAT` and recompute Z + drop encode/decode round-trip

- 4 B/texel (half the bandwidth of current).
- Switch `TerrainNormal.frag` to write the rotated tangent **directly** as signed XY (no `0.5 + 0.5 *` re-encode).
- Switch `Terrain.frag` and `ShaderFunctions.h::SampleNormal` to read XY directly (drop `2*x - 1` decode) and recompute Z = `sqrt(saturate(1 - x² - y²))` at sample time.
- Removes one pair of redundant ALU ops in the rasterization pipeline.
- Requires touching three shaders coherently — coordinate the change.

## Recommendation

**Option A** if the goal is to simply stop wasting the format's negative half — minimal risk, no shader edits.

**Option B** if bandwidth at the terrain-normal G-buffer is meaningful (it's at terrain-normal-multiplier resolution, generally large) — better long-term but needs the three-shader touch coordinated.

## Files to Modify

- `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp:331` — `.format` line
- (Option B only) `Engine/Data/Shaders/Terrain/TerrainNormal.frag` — drop `f2EncodedRG = 0.5f + 0.5f * f2Rot`; write `f2Rot` directly to .rg; drop the .b Z write or change attachment count
- (Option B only) `Engine/Data/Shaders/Terrain/Terrain.frag` — find the `2*x - 1` decode and remove
- (Option B only) `Engine/Data/Shaders/ShaderFunctions.h::SampleNormal` — same decode removal, plus add `sqrt(saturate(1 - x² - y²))` Z reconstruction

## Out of Scope

- Whether to keep using a separate normal G-buffer at all vs. encoding into the elevation/AO targets — that's a larger pipeline question.
