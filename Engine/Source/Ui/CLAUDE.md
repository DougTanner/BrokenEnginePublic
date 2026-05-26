# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes.

## Key Systems

- **Wrapper** - Type-safe value container for UI-bound settings with change detection. Supports float, bool, and discrete enum types.
- **HeightLerpWrapperQuartet** - Four `Wrapper`s grouped as a single eye-height-lerped scalar; `Resolve(fEyeHeight)` returns the lerped float. Canonical shape for camera-height-conditional uniforms — see `Engine/Source/Graphics/Render/CLAUDE.md`. A free `engine::LerpAtHeight(...)` in the same header serves hard-coded-endpoint variants that don't expose author controls.
- **CurveData / CurveWidget** (client-only) - Monotone cubic Hermite (Fritsch-Carlson) curve with Photoshop-style ImGui editor. Capped at 16 control points; endpoints locked; interior points clamp between neighbors.
- **NetworkUiControl** - Templated helper that disables a control while awaiting server confirmation and resets when the confirmed value arrives.

## Architecture Notes

Engine-scope wrappers split per Tweaks tab into `<Tab>WrappersBase.{h,cpp}` pairs (Pbr, Terrain, Water, Lighting, Shadow, SunMoon, Misc, Smoke, Wind, GraphicsSettings, SoundSettings). `WrapperBase.{h,cpp}` retains the `Wrapper` class itself plus internal-only globals not bound to any UI surface. Game-specific wrappers live in the game `Ui/` directory under matching per-tab pairs. Curve types are `BT_CLIENT`-guarded because `ImVec2` is server-unavailable.

**Wrapper invariants**: Storage is always `float` internally. `operator=` is deleted — callers must use the setter API. `Changed<T>()` self-advances previous-value tracking, making it a single-consumer contract per frame. Discrete-enum construction takes the allowed-value set and `DEBUG_BREAK`s on out-of-set values. The float-flavoured constructor accepts an optional snap step; when non-zero, default/min/max and all subsequent set/reset values round to the grid before clamp — used to enforce shader-side integer-product invariants on continuous sliders.

**NetworkUiControl invariants**: Update must be called every frame with authoritative state. While pending, a diverged state auto-clears the flag; otherwise the baseline is continually re-cached so the next pending-snapshot is fresh.

**CurveData change detection**: Size + scalar-hash compare, self-advancing (same single-consumer contract as `Wrapper`).

**Cross-system coupling**: `gWaterShapeDetail`'s largest value scales the full-detail size used by `TextureManager::DetailTextureSize`, driving the water mesh LOD0 quad grid. Render-target dimensions baked into texture creation are refreshed via the destroy-flag pipeline in `Graphics::Update` — any new wrapper that drives such state must register its own change-detector and destroy flag there (the shadow-texture resolution wrapper is the precedent). Prefer routing settings through per-frame uniforms when feasible; baking values into RTT clear/format requires a `kPipelines` destroy on every change and stale-descriptor risk for bindless-array consumers patched only on residency transitions. Within each per-tab `<Tab>WrappersBase` pair, declaration order must match the matching `TweaksScreen<Tab>.cpp` slider order.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays
