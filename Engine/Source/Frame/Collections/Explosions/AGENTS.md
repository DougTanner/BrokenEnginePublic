# Explosions - Composite Effects

Explosions coordinate point lights, puffs, smoke trails, wind radials, and GPU particles. They compile in both builds, while visible child effects are client-only.

## Invariants

- Keep every shared-random draw outside client guards. Client-only effects consume the results, but both builds must advance the frame random engine identically.
- Recalculated client frames suppress GPU-particle spawning without suppressing its random draws, preventing duplicate visuals during reconciliation while preserving stream lockstep.
- Explosion state lives in Interpolate; the paired PostRender collection hosts phase operations and the public spawn API. Only shared members participate in deterministic CRC.
- On clients, `PersistentMembers()` carries the fixed `pTrails[]` child-handle arrays through `AllocateAndCopyMembers()`; the server persistent tuple is empty. `Update` repopulates the shared columns and remaining client trail state.
- Trail duration tuning scales both travel distance and elapsed trail time so head speed remains constant. Cleanup intentionally uses unmultiplied trail time, keeping child removal aligned with shared parent destruction.
- Fixed trail slots are cleared when inactive so row reuse cannot expose stale shared state. Self-destroying explosions remove their row after owned trails expire; otherwise the caller owns row lifetime.
