# Switch island Color BC7 from `VK_FORMAT_BC7_UNORM_BLOCK` to `VK_FORMAT_BC7_SRGB_BLOCK`

## Context

After the Color/Normals color-space cleanup (May 2026), the island's `Color.BC7_UNORM_BLOCK` file contains **sRGB-encoded bytes** (Gaea writes PNG8 sRGB → DataPacker loads bytes as-is → BC7 encoder stores those bytes). The shader at `Engine/Data/Shaders/Terrain/Terrain.frag:57` samples those bytes and feeds them straight into the lighting math, which is operating on sRGB-encoded values as-if-linear. The result looks plausibly correct because everything downstream (the swapchain) assumes the shader output is sRGB-encoded, but the lighting computations themselves are incorrect — multiplications and lerps in sRGB space don't correspond to physical light mixing.

The textbook-correct fix is to declare the color image `VK_FORMAT_BC7_SRGB_BLOCK`. The Vulkan sampler then performs hardware sRGB→linear decode on every tap, so `texture(colorTextureSampler, ...)` yields true linear values, the lighting math runs in linear space, and the final write to the sRGB swapchain re-encodes back. Free perf, correct math.

This was deliberately deferred from the parent cleanup because the blast radius is larger: every shader site that samples the island color texture and every C++ site that pipes the format through descriptor-set/image-view creation needs to be audited at once.

## Recommended approach

### 1. DataPacker side

- `DataPacker/Source/ExportJobs/ExportIsland.cpp:57` — `texture.Save(rInputPath / kpcIslandColor, VK_FORMAT_BC7_UNORM_BLOCK, true)` → `VK_FORMAT_BC7_SRGB_BLOCK`.
- `DataPacker/Source/ExportJobs/ExportIsland.h:13` — `kpcIslandColor` extension `"Color.BC7_UNORM_BLOCK"` → `"Color.BC7_SRGB_BLOCK"`.
- `DataPacker/Source/ExportJobs/ExportIsland.h:31` — bump `GetVersion()`.
- `DataPacker/Source/Main.cpp:48, 94` — add `.BC7_SRGB_BLOCK` → `VK_FORMAT_BC7_SRGB_BLOCK` mapping and corresponding switch case for the migration codepath.
- `DataPacker/Source/Texture.cpp:274, 393, 410, 514-515` — add `VK_FORMAT_BC7_SRGB_BLOCK` alongside `VK_FORMAT_BC7_UNORM_BLOCK` everywhere. The BC7 encoder itself is format-agnostic (UNORM vs SRGB is consumer-side metadata) so `ToBc7` is unchanged.

### 2. Engine side

- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp:202` — add `VK_FORMAT_BC7_SRGB_BLOCK` to the `bCompressed` whitelist.
- Audit `Engine/Source/Graphics/Managers/TextureManager.h` and any texture-loading entry point that creates `VkImage`/`VkImageView` for island colors — confirm the descriptor view inherits the on-disk format (already keyed on the file extension via the kpcIslandColor constant). If any view explicitly hardcodes `VK_FORMAT_BC7_UNORM_BLOCK` for island color, change it.
- `Engine/Source/Debug/EnumToString.h:242` already maps `VK_FORMAT_BC7_SRGB_BLOCK` — no change.

### 3. Shader side — appearance audit

After the switch, `texture(colorTextureSampler, ...)` returns *linear* values instead of *sRGB-encoded* values. Every consumer must be revisited:

- `Engine/Data/Shaders/Terrain/Terrain.frag:57` — `texture(colorTextureSampler, ...).xyz` is now linear. Downstream lighting math (`fSnowPercent` thresholds, `fRockPercent` thresholds, beach detection) currently uses hardcoded color constants like `vec3(1.0, 1.0, 1.0)` and `vec3(210/255, 210/255, 210/255)` that were tuned against sRGB-encoded values. These thresholds will shift — re-tune.
- `Engine/Data/Shaders/Terrain/TerrainColor.frag` — G-buffer prepass that samples the per-island color into the composite RTT. The composite RTT's format also needs sRGB-or-linear consideration. If the composite stays `VK_FORMAT_R8G8B8A8_UNORM`, the linear values get quantized to 8 bits losing precision; switching the composite to `R8G8B8A8_SRGB` keeps the hardware roundtrip.

### 4. On-disk migration

The existing `MigrateLegacyIntermediates()` in `Main.cpp` handles legacy BC7 files. Confirm the migration path correctly handles both `.BC7_UNORM_BLOCK` (legacy) and `.BC7_SRGB_BLOCK` (new) intermediates, or that the cache-invalidation `GetVersion()` bump forces a clean re-export.

## Critical files

- `DataPacker/Source/ExportJobs/ExportIsland.cpp`
- `DataPacker/Source/ExportJobs/ExportIsland.h`
- `DataPacker/Source/Main.cpp`
- `DataPacker/Source/Texture.cpp`
- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp`
- `Engine/Data/Shaders/Terrain/Terrain.frag`
- `Engine/Data/Shaders/Terrain/TerrainColor.frag`

## Verification

1. Re-bake island 02. Confirm `Engine/Data/Islands/02/Color.BC7_SRGB_BLOCK` exists.
2. Open the new `Color.jpg` sidecar — should still match `Color.png` (the bake-side change doesn't affect the sidecar).
3. Run `BrokenEngineSandbox`. Terrain should look *visually similar* to the post-cleanup output (the sRGB→linear→shader→sRGB roundtrip cancels for trivial pass-through shading) but lighting falloff and snow/rock/beach blending will look more physically plausible.
4. Re-tune the hardcoded color thresholds in `Terrain.frag` if the visual transitions land in the wrong places after the linear→sRGB shift.

## Why deferred

The parent cleanup (`color-exr-color-jpg-extremely-noble-badger.md`) explicitly bounded scope to "fix the washed-out JPG" — this BC7 switch is *correct* but is a meaningfully larger change that needs a real shader-side appearance pass. The user directive at the start of that work was "only change color spaces when we're sure it must be done."
