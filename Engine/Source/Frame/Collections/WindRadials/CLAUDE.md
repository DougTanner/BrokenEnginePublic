# /Engine/Source/Frame/Collections/WindRadials/

Client-only radial wind simulation input quads with animated expansion, using the Controller pattern.

## File Structure

The implementation is split across three `.cpp` files:
- **WindRadials.cpp** - Registration, lifecycle (allocate/copy, spawn, transfer, destroy with auto-expiry of controlled radials)
- **WindRadialsUpdate.cpp** - Update, add/addControlled, collision phases
- **WindRadialsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## Architecture Notes

- Uses custom `WindRadialKeyframe` / `WindRadialControllerType` (intensity, size) instead of the default `ControllerType`/`ControllerKeyframe`, registered via `ControllerTypeRegistry<WindRadialsInterpolate, WindRadialControllerType>`
- `WindRadialsPostRender::Members()` returns an empty tuple -- there are no PostRender SOA arrays

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
