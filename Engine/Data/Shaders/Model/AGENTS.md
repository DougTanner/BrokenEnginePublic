# Model Shaders - Physically Based Rendering

Static and skinned model paths feed a Cook-Torrance material shader with split-sum image-based lighting. DataPacker generates irradiance and prefiltered-radiance cubemaps; the renderer generates and caches the BRDF lookup texture with this shader family.

## Architecture Notes

- The material push-constant index also selects the mesh-data slot relative to the instance base. CPU draw generation must keep one material/primitive draw aligned with that mesh slot.
- The generated BRDF lookup stores inverted roughness on its V axis. Generator and runtime lookup must remain paired.
- Tangent space is derived from screen-space position and UV derivatives. Degenerate UV gradients or a collapsed orthogonalized tangent fall back to the geometric normal to avoid NaNs.
- Normal and metallic-roughness textures use the model-data sampler; color, ambient-occlusion, and emissive textures use the color sampler. Preserve that separation when changing mip-bias behavior.
- Skinning uses compact three-row joint matrices in a buffer separate from mesh data. That split is a layout choice — each mesh indexes the shared buffer through an offset, so no per-mesh joint cap is baked into the mesh-data struct — and not a workaround for the shader-compiler hang the hub records; dynamic indexing of that buffer is exactly the case that ruled large arrays out as a cause. The skinned normal path combines the mesh normal matrix with `mat3(skinMatrix)` and therefore assumes near-rigid joint transforms rather than arbitrary non-uniform joint scaling.
- Shadow projection expands lateral model extent so small units retain readable shadows at the RTS camera scale.
- Material output remains linear HDR for the fullscreen resolve. Engine cubemaps use Y-up sampling, so model shaders perform the Z-up/Y-up conversion at the sampling boundary.
