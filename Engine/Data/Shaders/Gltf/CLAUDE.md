# Gltf - Physically-Based Rendering Shaders

GLSL shaders implementing Cook-Torrance microfacet BRDF for rendering glTF 2.0 models with physically-based materials. Based on Sascha Willems' Vulkan-glTF-PBR reference implementation.

## Overview

This directory contains PBR shaders for rendering glTF models along with precomputation shaders for image-based lighting resources. The implementation follows industry-standard PBR techniques based on the glTF 2.0 specification, Khronos glTF-WebGL-PBR reference, and Google Filament documentation.

## Shaders

### Main Rendering Pipeline
- **GltfCommon.h** - Shared header defining vertex inputs (position, normal, 5 UV channels, joint indices, joint weights), vertex outputs, uniform bindings, and the GltfVertexOutput function that applies Y-axis flips to both positions and normals for Vulkan coordinate system conversion, then handles three rendering modes: camera view projection, visible area projection, and shadow projection with sun-based offset
- **GltfStatic.vert** - Vertex shader for static (non-animated) glTF models, passing vertex positions directly to GltfVertexOutput
- **GltfSkinned.vert** - Vertex shader for animated glTF models supporting both skeletal skinning (4-bone-per-vertex weighted blend) and mesh-based animation. Uses material index from push constants combined with the per-instance mesh data base offset to index into the mesh data buffer, allowing each material's draw call to access its own mesh transform and joint matrices
- **Gltf.frag** - Full PBR fragment shader with Cook-Torrance BRDF, both metallic-roughness and specular-glossiness workflows, image-based lighting, and engine integration for directional lights, shadows, and smoke. Uses getUV() helper to select the appropriate UV channel per texture based on material texture set indices
- **GltfLighting.frag** - Extracts emissive texture channels and outputs directional lighting contributions based on surface normals
- **GltfShadow.frag** - Minimal shadow pass outputting zero for shadow map generation

### IBL Precomputation
- **GltfFilterCube.vert** - Vertex shader for cubemap face rendering during IBL precomputation
- **GltfIrradianceCube.frag** - Generates diffuse irradiance cubemap via hemispherical convolution of environment map
- **GltfPrefilterEnvMap.frag** - Prefilters environment map for specular IBL using importance-sampled GGX distribution at varying roughness levels
- **GltfGenBrdfLut.vert / GltfGenBrdfLut.frag** - Generates BRDF lookup texture (split-sum approximation) using Monte Carlo integration over the GGX distribution

## Architecture Notes

The main fragment shader combines standard PBR direct lighting with engine-specific ambient lighting. Direct sun lighting uses the full Cook-Torrance BRDF, while engine directional lights contribute through the four-channel lighting system (see parent ShaderFunctions.h).

Normal mapping computes tangent space from screen-space derivatives, avoiding per-vertex tangent storage. Cubemap coordinates are transformed from the engine's Z-up space to Y-up cubemap space.

Debug visualization modes allow inspection of individual PBR components (base color, normals, AO, metallic, roughness) and BRDF terms (diffuse, Fresnel, visibility, distribution, specular).

Final output uses Uncharted 2 tone mapping with configurable exposure and gamma correction.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory with shared utilities and lighting functions
