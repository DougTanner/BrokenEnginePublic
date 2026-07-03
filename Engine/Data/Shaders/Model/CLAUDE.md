# Model - Physically-Based Rendering Shaders

GLSL shaders implementing Cook-Torrance microfacet BRDF for rendering models with physically-based materials, based on the glTF 2.0 PBR specification.

## Shaders

### Main Rendering Pipeline
- **ModelCommon.h** - Shared vertex I/O declarations, `MeshData` and `JointMatrix` struct definitions, and the `ModelVertexOutput` function handling camera, visible area, and shadow projection modes
- **ModelStatic.vert** - Vertex shader for static (non-animated) models; transforms vertices directly with no per-mesh matrix
- **ModelSkinned.vert** - Vertex shader for animated models; blends per-vertex joint weights against offset-indexed joint matrices, then applies the per-mesh transform and precomputed normal matrix from `MeshData`
- **Model.frag** - Full PBR fragment shader combining Cook-Torrance direct sun/moon BRDF, IBL split-sum ambient, four-cardinal-direction EWNS specular, engine directional lighting, emissive, and smoke. Individual contributions can be isolated via `ENABLE_*` preprocessor toggles
- **ModelShadow.frag** - Minimal shadow pass; writes a constant to a single-channel target so only depth/coverage is recorded. Declares the full vertex-output interface (unused) so it pairs with either model vertex shader

### IBL Precomputation
- **ModelGenBrdfLut.vert / .frag** - Generates the split-sum BRDF lookup texture (scale/bias) via Hammersley-sequence Monte Carlo GGX importance sampling, with a Frisvad tangent frame and a specialization-constant sample count. The LUT's V axis is inverted roughness — the generator and the runtime lookup in Model.frag must stay paired

Irradiance and pre-filtered radiance cubemaps are generated offline by DataPacker and loaded from pack data at runtime.

## Architecture Notes

- **Material index doubles as mesh slot index**: the push-constant material index selects both the per-material entry (Set 2; texture indices into the global bindless array) and, offset from the instance's mesh-data base, the vertex shader's per-mesh slot — one draw call per material/primitive, an invariant the CPU buffer fill must maintain.
- **Tangent-free normal mapping**: Tangent space is derived per-fragment from screen-space position/UV derivatives, avoiding per-vertex tangent storage. Falls back to the geometric normal when the UV gradient is degenerate or the Gram-Schmidt-orthogonalized tangent collapses, so the mapper degrades to flat shading instead of producing NaNs. Normal maps are BC5 (two channels); Z is reconstructed as `sqrt(1 - X^2 - Y^2)`.
- **Compact joint matrices**: skinning matrices are stored as a 3-row (48-byte) form in a buffer separate from per-mesh data; the vertex shader weight-blends rows and reconstructs a mat4. Split out so per-mesh data stays small and fixed-size with no embedded joint-count cap (a layout choice — the NVIDIA pipeline-creation hang is triggered by `inverse()`, not by large `mat4[]` arrays; see parent doc).
- **Skinned normal matrix is approximate**: the skinned path multiplies the precomputed mesh normal matrix by `mat3(skinMatrix)`, valid only for the near-rigid joint transforms the engine uses (no per-joint inverse-transpose).
- **Shadow projection grows the footprint**: the shadow render mode expands the model's lateral XY extent around its position before projecting, so small units cast readable shadows from the km-altitude RTS camera.
- **Linear HDR output**: emits unclamped linear HDR color with no per-material tone map or gamma — the fullscreen HDR-resolve pass tone-maps the accumulated frame (see [../CLAUDE.md](../CLAUDE.md)). IBL cubemap samples are linearized on read (decoding stored sRGB cubemap data); engine cubemaps are sampled in Y-up space (swizzled from Z-up world).
- **Sun/moon split**: the direct and IBL paths take the per-fragment max of a sun term (terrain-shadow-attenuated, squared on the BRDF path) and a moon term (bypasses shadow — Model.frag has no shadow input besides the terrain texture). Per-target intensity sliders are applied at the use sites, not baked into the colors.
- **Linear `fPbrSun` response on both BRDF and IBL paths**: `fSunIntensity` / `fMoonIntensity` are the Rec.709 luminance of the *unscaled* sun/moon color (divided by `fPbrDayBrightness`), so the IBL specular consumer compounds to a single `fPbrSun` factor matching the direct BRDF term. Deriving the luminance scalars from `fPbrSun`-scaled colors instead would make the IBL response cubic while the BRDF stayed linear.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory with shared utilities and the NVIDIA `inverse()` bug details
