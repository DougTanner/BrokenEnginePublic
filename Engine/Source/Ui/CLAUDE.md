# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes.

## Key Systems

- **Wrapper** - Type-safe `float`-backed value container for UI-bound settings, with single-consumer change detection. Supports float, bool, and discrete-enum flavors; the discrete-enum form `DEBUG_BREAK`s on out-of-set values. `operator=` is deleted — mutate only through the setter API. `Changed<T>()` advances its own previous-value tracking, so exactly one consumer may poll it per frame. The float constructor's optional snap step rounds default/min/max and every set/reset value to a grid before clamp, used to enforce shader-side integer-product invariants on continuous sliders.
- **HeightLerpWrapperQuartet** - Four `Wrapper`s (start/end height, low/high) resolved to one eye-height-lerped scalar. Canonical shape for camera-height-conditional uniforms — see [Graphics/Render/CLAUDE.md](../Graphics/Render/CLAUDE.md). The free `engine::LerpAtHeight(...)` in the same header serves hard-coded-endpoint variants with no author controls.
- **CurveData / CurveWidget** (client-only) - Monotone cubic Hermite (Fritsch-Carlson) curve with a Photoshop-style ImGui editor. Capped at 16 control points; endpoints X-locked; interior points clamp between neighbors. Change detection mirrors `Wrapper`'s single-consumer contract (size + scalar-hash compare, self-advancing).
- **NetworkUiControl** - Templated helper that disables a control while awaiting server confirmation, auto-clearing once the authoritative state diverges from the requested snapshot. `Update` must be called every frame with authoritative state.

## Architecture Notes

Engine-scope wrappers split per Tweaks tab into `<Tab>WrappersBase.{h,cpp}` pairs (Pbr, Terrain, Water, Lighting, Shadow, SunMoon, Misc, Smoke, Wind, GraphicsSettings, SoundSettings). `WrapperBase.{h,cpp}` holds the `Wrapper` class plus internal-only globals bound to no UI surface. Game-specific wrappers live in the game `Ui/` directory under matching per-tab pairs. Curve types are `BT_CLIENT`-guarded because `ImVec2` is server-unavailable. Within each `<Tab>WrappersBase` pair, declaration order must match the slider order in the matching `TweaksScreen<Tab>.cpp`.

**Cross-system coupling**: Wrappers that drive baked render-target dimensions (not just per-frame uniforms) must register a change-detector and destroy flag in `Graphics::Update` — see the RTT-resolution caveats in [Graphics/Render/CLAUDE.md](../Graphics/Render/CLAUDE.md). Prefer routing settings through per-frame uniforms whenever feasible.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays
