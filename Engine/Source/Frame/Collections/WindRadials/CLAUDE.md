# /Engine/Source/Frame/Collections/WindRadials/

Client-only radial wind simulation input quads with animated expansion, using the Controller pattern.

## File Structure

The implementation is split across three `.cpp` files:
- **WindRadials.cpp** - Registration, lifecycle, equality
- **WindRadialsUpdate.cpp** - Update, add/addControlled, collision phases
- **WindRadialsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
