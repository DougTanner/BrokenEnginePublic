# Engine/Data/Shaders/Objects - HexShield Object Shaders

## Overview

GLSL shaders for instanced hex-shield rendering. Per-instance data (transform, normal transform, colors, per-direction directions/intensities, sizing) lives in a storage buffer indexed by `gl_InstanceIndex`. Directional state is two parallel intensity arrays — vertex intensities drive geometry, fragment intensities drive shading — over a shared fixed-size direction array (`kiHexShieldDirections`).

## Shaders

- **HexShield.vert** — Single vertex stage feeding both passes. Applies the instance transform, computes a center-normal (geodesic-center direction blended with mesh normal), grows the mesh along its normal, then displaces each vertex by a summed per-direction sine wave gated on vertex intensity, before scaling and translating into world space. A push-constant selects standard view-projection vs. an eye-line/base-height intersection projected into the visible (lighting) area.
- **HexShield.frag** — Main color pass. Mixes a skybox cubemap reflection (sampled along the center-normal reflection vector, swizzled from engine Z-up to the cubemap's Y-up convention) with the instance base color by a per-instance mix factor; alpha is a per-instance baseline plus summed per-direction fragment intensities, attenuated by an edge falloff based on distance from the un-transformed mesh origin.
- **HexShieldLighting.frag** — Lighting deposit pass. Reuses the same per-direction intensity sum, projects the center-normal XY into an EWNS 4-vector, scales it per RGB channel into the three lighting render targets so shields emit into scene lighting, and fades the deposit near the lighting-area border (`LightingDepositEdgeFade`).

## Architecture Notes

- **Per-direction decoupling**: vertex intensities drive geometry displacement; fragment intensities drive alpha and lighting deposit. Keeps the hit-animation wave independent of the visible flash and emitted light. The color and lighting passes apply slightly different falloff curves to the same intensity sum — deliberate, not copy-paste drift.
- **Center-normal blend**: `normalize(normalize(position) + meshNormal)` blends the geodesic-sphere-center direction with the mesh normal to soften facet edges for both the cubemap reflection and the EWNS lighting projection.
- **Normal-projection deposit, not world-space-offset**: unlike the deposit convention in the parent CLAUDE.md (EWNS weights from world-space offset to the light center, omnidirectional epsilon fallback), the lighting pass projects the surface center-normal, and its epsilon fallback zeroes the deposit for fragments facing straight up.

## See Also

- [../CLAUDE.md](../CLAUDE.md) — scalar block layout, bindless textures, multi-set descriptors, push-constant render modes, EWNS lighting render targets, and the `reflect(unit, unit)` unit-length identity (relied on in HexShield.frag).
