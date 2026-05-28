# Engine/Data/Shaders/Objects - HexShield Object Shaders

## Overview

GLSL shaders for instanced hex-shield rendering. Per-instance data (transform, normal transform, colors, per-direction directions/intensities, sizing) lives in a storage buffer indexed by `gl_InstanceIndex`. Directional state is two parallel arrays — vertex intensities drive geometry, fragment intensities drive shading — over a fixed set of directions (`kiHexShieldDirections`).

## Shaders

- **HexShield.vert** — Single vertex stage feeding both passes. Applies the instance transform, computes a center-normal (geodesic-center direction blended with mesh normal), grows the mesh along its normal, then displaces each vertex by a summed per-direction sine wave gated on vertex intensity, before scaling and translating into world space. A push-constant selects standard view-projection vs. an eye-line/base-height intersection projected into the visible (lighting) area.
- **HexShield.frag** — Main color pass. Mixes a skybox cubemap reflection (sampled along the center-normal reflection vector) with the instance base color by `fColorMix`; alpha is a summed per-direction fragment intensity attenuated by an edge falloff based on distance from the un-transformed mesh origin.
- **HexShieldLighting.frag** — Lighting deposit pass. Reuses the same per-direction intensity sum, then projects the center-normal XY into an EWNS 4-vector and scales it per RGB channel into the three lighting render targets so shields emit into scene lighting.

## Architecture Notes

- **Per-direction decoupling**: vertex intensities drive geometry displacement; fragment intensities drive alpha and lighting deposit. Keeps the hit-animation wave independent of the visible flash and emitted light.
- **Center-normal blend**: `normalize(normalize(position) + meshNormal)` blends the geodesic-sphere-center direction with the mesh normal to soften facet edges for both the cubemap reflection and the EWNS lighting projection.

## See Also

- [../CLAUDE.md](../CLAUDE.md) — scalar block layout, bindless textures, multi-set descriptors, push-constant render modes, EWNS lighting render targets, and the `reflect(unit, unit)` unit-length identity (relied on in HexShield.frag).
