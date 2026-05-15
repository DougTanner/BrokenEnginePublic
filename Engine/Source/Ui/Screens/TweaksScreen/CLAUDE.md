# `/Engine/Source/Ui/Screens/TweaksScreen/` - Tweaks Parameter UI (Base)

Multi-section ImGui runtime parameter adjustment screen base class, bound to Wrapper globals. `Render()` body is gated by `if constexpr (kbDebugInput)` (compile-time elision). Screen state (section visibility, window positions, active subtabs) persists across restarts via `SaveState()`/`LoadState()`; game layer handles disk I/O.

## Architecture Notes

Data-driven: a slider map plus a parallel function-pointer table drive section rendering, indexed by `TweakSection` and guarded by `static_assert`. Adding a section requires updating the enum and both arrays in identical order.

**Exclusive-render while dragging**: when a slider is active, only its owning section window renders; others fade to alpha 0 with layout preserved. A sentinel index denotes "slider owned by the toggle bar".

**Toggle bar**: full-width bar hosts section show/hide selectables plus a special-cased full-width Sun Angle slider not in the main slider map.

**Slider map lifetime**: `TweaksSliderMap::Get()` returns a function-local static `std::unordered_map` wrapped in `ScopedSuppressAllocationTracking` (STL hash buckets heap-allocate; workbuffer unusable because lifetime is program-wide). Engine and game per-section `.cpp` files populate it at static-init time via anonymous-namespace `TweaksSliderMapRegistrar` instances co-located with the matching `Render*` function — each registrar owns exactly the labels its section consumes.

**Extension hooks**: Pure virtuals for game-only sections (hex shield, particles); default-empty virtual hooks for game-only tabs hosted inside an engine section (e.g., the Wind window's Deposits tab, the Lighting Effects window's Visible/Lighting tabs).

**Ordering convention**: Slider order in each per-section `.cpp` is the source of truth; Wrapper global order in the matching engine `<Tab>WrappersBase.{h,cpp}` (and the game-side per-tab wrapper pair) must match.

**Label disambiguation**: `WrapperSlider`'s `mapKey` parameter lets display labels drop redundant prefixes while preserving unique ImGui IDs via `"label##mapKey"` when keys collide.

**Column layout**: At most one `ImGui::BeginTable("…", N)` per render path — never nest. A `BeginTable` inside a parent table cell inherits that cell's width, squeezing inner columns until the right ones get clipped off-screen. Concretely: if an engine `Render*Section()` calls a game virtual hook whose body opens its own `BeginTable`, the engine side MUST render its own sliders inline (no outer table wrap) and let the hook own the table at full window width. See `RenderLightingSection` → `RenderLightingEffectsVisibleTab` / `RenderLightingEffectsLightingTab` and `RenderSoundSection` → `RenderSoundEffects` for the canonical pattern.

**Slider width inside tables**: `WrapperSlider`'s `fWidthMultiplier` defaults to `2.0f`, sized for single-column tabs that take the full window width. Inside a multi-column `BeginTable` each cell is already narrower, so callers MUST pass `1.0f` — otherwise the slider asks for double its cell width, stretching the column off-screen. Canonical: `RenderWaterSection` Specular and Depth tabs.

**First-open drift audit** (`kbDebugInput` only): on first open, a multi-frame pass force-rotates each section's active subtab so every slider call site executes; `WrapperSlider` records touched and missed map keys instead of drawing, then warns on orphan map entries and missed lookups. No effect outside debug builds.

## See Also

- [Game override](../../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/TweaksScreen/) - `game::TweaksScreen`
