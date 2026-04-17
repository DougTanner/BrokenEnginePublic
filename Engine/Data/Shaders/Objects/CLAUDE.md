# Engine/Data/Shaders/Objects - HexShield Object Shaders

## Overview

GLSL shaders for instanced hex-shield rendering. Per-instance data (transforms, colors, per-vertex/per-fragment directional intensities) lives in a storage buffer indexed by `gl_InstanceIndex`.

## Shaders

- **HexShield.vert** — Drives both the main color pass and the lighting MRT pass from a single vertex stage; a push-constant selects projection mode.
- **HexShield.frag** — Main color pass. Blends cubemap reflection with base color; alpha attenuated by directional-hit intensity and edge falloff.
- **HexShieldLighting.frag** — Lighting deposit pass. Writes four-quadrant (EWNS) directional contributions into three RGB MRTs so shields emit into scene lighting.

## Architecture Notes

- **Per-direction decoupling**: per-vertex intensities drive geometry displacement; per-fragment intensities drive alpha/lighting. Keeps hit-animation geometry independent of the visible flash.
- **Center normal blend**: `normalize(normalize(position) + meshNormal)` blends geodesic-sphere-center direction with mesh normal to soften facet edges in the cubemap reflection.
- **Lighting MRT write**: center-normal XY projected into EWNS 4-vector, scaled per RGB channel into three MRTs. Edge fade applied before write — see parent for rationale.

## See Also

- [../CLAUDE.md](../CLAUDE.md) — shared scalar block layout, bindless texture convention, dual-language headers.
