# ParticleManager

Global: `gpParticleManager`

Stages CPU particle spawns for fixed-capacity GPU compute allocation and simulation. Worker spawns first cull by visible area and intensity, then append under the spawn mutex during joined frame-tick work.

`RenderGlobal` runs after the worker join, copies staged spawns into the current framebuffer's mapped storage, and clears CPU staging. Bindless texture-index assignment mutates descriptor bookkeeping while spawning; its safety relies on this tick/render phase exclusion. Keep that exclusion if spawn or descriptor work moves between phases.
