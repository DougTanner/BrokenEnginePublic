# Terrain Detail-Normal Height Fade (fragment sample trim)

## Context

`kGpuTimerTerrain` measures **998 µs current / 987 avg / 1027 max** at 120 fps, Profile build, ~6.6 ms total GPU/frame. Caveat: **idle scene near the spawn islands** — shoreline-heavy framing, so the rock/beach mask branches below are active on a large share of island pixels; this plan's win tracks that share.

`Terrain.frag` samples up to ~20 textures per fragment. The two material-detail branches are the biggest discretionary block: the rock branch does 3 `SampleNormal` octaves (`rockNormalsSampler0/1/2` at three world-space tilings) + 1 `rockSampler` color mix; the beach branch does 3 more `SampleNormal` octaves (`sandNormalsSampler0/1/2`) + 1 `sandSampler` color mix — **up to 8 of the ~20 samples**, gated only by the per-island mask (`fRockPercent`/`fBeachPercent > 0.001f`), never by camera height.

At gameplay heights (kilometers up) the detail-normal octaves are analytically near-no-ops: their world-space tilings (`fTerrainRockNormalsSize*`, `fTerrainBeachNormalsSize*`) put the fetches deep in the BC5 mip chain, where averaged RG → 0.5 decodes to a normal ≈ (0,0,1), whose blend contribution ≈ 0 — six texture fetches per fragment that buy nothing on screen. The *color* mixes (`rockSampler`/`sandSampler`) are different: their mip-averaged flat color still tints the island and its removal would be visible — they stay.

The engine has a canonical shape for exactly this: **`HeightLerpWrapperQuartet`** (`Engine/Source/Ui/AGENTS.md` — "Canonical shape for camera-height-conditional uniforms"), resolved CPU-side per frame, so the shader sees a single pre-faded uniform.

## Design

- **CPU:** replace the flat `gTerrainRockNormalsBlend` / `gTerrainBeachNormalsBlend` wrappers (`TerrainWrappersBase.{h,cpp}`) with height-faded resolution: keep each existing blend slider as the low-height strength and add fade start/end height sliders (one shared pair for both families, or a `HeightLerpWrapperQuartet` each — grill; quartet is the established shape). `GlobalUniforms.cpp` (which populates `GlobalLayout.fTerrainRockNormalsBlend` / `fTerrainBeachNormalsBlend`) writes the height-resolved value: full strength below fade-start eye height, 0.0 at/above fade-end.
- **Shader:** in `Terrain.frag`, extend the two branch conditions so the octave sampling is skipped when its blend uniform is zero: `if (fRockPercent > 0.001f && globalLayout.fTerrainRockNormalsBlend > 0.0f)` around the 3-octave `SampleNormal` sum + normal blend, with the color `mix` hoisted out under the original mask-only condition (it keys off `fTerrainRockBlend`, untouched). Same restructure for the beach branch. The blend uniforms are dynamically uniform, so the branch is warp-coherent — a true skip, matching the existing `fPowerMode` skip pattern in `ShaderFunctions.h`.
- **Continuity = no pop:** the contribution lerps to exactly 0.0 *before* the branch turns off, so the skip point is visually a no-op by construction (same trick as fading a light to zero before culling it).
- **Explicitly rejected in-scope alternative:** forwarding `Terrain.vert`'s `fVertexZ` as an interpolant to drop the per-fragment `elevationTextureSampler` fetch (1 sample/px over all terrain pixels). Rejected because (a) the vertex Z includes the underwater seafloor sink while the fragment deliberately reads the un-sunk MAX composite (lighting/shadow lookups at overlap skirts would change), and (b) it degrades with `TerrainMeshLodChain.md` — the per-fragment composite fetch is what keeps lighting/shadow sampling identical across mesh LODs. Recorded here so it isn't re-proposed.

**Visual impact: (b) minor/imperceptible at gameplay camera heights.** Above the fade band the octaves' on-screen contribution is already mip-flattened to ~zero; below fade-start nothing changes; inside the band the strength lerps. Zoomed fully in, behavior is bit-identical to today (fade-start default sits above the close-inspection zoom bucket). Runtime sliders: the existing blend strengths plus fade start/end heights — suggested defaults: fade start ≈ 2x min eye height, end ≈ 6x (grill; tune live against the Tweaks Terrain section), so the tradeoff is fully user-tunable and settable to "never fade" (fade-start = max height) as the fallback.

**Expected saving:** 6 samples/fragment removed on mask-active island pixels at gameplay heights — estimated **50-150 µs** on the idle spawn scene (shoreline-heavy), less on mask-sparse framing. Small but cheap, and it compounds with `TerrainMeshLodChain.md`'s quad-overshading reduction (fewer helper-lane wasted samples).

## Critical files

- `Engine/Data/Shaders/Terrain/Terrain.frag` — rock/beach branch restructure (octave skip vs color mix split).
- `Engine/Source/Ui/TerrainWrappersBase.{h,cpp}` — fade wrappers (quartet or start/end pair) replacing/augmenting `gTerrainRockNormalsBlend` / `gTerrainBeachNormalsBlend`; Terrain Tweaks section slider registration (`TweaksSliderMap`, declaration order = slider order per `Ui/AGENTS.md`).
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` — height-resolved uniform population (`fTerrainRockNormalsBlend` / `fTerrainBeachNormalsBlend` writes).
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — no layout change expected (same two floats); cited because the uniforms live in `GlobalLayout`.

## Out of scope

- The rock/sand **color** mixes and their `fTerrainRockBlend`/`fTerrainBeachSandBlend` sliders (visible tint at all heights — kept).
- The `fVertexZ`-interpolant elevation-fetch removal (rejected above, recorded).
- Octave *count* reduction (3 → 2) at full strength — a quality regression at close zoom, class (c); not proposed.
- Lighting/shadow/smoke sample reduction in `Terrain.frag` (each feeds a distinct visible term; no analytically-zero candidates).
- Snow branch (`fSnowPercent` path costs no extra samples).
- Mesh-side work (`TerrainMeshLodChain.md`, `TerrainInstanceAreaCull.md`).

## Notes

- **Invariant exposure:** client/graphics-only; **shader repack** (Terrain.frag SPIR-V re-export via DataPacker). No `DataHeader::kiVersion`/`.pack` layout change, no CRC/wire/determinism, no allocation-tracked-path concerns (uniform math only).
- Wrapper min/max bounds: fade heights need no shader-safety bounds (consumed CPU-side); keep the existing blend-strength bounds untouched.
- **Sequencing:** model-data mip bias now uses a dedicated Model-only sampler; terrain detail-normal fetches remain on the existing terrain sampler, so there is no overlap with this plan.
- **Grill decision (single):** shared fade pair vs per-family `HeightLerpWrapperQuartet`s, and the default fade band heights.
