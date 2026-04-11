# /Engine/Source/Frame/Collections/Pushers/

Physics force fields with zone-based spatial acceleration for push queries with flag-based filtering. Compiles in both client and server builds using the Sync pattern. Render methods are present in the header but guarded with `#ifdef BT_CLIENT`.

## File Structure

The implementation is split across two `.cpp` files:
- **Pushers.cpp** - Registration, lifecycle (allocate/copy, spawn, transfer, destroy), `LogDifferences()`, render counter tracking (`#ifdef BT_CLIENT`)
- **PushersUpdate.cpp** - Update, sync, add/remove, zone setup, push application, collision phases

## Architecture Notes

- Zone acceleration uses `thread_local` globals so each Dispatch worker has its own copy for safe parallel physics execution
- `Update()` bulk-copies all SOA arrays from previous frame; `Sync()` then overwrites the entry for a specific ID each frame
- `SetupZones()` is called from `FramePostRenderBase::Update()` and rebuilds the spatial grid each frame centered on the player position; `ApplyPush()` queries a single zone cell with flag-based include/exclude filtering
- Render methods only track a profile counter (no GPU draw calls)

## ApplyClampedPush Utility

`engine::ApplyClampedPush(vecVelocity, vecPushDirection, fPushStrength, fMaxPushVelocity)` is an inline utility in `Pushers.h` implementing terrain-push-style clamped impulse: it caps the velocity component in the push direction to `fMaxPushVelocity`, preventing stacking of repeated pushes beyond the cap. Each caller passes its own cap (e.g., `kfPlayerMaxPusherPushVelocity`, `kfSpaceshipMaxPusherPushVelocity`), derived from half the entity's max speed.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
