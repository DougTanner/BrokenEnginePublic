# ParticleManager

**Global**: `gpParticleManager`

GPU-based particle system with compute shader spawning and physics simulation. Two particle types (long trails and square explosions) sharing the same compute shaders. Fixed capacity with bitfield allocation tracking. Spawn is thread-safe via `mSpawnMutex` (called from worker threads during parallel frame tick dispatch), update is parallel (independent particles). Supports per-particle texture selection via the global bindless texture array.
