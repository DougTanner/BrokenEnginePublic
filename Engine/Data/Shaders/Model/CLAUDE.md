# Model - Physically-Based Rendering Shaders

GLSL shaders implementing Cook-Torrance microfacet BRDF for rendering models with physically-based materials. Based on Sascha Willems' Vulkan-glTF-PBR reference implementation.

## Overview

This directory contains PBR shaders for rendering models along with precomputation shaders for image-based lighting resources. The implementation follows industry-standard PBR techniques based on the glTF 2.0 specification, Khronos glTF-WebGL-PBR reference, and Google Filament documentation.

## Shaders

### Main Rendering Pipeline
- **ModelCommon.h** - Shared header defining vertex inputs (position, normal, 5 UV channels, joint indices, joint weights), vertex outputs, uniform bindings, MeshData struct with precomputed normal matrix (stored as 3 vec4s with GetNormalMatrix() helper), JointMatrix struct (3 rows, 48 bytes instead of full mat4), and the ModelVertexOutput function that handles three rendering modes: camera view projection, visible area projection, and shadow projection with sun-based offset. Storage buffers (models, meshData, jointMatrices) use scalar layout for C-like struct packing
- **ModelStatic.vert** - Vertex shader for static (non-animated) models, passing vertex positions directly to ModelVertexOutput
- **ModelSkinned.vert** - Vertex shader for animated models supporting both skeletal skinning (4-bone-per-vertex weighted blend) and mesh-based animation. Uses material index from push constants combined with the per-instance mesh data base offset to index into the mesh data buffer. Joint matrices are accessed via offset-based indexing using `jointMatrixOffset` from MeshData, allowing multiple meshes to share a single joint matrix buffer with different base offsets. Performs weighted blend of 3-row JointMatrix structs, then reconstructs a full mat4 by appending row (0, 0, 0, 1). Normal transformation uses precomputed normal matrix from CPU to avoid shader-side inverse() calls
- **Model.frag** - Full PBR fragment shader with Cook-Torrance BRDF using metallic-roughness workflow only (spec-gloss materials are converted at glTF export time). Implements GGX/Trowbridge-Reitz NDF, Smith-GGX geometry visibility, and Schlick Fresnel. BRDF diffuse and specular contributions have independent multiplier and power controls for artistic tuning. IBL uses split-sum approximation returning separate diffuse and specular via out parameters, each with independent multiplier and power controls. IBL diffuse is modulated by ambient intensity, ambient color blend (configurable mix between white and time-of-day ambient color), and shadow blend (configurable shadow influence). IBL specular is modulated by sun intensity, sun color, and shadow. Shadow floor is configurable via uniform. Engine integration includes Cook-Torrance specular evaluation per EWNS cardinal light direction (matching the main sun BRDF quality), normal-mapped directional lighting, shadows, smoke with height-based fade above base height, and emissive. Uses getUV() helper to select the appropriate UV channel per texture based on material texture set indices. Contains preprocessor debug toggles (`ENABLE_BRDF`, `ENABLE_IBL`, `ENABLE_EMISSIVE`, `ENABLE_SPECULAR_LIGHTING`, `ENABLE_DIRECTIONAL_LIGHTING`, `ENABLE_SMOKE`) to isolate individual lighting contributions during development. Outputs `baseColor.a` as the fragment alpha, enabling transparent materials when the pipeline uses alpha blending
- **ModelLighting.frag** - Extracts emissive texture channels and outputs directional lighting contributions based on surface normals
- **ModelShadow.frag** - Minimal shadow pass outputting zero for shadow map generation

### IBL Precomputation
- **ModelFilterCube.vert** - Vertex shader for cubemap face rendering during IBL precomputation
- **ModelIrradianceCube.frag** - Generates diffuse irradiance cubemap via hemispherical convolution of environment map
- **ModelPrefilterEnvMap.frag** - Prefilters environment map for specular IBL using importance-sampled GGX distribution at varying roughness levels
- **ModelGenBrdfLut.vert / ModelGenBrdfLut.frag** - Generates BRDF lookup texture (split-sum approximation) using Monte Carlo integration over the GGX distribution

## Architecture Notes

The main fragment shader combines standard PBR direct lighting with engine-specific ambient lighting. BRDF contribution applies shadow attenuation directly to sun-lit Cook-Torrance terms with independent diffuse and specular multiplier/power controls. IBL uses GetIBLContribution() which returns separate diffuse and specular via out parameters, each with independent multiplier and power controls. IBL diffuse is modulated by ambient intensity, a configurable ambient color blend (mixing between white and time-of-day color), and a configurable shadow blend. IBL specular is modulated by sun intensity, sun color, and shadow. Shadow floor is configurable via uniform rather than hardcoded. Engine directional lights contribute through the four-channel lighting system (see parent ShaderFunctions.h) using the normal-mapped surface normal for accurate per-pixel lighting. Specular from engine lights evaluates a full Cook-Torrance BRDF per EWNS cardinal direction, computing per-direction half vectors, Fresnel, GGX distribution, and Smith visibility terms against each light's RGB intensity to produce tight specular highlights matching the main sun BRDF quality. Emissive contribution is scaled by a configurable emissive multiplier uniform.

Day/night brightness scaling uses `fGltfDayBrightness` uniform to derive sun intensity for IBL specular tinting. Sun color is computed from the `fGltfSun` multiplier applied to the global sun color.

Normal mapping computes tangent space from screen-space derivatives, avoiding per-vertex tangent storage. Cubemap coordinates are transformed from the engine's Z-up space to Y-up cubemap space.

Final output uses Uncharted 2 tone mapping with configurable exposure and gamma correction.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory with shared utilities and lighting functions
