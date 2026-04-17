# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes.

## Key Systems

- **Wrapper** - Type-safe value container for UI-bound settings with change detection. Supports float, bool, and discrete enum types.
- **CurveData / CurveWidget** (client-only) - Monotone cubic Hermite (Fritsch-Carlson) curve with Photoshop-style ImGui editor. Capped at 16 control points; endpoints locked; interior points clamp between neighbors.
- **NetworkUiControl** - Templated helper that disables a control while awaiting server confirmation and resets when the confirmed value arrives.

## Architecture Notes

Engine-scope wrappers live here; game-specific wrappers live in `game::Wrapper.h/.cpp`. Curve types are `BT_CLIENT`-guarded because `ImVec2` is server-unavailable.

**Wrapper invariants**: Storage is always `float` internally. `operator=` is deleted — callers must use the setter API. `Changed<T>()` self-advances previous-value tracking, making it a single-consumer contract per frame. Discrete-enum construction takes the allowed-value set and `DEBUG_BREAK`s on out-of-set values.

**NetworkUiControl invariants**: Update must be called every frame with authoritative state. While pending, a diverged state auto-clears the flag; otherwise the baseline is continually re-cached so the next pending-snapshot is fresh.

**CurveData change detection**: Size + scalar-hash compare, self-advancing (same single-consumer contract as `Wrapper`).

**Cross-system coupling**: `gWorldDetail`'s largest divisor must match `Graphics::WorldDetail()`. Wrapper declaration order must match the tweaks screen UI layout.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays
