# Engine/Data/Shaders/Objects - Game Object Shaders

## Overview

GLSL shaders for rendering game-specific objects including hex shields and player models. These shaders handle instanced rendering of dynamic game objects using storage buffers for per-instance data.

## Shaders

### HexShield (Vertex + Fragment + Lighting Fragment)
Renders translucent geodesic hex shield effects around game objects. Uses glTF vertex format inputs (position, normal, UVs, joints, weights) from the DualGeodesicIcosahedron glTF model buffer. The vertex shader applies per-instance rotation, directional wave deformation, size scaling, and world positioning via storage buffer data. Supports both perspective (view-projection) and visible-area (2D top-down) rendering modes selected via push constants. The fragment shader blends skybox cubemap reflections with per-instance color and computes directional intensity falloff for shield hit effects. The lighting fragment outputs directional EWNS lighting contributions to the MRT R/G/B lighting targets.

### Objects (Vertex)
General-purpose instanced object vertex shader using OBJ vertex format (position, normal, texcoord). Transforms vertices using per-instance 3x4 matrices from storage buffer. Supports two rendering modes via push constants: perspective mode (standard view-projection transform) and shadow mode (projects flattened shadow geometry into visible-area space using sun direction translation, sunrise/sunset cubic offset, and directional stretching based on vertex distance from instance origin).

### Player (Fragment)
Fragment shader for player objects combining skybox cubemap reflections with four-channel directional lighting and shadow mapping.

### ObjectShadows (Fragment)
Minimal shadow pass fragment shader that outputs zero for shadow map generation.

## Architecture Notes

- All object shaders use instanced rendering with `gl_InstanceIndex` to index into storage buffer arrays
- Push constants control rendering mode (perspective vs. visible area / shadow passes)
- HexShield shaders use glTF vertex format; Objects shader uses OBJ vertex format
