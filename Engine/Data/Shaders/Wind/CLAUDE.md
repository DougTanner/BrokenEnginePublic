# Engine/Data/Shaders/Wind - Wind Simulation Shaders

GPU-driven 2D wind field simulation using a copy+simulate texture architecture, sharing the smoke system's coordinate space.

## Overview

The wind system maintains a 2D velocity field (stored as RG channels) that advects over time. Objects deposit directional wind via trail-shaped quads, and the field simulates once per frame via a copy+simulate pass pair. The resulting wind velocity feeds back into the smoke simulation and affects visual elements like vegetation.

## Architecture: Copy+Simulate

The wind system uses a `vkCmdCopyImage` plus a simulation render pass per frame:

- **vkCmdCopyImage** copies TextureOne to TextureTwo (preserving state as a hardware image copy). TextureTwo is a non-render-pass transfer destination (usage: `VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`)
- **WindSpread** reads from TextureTwo and writes the full simulation result to TextureOne

This yields exactly one simulation step per frame, avoiding the double-advection/decay that would result from running identical simulation in both passes. The copy always executes (even when wind is disabled or clearing) since command buffers are record-once; conditional behavior is controlled by indirect draw buffer instance counts on WindSpread and WindClear.

## Shaders

- **WindDeposit.frag** - Writes wind velocity into the wind texture from per-object oriented quads. Samples a falloff texture and applies wind direction and magnitude passed via per-vertex params from the CPU-side WindDeposits collection. The oriented vertex shader handles quad alignment to the motion direction, so the fragment shader simply scales the falloff by magnitude and direction.
- **WindSpread.frag** - Full simulation pass reading from TextureTwo and writing to TextureOne. All operations are time-scaled for framerate independence. Computes a magnitude factor (`fMagFactor`) using a threshold range with configurable power curve: magnitude is remapped through `fWindThresholdLow`/`fWindThresholdHigh` and shaped by `fWindThresholdPower` via `pow()`. This factor gates multiple simulation stages. Slider semantics are intuitive (up = more effect): decay sliders control energy dissipation (high value = faster decay, applied as `1.0 - decayValue` internally), and momentum sliders control directional inertia (high value = more momentum = less lateral spread/swirl/diffusion, applied as `1.0 - momentumValue` to derive a spread factor). Semi-Lagrangian advection traces back along wind direction scaled by the spread factor so high-momentum wind advects directionally while low-momentum wind spreads laterally. Noise-driven swirl perturbation and 4-neighbor diffusion blending also scale with the spread factor. An energy scale factor (`fWindEnergyScale`) applied as `pow(scale, timeScale)` compensates for numerical dissipation from advection and diffusion, allowing fine-tuned control over whether the field conserves, gains, or loses energy over time.

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shader directory overview
