# Particle Shaders - GPU-Driven Visual Effects

Long and Square particles share spawn and update compute shaders but use separate storage, spawn buffers, and render vertex paths. They are client-only visual effects with no CPU readback; ParticleManager (`../../../Source/Graphics/Managers/ParticleManager.AGENTS.md`) stages spawns and owns GPU resources.

## Pipeline Contracts

- Spawn runs serially so it can claim free slots without atomic allocation. It writes the indirect update and render counts, drops excess spawns when the pool is full, and owns reset initialization.
- Update and render tolerate holes inside the indirect live range. Update skips free slots; render emits degenerate geometry for them. Keep group-size assumptions paired between spawn's indirect dispatch calculation and update's workgroup size.
- Physics updates XYZ only, preserving Position W=1 and Velocity W=0. Terrain collision tests pre-snap height so water deaths are not hidden by the terrain bounce.
- Long particles fall back to an eye-facing quad for zero velocity and guard the view-parallel width-axis cross product. Square particles switch the reference up axis near a vertical view. These are normal top-down-camera cases, not optional edge handling.
- Clamp the intensity-curve base before `pow`; decay can drive intensity below zero and otherwise produce NaN.
- Particle fragment output is premultiplied into RGB with zero alpha and requires the `kAdd` pipeline.

The Billboards shaders in this directory are a separate screen-space, alpha-blended system without spawn, update, or allocation storage. Do not interchange them with the additive particle path.
