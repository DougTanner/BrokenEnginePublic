# Engine/Data/Shaders/Objects - Game Object Shaders

## Overview

GLSL shaders for instanced rendering of dynamic game objects. Per-instance data (transforms, colors, intensities) is read from storage buffers indexed by `gl_InstanceIndex`.

## Shaders

- **HexShield** (vert + frag + lighting frag) - Translucent geodesic hex shield effects with cubemap reflections and dynamic shield hit visuals. The lighting fragment outputs directional contributions to the MRT lighting targets, making shields act as light emitters.
- **Objects** (vert) - General-purpose instanced object vertex shader supporting perspective and shadow projection modes via push constants.
- **Player** (frag) - Player fragment shader combining cubemap reflections with sun lighting, directional lighting, and shadow mapping.
- **ObjectShadows** (frag) - Minimal shadow pass fragment shader for shadow map generation.

## Architecture Notes

- HexShield uses glTF vertex format; Objects shader uses OBJ vertex format
- HexShield has three shader stages because it contributes to both the main color output and the deferred directional lighting MRT targets
- The hex shield lighting deposit applies an edge fade to prevent popping at lighting texture boundaries (see `ShaderFunctions.h`)
