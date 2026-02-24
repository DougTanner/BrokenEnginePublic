# Model - Physically-Based Rendering Shaders

GLSL shaders implementing Cook-Torrance microfacet BRDF for rendering models with physically-based materials, based on the glTF 2.0 PBR specification, Google Filament documentation, and LearnOpenGL PBR Theory.

## Overview

PBR shaders for rendering static and skinned models, plus precomputation shaders for image-based lighting resources. The main fragment shader layers direct sun lighting (Cook-Torrance BRDF), image-based lighting (split-sum approximation), engine directional lighting with per-direction specular, emissive, and smoke into a final tonemapped output.

## Shaders

### Main Rendering Pipeline
- **ModelCommon.h** - Shared header with vertex I/O declarations, mesh/joint data structures, and the `ModelVertexOutput` function that handles three rendering modes (camera, visible area, shadow projection with sun-based offset)
- **ModelStatic.vert** - Vertex shader for static (non-animated) models, passing vertex positions directly to `ModelVertexOutput`
- **ModelSkinned.vert** - Vertex shader for animated models with 4-bone-per-vertex skeletal skinning. Uses offset-based indexing into a shared joint matrix buffer so multiple meshes can share one buffer. Normal transformation uses CPU-precomputed normal matrices to avoid the NVIDIA `inverse()` driver bug
- **Model.frag** - Full PBR fragment shader using metallic-roughness workflow only (spec-gloss is converted at export time). Combines Cook-Torrance direct sun BRDF, IBL via split-sum approximation, engine four-channel directional lighting with full Cook-Torrance specular per EWNS direction, emissive, and smoke with height-based fade. All lighting contributions have independent artist-tunable multiplier and power controls. Contains preprocessor debug toggles to isolate individual contributions during development. Outputs fragment alpha for transparent material support
- **ModelLighting.frag** - Extracts per-channel emissive from materials and outputs EWNS directional lighting contributions based on surface normals
- **ModelShadow.frag** - Minimal shadow pass outputting zero for shadow map generation

### IBL Precomputation
- **ModelGenBrdfLut.vert / ModelGenBrdfLut.frag** - Generates BRDF lookup texture via Monte Carlo integration over the GGX distribution using Hammersley low-discrepancy sequence and Frisvad's tangent frame for importance sampling

Irradiance and pre-filtered radiance cubemaps are generated offline by DataPacker using CMFT and loaded from pack data at runtime.

## Architecture Notes

Model shaders extend the engine's multi-set descriptor layout with a third set: Set 0 (global), Set 1 (per-model: IBL cubemaps, lighting/shadow/smoke textures, mesh data, joint matrices), Set 2 (per-material). Per-material texture indices index into the global bindless texture array.

Normal mapping computes tangent space from screen-space derivatives, avoiding per-vertex tangent storage. Cubemap coordinates are transformed from the engine's Z-up space to Y-up cubemap space. Final output uses ACES filmic tone mapping with configurable exposure and gamma.

Joint matrices use a compact 3-row format (48 bytes vs 64 for a full mat4), blended in the vertex shader and reconstructed to mat4 for skinning.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory with shared utilities, lighting functions, and the NVIDIA `inverse()` bug details
