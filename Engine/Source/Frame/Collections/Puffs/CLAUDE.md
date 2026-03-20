# /Engine/Source/Frame/Collections/Puffs/

Client-only fire-and-forget smoke puffs with custom keyframe animation, using the Controller pattern. All puffs are controlled (spawned via `AddControlled()`) and auto-destroy on animation expiry. `PuffsPostRender` carries no SOA members — lifetime is managed entirely through the interpolate collection.

## File Structure

The implementation is split across three `.cpp` files:
- **Puffs.cpp** - Registration, lifecycle (allocate/copy, spawn, transfer, destroy with auto-expiry of controlled puffs)
- **PuffsUpdate.cpp** - Update, addControlled, collision phases
- **PuffsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## Architecture Notes

- Uses a custom `PuffControllerType` / `PuffKeyframe` pair (area, intensity, rotation) instead of the default `ControllerType`/`ControllerKeyframe`, registered via `ControllerTypeRegistry<PuffsInterpolate, PuffControllerType>`
- `PuffsPostRender::Members()` returns an empty tuple — there are no PostRender SOA arrays

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
