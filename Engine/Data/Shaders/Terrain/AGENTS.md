# Terrain Shaders - Island Elevation and Compositing

Terrain first composites island heightmaps into visible and shadow elevation targets, then renders per-island meshes from bindless color, normal, ambient-occlusion, and material-mask textures.

## Elevation Contract

- Overlapping island footprints use MAX blending because packed chain bounds can overlap underwater. The composite elevation target is the authoritative upstream height source for water, shadow, lighting, smoke, and particle consumers.
- Mesh vertices sample the composite elevation for surface alignment, then check their own island heightmap. Vertices below the underwater cutoff sink to sea floor so an overlapping taller island cannot raise and expose baked-black submerged terrain.
- Terrain heightmaps store beach-relative engine meters. Undersea compression is applied once in the elevation prepass; downstream users inherit that shaping.

## Island and Material Invariants

- The bindless island texture slot comes from instance layout, not `gl_InstanceIndex`; cells may mix island templates without per-instance pipeline state.
- Island-local UVs do not rotate with world placement. Rotate mesh placement and decoded normal XY together; reconstructed normal Z is rotation-invariant.
- The packed material mask is R=Rock, G=Sand, B=Snow, A=Flow. Flow contains authored data but Terrain currently does not consume it.
- Snow has priority over rock and sand, contributes its sun-oriented normal, and suppresses ambient-occlusion darkening. Height fading gates detail-normal samples only; material color remains mask-driven.
- Island normals, ambient occlusion, and masks use the clamp sampler, while detail normals use the repeat sampler. They intentionally follow the global color-texture mip bias rather than the model-data sampler.
