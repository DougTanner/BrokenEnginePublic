# Particles - GPU-Driven Particle System Shaders

## Overview

Fully GPU-resident particle system — compute spawn and physics stages plus instanced quad render stages, no CPU readback; the CPU only fills a per-framebuffer spawn staging buffer (CPU side: [ParticleManager](../../../Source/Graphics/Managers/ParticleManager.CLAUDE.md)). The same two compute shaders are built into two independent pipeline families — Long (velocity-stretched trails) and Square (camera-facing rotating quads) — distinguished purely by descriptor wiring (own storage/spawn buffers and render vertex shader), not shader permutations. Client-side VFX only; not deterministic sim state.

## Compute Pipeline

- **ParticlesSpawn.comp** - Single-threaded (`local_size_x = 1`) by design — free-slot claiming is sequential, which is what lets it write the allocation bitmap non-atomically. Copies spawn-buffer particles into free slots (silently dropping the remainder if the pool fills), computes delta time GPU-side (elapsed time minus the stored last-update time — the CPU never passes a dt), tightens `iLastCount` past the dead tail, and writes the indirect dispatch/draw commands for the update and render pipelines. Also owns the one-shot reset path (clears the allocation store and indirect commands, reseeds the clock), driven by ParticleManager's reset flag
- **ParticlesUpdate.comp** - Parallel physics, dispatched indirectly: Euler integration (xyz only — preserves Position.w=1 / Velocity.w=0 bit-exact), terrain collision against the elevation prepass texture, gravity, decay; kills particles below size/intensity thresholds or below water and frees their slots

## Render Shaders

- **LongParticlesRender.vert** - Quads whose long axis follows velocity, length scaled by a velocity-driven stretch multiplier. Two guards: zero velocity falls back to an eye-facing quad (newly spawned particles), and the width-axis cross (to-eye × direction) checks magnitude for view-parallel velocity — the common case under the top-down camera, not an edge case
- **SquareParticlesRender.vert** - Camera-facing quads with per-particle texcoord rotation. World-up is perturbed when `|toEye.z| > 0.999` so the basis crosses never go zero (same top-down rationale; mirrors `Debug/DebugRenderBillboard.vert`); the final up axis skips `normalize()` per the parent's unit-preserving identities (cross of perpendicular unit vectors)
- **ParticlesRender.frag** - Shared by both particle vertex shaders. Intensity curve clamps the pow base to 0 — decay can overshoot intensity below zero and `pow` of a negative base is NaN. Bindless cookie texture; smoke attenuation folds a height-fraction fade above `fBaseHeight` into the `SmokeShadow` sample. Additive-blend contract: output alpha is 0 and `color.a` is baked into RGB — only valid with the `kAdd` pipeline
- **Billboards.vert/.frag** - Separate screen-space system that shares the directory but not the particle pipeline: no spawn/update, no allocation bitmap, quads placed directly in NDC with aspect correction. Consumed by the game-frame Billboards collection via a dynamic pipeline with alpha blend and a host-visible indirect count — not interchangeable with the additive particle path

## Architecture Notes

- Indirect everything: spawn writing the update dispatch and render draw commands removes any CPU knowledge of live counts — zero readback, zero fences. The dispatch math hardcodes `(count + 31) / 32` with a comment instead of referencing `kiParticleUpdateGroupSize`, so changing the group size means touching both compute shaders
- The cost of indirect counts is hole tolerance: render `instanceCount` includes freed slots, so both particle vertex shaders check the allocation bitmap and emit degenerate quads for holes, and update early-outs on unallocated slots
- Allocation storage is `ENABLE_32_BIT_BOOL`-switched: a packed 32-bit-word bitmap (live path) or a `uint16_t` array (dead branch, kept type-reconciled). Spawn claims slots scanning forward from `iMinFreeIndex`; update frees with `atomicAnd` and `atomicMin`s the hint back down — 32 invocations share each bitmap word, so only the single-threaded spawn may write non-atomically
- All shaders here read SSBO struct fields one at a time, never whole-struct copies — glslang has been observed to drop trailing fields on scalar-block-layout SSBO struct copies (see `Terrain.vert`)
- Snap-to-terrain bounce: collision snaps z to the terrain elevation and flips z-velocity via `abs()` rather than re-integrating a full dt step (avoids energy gain); the death test uses z captured before the snap so water hits aren't masked

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Shared includes, scalar block layout, bindless texture and descriptor set conventions
