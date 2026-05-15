# Crop Gaea Mesher mesh to heightmap bbox

## Context

DataPacker's island bake produces a Mesher-baked terrain mesh from Gaea2 at full archetype footprint (`fFootprintMeters` × `fFootprintMeters`), but the heightmap is auto-cropped to its tight land bbox plus halo (post-crop dimensions land in `IslandHeader::fWorldFootprintXMeters` / `fWorldFootprintYMeters`). Result: mesh vertices outside the cropped bbox project to world positions outside the per-island `f4VertexRect` (sized by post-crop footprint in `Islands::UpdateActiveIslands`). Those vertices' visible-area UVs in `Terrain.vert` may sample composite G-buffer regions that belong to a neighbouring island or empty texture space.

Visual symptom: fringe artifacts (wrong color/AO/normal) at island silhouettes; sampling neighbour content for islands that overlap on the visible-area composite.

Severity is bounded today (single-archetype test case, halo absorbs most overshoot, `Terrain.frag`'s `fTerrainEarlyOut` discards low-elevation samples), but it will surface on multi-island layouts and shallow-beach archetypes.

## Design

In `BakeIslandIntermediates.cpp`'s mesh post-processing block (the section that reads `Mesh.gltf` and writes `MeshProcessed.bin`):

1. After the heightmap auto-crop computes `iCropX/iCropY/iCropWidth/iCropHeight`, derive the mesh's analogous bbox in mesh-local meters:
   - `fCropMinX_m = iCropX * fFootprintMeters / iTexturePixels`
   - `fCropMaxX_m = (iCropX + iCropWidth) * fFootprintMeters / iTexturePixels`
   - Same for Y, with image-space-Y to mesh-space-Y handling (Gaea's `UVOrigin: TopLeft` may invert).
2. Re-center mesh XY around the **cropped** bbox center (not the pre-crop footprint center) so mesh-local origin matches `f4VertexRect` center:
   - `fLocalX = vert.x - 0.5 * (fCropMinX_m + fCropMaxX_m)`
3. Discard vertices outside the cropped bbox AND any triangle whose three vertices are all outside — keep partially-inside triangles (don't crack the silhouette).
4. Rebuild the index buffer with the surviving triangles, remap vertex indices.

Alternative (lower-effort, lower-quality): keep all vertices but clamp XY to the bbox in DataPacker — squashes overshooting verts to the edge, producing a degenerate-triangle silhouette. **Not recommended** — discard-and-rebuild gives clean edges.

## Critical files

- `DataPacker/Source/BakeIslandIntermediates.cpp` — `BakeOne` mesh-processing block, immediately after the existing `tinygltf` parse and before `MeshProcessed.bin` write. Reuse the `iCropX`/`iCropY`/`iCropWidth`/`iCropHeight` locals already in scope.
- `DataPacker/Source/BakeIslandIntermediates.cpp` — bump `kiBakeVersion` 7 → 8 to invalidate cached intermediates.
- `Engine/Source/Frame/IslandTerrain.h` — no struct change needed; `mpfMeshPositions` and `miMeshVertexCount` keep the same shape.

## Out of scope

- Bake mesh at multiple LODs (separate plan if profiling demands).
- Switch to `Tris (Adaptive)` Mesher topology (would naturally concentrate density near coastlines and might obviate explicit cropping).
- Edit Gaea's `Mesher` node properties from DataPacker beyond `VerticesPerSide`.

## Acceptance criteria

- After re-bake, all `MeshProcessed.bin` vertex positions satisfy `|x| <= 0.5 * fWorldFootprintXMeters` and `|y| <= 0.5 * fWorldFootprintYMeters`.
- Visual A/B against pre-fix render: edge silhouettes free of fringe artifacts at coastlines.
- `Mesh.gltf` triangle count vs `MeshProcessed.bin` triangle count differ by the number of trim-discarded triangles (log the count for diagnostics).
