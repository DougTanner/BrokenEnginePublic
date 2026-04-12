# /Engine/Source/Frame/Collections/Explosions/

Composite explosion effects that spawn lights, puffs, smoke trails, wind radials, and GPU particles. Compiles in both client and server builds, with visual spawning client-only. Game code registers custom explosion types.

## File Structure

The implementation is split across three `.cpp` files:
- **Explosions.cpp** - Registration, type registry, lifecycle (transfer, `Destroy()` with trail cleanup and expiry), `LogDifferences()`, render counter tracking
- **ExplosionsSpawn.cpp** - `Spawn()` with fire-and-forget child effects
- **ExplosionsUpdate.cpp** - `Update()` (copies explosion state, syncs trail positions with gravity), collision phases (all empty)

## Architecture Notes

- `Spawn()` spawns primary/secondary PointLights, Puffs, SmokeTrails, WindRadials, and GPU particles; random engine calls are unconditional to keep client/server in sync. Keyframe values are normalized multipliers; game-side `Wrapper` globals supply base magnitudes via per-keyframe wrapper arrays on controller types
- Per-instance scaling percentages gate visual effect intensity (light, size, smoke, time)
- Trail management: up to 8 SmokeTrails per explosion with gravity-affected interpolation and auto-cleanup. `Explosions.h` forward-declares `SmokeTrailsInterpolate` and `smoke_trails_t` (client-only) rather than including `SmokeTrails.h`, keeping the header dependency-light
- Uses `SharedMembers()`/`ClientMembers()`/`Members()` three-method pattern for shared CRC compatibility

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
