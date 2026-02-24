# Engine/Data/Shaders/Objects - Game Object Shaders

## Overview

GLSL shaders for instanced rendering of dynamic game objects including hex shields and player models. Per-instance data (transforms, colors, intensities) is read from storage buffers indexed by `gl_InstanceIndex`.

## Shaders

### HexShield (Vertex + Fragment + Lighting Fragment)
Renders translucent geodesic hex shield effects around game objects using glTF vertex format. The vertex shader applies per-instance rotation, directional wave deformation, size scaling, and world positioning. The fragment shader blends skybox cubemap reflections with per-instance color and computes directional intensity falloff for shield hit effects. The lighting fragment outputs directional EWNS contributions to the MRT R/G/B lighting targets, allowing hex shields to act as dynamic light emitters.

### Objects (Vertex)
General-purpose instanced object vertex shader using OBJ vertex format. Supports two rendering modes via push constants: perspective mode for standard camera rendering, and shadow mode which projects flattened shadow geometry into visible-area space with sun-direction-based offset and stretching.

### Player (Fragment)
Fragment shader for player objects combining skybox cubemap reflections with four-channel directional lighting and shadow mapping.

### ObjectShadows (Fragment)
Minimal shadow pass fragment shader that outputs zero for shadow map generation.

## Architecture Notes

- HexShield uses glTF vertex format (from the DualGeodesicIcosahedron model buffer); Objects shader uses OBJ vertex format
- HexShield has three shader stages (vertex, fragment, lighting fragment) because it contributes both to the main color output and to the deferred directional lighting MRT targets
