# `/Engine/Source/Ui/` - User Interface

Runtime-adjustable parameter wrappers and ImGui-based screen classes.

## Key Systems

- **Wrapper** - Type-safe `float`-backed value container for UI-bound settings, with single-consumer change detection. Supports float, bool, and discrete-enum flavors; the discrete-enum form `DEBUG_BREAK`s on out-of-set values. `operator=` is deleted — mutate only through the setter API. `Changed<T>()` advances its own previous-value tracking, so exactly one consumer may poll it per frame. `Set` writes only the current value (the next `Changed()` poll fires); `Reset` writes the previous value too, deliberately bypassing change detection — use it to seed a value (e.g., clamping to device capabilities at startup) without triggering rebuilds. The float constructor's optional snap step rounds default/min/max and every set/reset value to a grid before clamp, used to enforce shader-side integer-product invariants on continuous sliders.
- **HeightLerpWrapperQuartet** - Four `Wrapper`s (start/end height, low/high) resolved to one eye-height-lerped scalar. Canonical shape for camera-height-conditional uniforms — see [Graphics/Render/CLAUDE.md](../Graphics/Render/CLAUDE.md). The free `engine::LerpAtHeight(...)` in the same header serves hard-coded-endpoint variants with no author controls.
- **CurveData / CurveWidget** (client-only) - Monotone cubic Hermite (Fritsch-Carlson) curve with a Photoshop-style ImGui editor. Capped at 16 control points; endpoints X-locked; interior points clamp between neighbors. Change detection mirrors `Wrapper`'s single-consumer contract (size + scalar-hash compare, self-advancing). Drag state is one shared function-local static — at most one widget instance may be interacted with at a time.
- **NetworkUiControl** - Templated helper that disables a control while awaiting server confirmation, auto-clearing once the authoritative state diverges from the requested snapshot. `Update` must be called every frame with authoritative state.

## Architecture Notes

Engine-scope wrappers split per Tweaks tab into `<Tab>WrappersBase.{h,cpp}` pairs (Pbr, Terrain, Water, Lighting, Shadow, SunMoon, Misc, Smoke, Wind, GraphicsSettings, SoundSettings). `WrapperBase.{h,cpp}` holds the `Wrapper` class plus internal-only globals bound to no UI surface. Game-specific wrappers live in the game `Ui/` directory under matching per-tab pairs. Curve types are `BT_CLIENT`-guarded because `ImVec2` is server-unavailable; everything else compiles into both builds — the server reads wrappers at defaults, no UI mutates them there. Within each pair, declaration order must match the slider order in the matching Tweaks section — ordering rules in [Screens/TweaksScreen/CLAUDE.md](Screens/TweaksScreen/CLAUDE.md).

Wrapper headers are deliberately not aggregated into `Engine.h` (only `NetworkUiControl.h` is) — consumers include the specific `<Tab>WrappersBase.h` they need, so default-value edits don't recompile the world. Don't "fix" this by aggregating them.

Some wrapper min/max bounds encode shader-safety invariants (divide-by-zero and `pow`-base guards), marked by inline comments citing the exact shader file:line — changing such a bound is a shader-correctness change, not tuning.

**Cross-system coupling**: Wrappers that drive baked render-target dimensions (not just per-frame uniforms) must register a change-detector and destroy flag in `Graphics::Update` — see the RTT-resolution caveats in [Graphics/Render/CLAUDE.md](../Graphics/Render/CLAUDE.md). Prefer routing settings through per-frame uniforms whenever feasible.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug overlays
