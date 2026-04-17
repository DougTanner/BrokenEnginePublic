# Model - Physically-Based Rendering Shaders

GLSL shaders implementing Cook-Torrance microfacet BRDF for rendering models with physically-based materials, based on the glTF 2.0 PBR specification.

## Shaders

### Main Rendering Pipeline
- **ModelCommon.h** - Shared vertex I/O declarations, `MeshData` and `JointMatrix` struct definitions, and the `ModelVertexOutput` function handling camera, visible area, and shadow projection modes
- **ModelStatic.vert** - Vertex shader for static (non-animated) models
- **ModelSkinned.vert** - Vertex shader for animated models with skeletal skinning using offset-based indexing into a shared joint matrix buffer, with precomputed normal matrices from `MeshData`
- **Model.frag** - Full PBR fragment shader combining Cook-Torrance direct sun/moon BRDF, IBL split-sum ambient, per-direction EWNS specular, engine directional/ambient lighting, emissive, and smoke. Individual contributions can be isolated via `ENABLE_*` preprocessor toggles
- **ModelShadow.frag** - Minimal shadow pass for shadow map generation

### IBL Precomputation
- **ModelGenBrdfLut.vert / .frag** - Generates BRDF lookup texture via Monte Carlo integration over the GGX distribution

Irradiance and pre-filtered radiance cubemaps are generated offline by DataPacker and loaded from pack data at runtime.

## Architecture Notes

- **Descriptor layout**: Extends the engine's multi-set layout with Set 2 for per-material data. Per-material texture indices reference the global bindless texture array
- **Tangent-free normal mapping**: Tangent space computed from screen-space derivatives, avoiding per-vertex tangent storage
- **Compact joint matrices**: 3-row format blended in the vertex shader and reconstructed to mat4 for skinning
- **Tone mapping**: ACES filmic with configurable exposure and gamma

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory with shared utilities and the NVIDIA `inverse()` bug details
