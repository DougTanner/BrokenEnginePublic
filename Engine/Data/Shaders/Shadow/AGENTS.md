# Engine/Data/Shaders/Shadow - Top-Down Shadow Compute Pipelines

## Overview

Two independent compute chains post-process top-down (orthographic, world-XY-mapped) shadow data for the RTS camera:

- **Terrain chain**: `Shadow.comp` (ray-march sun occlusion over the island elevation raster) → `ShadowBlurH/V.comp` (separable Gaussian) → `ShadowTemporal.comp` (EMA history blend, in place) → copy to a history texture for next frame.
- **Object chain**: `ObjectShadowsBlurH/V.comp` (separable Gaussian over the top-down object-coverage raster).

Both final textures are sampled by the Terrain/Water fragment shaders; Model samples only the terrain shadow texture. All shadow uniforms are populated once per frame by `PopulateShadowParameters` (`Engine/Source/Graphics/Render/GlobalUniforms.cpp`) — the CPU half of this subsystem and the place to read for the full texel-grid math; the shaders only consume area uniforms.

## Architecture Notes

- **Inverse coverage is load-bearing**: the terrain shadow texture stores `1 - shadow` (1.0 = fully lit) precisely so its clamp-to-border-white sampler makes any off-texture sample (fast zoom-out outrunning the texel ramp) degrade to "no shadow" instead of artifacting.
- **Rate-limited texel grid + centered visible window**: the texel world size derives CPU-side from the camera's rate-limited shadow-texel eye height — snap-stable under XY pan, rescaled only on the slow crawl while tracking a zoom; texels coarsen with zoom instead of cropping coverage, so the window holds constant on-screen pixels at any settled height. All four terrain dispatches early-out outside the visible-window uniforms expanded by `kiShadowWindowMargin`; the margin guarantees every bilinear read inside the window lands on current-frame, fully-blurred, temporally-resolved texels. `Shadow.comp` differs outside the window: it writes the unshadowed lit value rather than skipping, so blur taps straddling the window edge read consistent data.
- **Sun direction is loop parameters, not branches**: morning vs evening flip the march increment, start offset, and width-scale sign. The ray-march is 1-D along texture rows (sun azimuth is east/west by design). The elevation texture is 1.5x wider than the shadow texture, extended on the sun side, so the march can find occluders casting in from off-screen.
- **Shadow shaping**: per-occluder shadow is feathered by sun-vs-terrain angle and distance falloff, max-accumulated; an elevation height-fade band keeps low beach terrain from casting long noisy shadows at dawn/dusk, and a moon multiplier fades all terrain shadow at night in step with the lighting envelope.
- **Terrain blur radius is compile-time, object radius is runtime**: `kiShadowWindowMargin` must be derivable at compile time from the blur radius (it gates every window test), so the terrain radius is fixed; the object chain has no visible window (plain full-texture bounds guard), so its radius/sigma are free tweak uniforms.
- **Object blur grid and filter contract**: adjacent Gaussian taps are paired into linear-filtered samples only when the sampler texel grid matches the logical tap spacing. V's intermediate and output grids always match and use pairing; H pairs at equal extents and retains discrete output-grid taps when the independently scaled render input differs. Keep both input samplers linear and clamp-to-edge. H inverts sampled coverage so the blur accumulates occlusion; V applies final intensity, returning to multiply-with-lighting form.
- **Temporal de-flicker**: the texel ramp resamples high-contrast shadow edges every frame while tracking a zoom, causing flicker. `ShadowTemporal.comp` reprojects each texel's world position into the previous frame's area to fetch history, then EMA-blends with a runtime weight (1.0 = disabled; forced on the first frame so uninitialized history never shows). Reprojected UVs outside [0,1] are disocclusions and fall back to current. Mirrors the smoke/wind previous-area reprojection.
- **In-place read-modify-write**: `ShadowTemporal.comp` is the only non-`writeonly` storage image user here — it loads and stores the blur texture in place, relying on the barrier recorded after `ShadowBlurV`; reordering the dispatch chain must preserve that barrier.

## See Also

- `../AGENTS.md` - Shared includes and cross-cutting shader conventions
