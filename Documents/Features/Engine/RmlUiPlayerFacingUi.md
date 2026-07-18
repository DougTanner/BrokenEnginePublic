# RmlUi for Player-Facing UI

## Context

The player-facing UI (main menu, pause/graphics/sound menus, modal, HUD) is Dear ImGui, restyled by the selectable-theme facelift (engine `UiTheme` palettes in `ImGuiManager`, custom `MenuButton`/`DrawPanelAccents` chrome in game `MenuUtils`). That approach tops out at "clean flat digital": layouts are code-authored, animation is limited to simple interpolants, and there is no designer-editable art direction (textures, nine-patch, transitions, rich text flow). RmlUi (MIT, HTML/CSS-like, C++17, v6.x) is the only mature open-source middleware fit: production Vulkan reference backend, FreeType text, MVC data binding, CSS animations/transitions/decorators, and gamepad spatial navigation — proven for game menus/HUD by the vkQuake-RmlUi port. This plan adopts RmlUi for the ~8 player-facing screens only; ImGui remains permanently for the TweaksScreen dev family and ImPlot (curve editor, profiler graphs), which RmlUi cannot replace.

## Design

- **Vendor RmlUi** under `ThirdParty/RmlUi/` following the prebuilt-static-lib pattern in `ThirdParty/Prebuilts/` (new unity unit(s) + include dirs across the three ThirdParty configs). RmlUi requires FreeType by default — either vendor FreeType (license: FTL/GPLv2 dual — needs acceptance) or implement a custom RmlUi font engine that consumes the retained raw Noto Sans SC OTF and provides the required glyph rasterization/atlas path; decide during grill.
- **`Rml::RenderInterface` implementation** (new engine class, e.g. `engine::RmlRenderInterface`, client-only): compiled-geometry buffers via VMA, texture load through `TextureManager`/DataPacker chunks, scissor + transform support. Render inside the existing ImGui render pass owner (`ImGuiManager`, or a sibling manager sharing its `VkRenderPass` that draws over the finished frame before the ImGui submit) so the Global → Main → UI → present submission chain is unchanged.
- **`Rml::SystemInterface`**: engine clock, clipboard via Win32, logging to `LOG`.
- **Resolution independence**: feed RmlUi's context dp-ratio (its density-independent-pixel scale) from `engine::UiScale()` (framebuffer height / 2160 reference, `ImGuiManager.h`), re-set on framebuffer resize, so RmlUi screens share the exact resolution-independence convention as the surviving ImGui UI (TweaksScreen/ImPlot) rather than authoring a second scaling scheme.
- **Opaque occlusion**: opaque RmlUi panels register through the existing `ImGuiManager::RegisterOpaqueRect` depth pre-pass so the 3D scene is occluded exactly as ImGui panels are today.
- **Input routing**: forward Win32 messages from the `Main.cpp` WndProc to the RmlUi context alongside `ImGui_ImplWin32_WndProcHandler`, with a capture arbitration rule (RmlUi documents get first refusal when a player-facing screen is active).
- **Documents + data binding**: one `.rml`/`.rcss` document per screen (`MainMenuScreen`, `PauseMenuScreen`, `GraphicsMenuScreen`, `SoundMenuScreen`, `DeathMenuScreen`, `ModalScreen`, `HudScreen`), packed as Raw assets via DataPacker. Settings controls bind to the existing `engine::Wrapper` globals through RmlUi data models (replacing `MenuUtils` `WrapperSlider`/`WrapperToggle`/`WrapperPlusMinus` for these screens); server-confirmation gating keeps using `engine::NetworkUiControl`.
- **Localization**: feed the UTF-32 table in the game `Localization.h` through RmlUi's string interface; CJK via the Chinese font source already packed (`data::kRawNotoSansSCLightotfCrc`).
- **Allocation tracking**: RmlUi allocates during document load and layout; wrap load/reload in `ScopedSuppressAllocationTracking` and audit steady-state per-frame allocations (install `Rml::Allocator` hooks if needed).
- **Migration order**: infrastructure → ModalScreen (smallest) → menus → HUD last (slide animation + occlusion interplay). ImGui and RmlUi coexist throughout; each screen is deleted from ImGui only when its RmlUi document reaches parity.

## Critical files

- `ThirdParty/RmlUi/` (new vendored library), `ThirdParty/Prebuilts/` unity units and the three ThirdParty configs
- `Engine/Source/Graphics/Managers/ImGuiManager.{h,cpp}` — render-pass sharing, `RegisterOpaqueRect`, submission ordering
- New `engine::RmlRenderInterface` / `RmlSystemInterface` (location: `Engine/Source/Graphics/Managers/` or `Engine/Source/Ui/`)
- `Engine/Source/Main.cpp` — WndProc input routing
- `Projects/BrokenEngineSandbox/Source/Ui/Screens/*.{h,cpp}` — per-screen replacement (~125 ImGui call sites), `MenuUtils.{h,cpp}` chrome/binding helpers retired for these screens
- `Projects/BrokenEngineSandbox/Source/Ui/Localization.h` — string bridge
- New `.rml`/`.rcss` assets under `Engine/Data/Raw/` or a game data directory (DataPacker Raw pipeline)

## Out of scope

- TweaksScreen family (`engine::TweaksScreenBase` + game override) — stays ImGui permanently
- ImPlot consumers (curve editor `CurveWidget`, profiler graphs) — stays ImGui/ImPlot
- Removing the ImGui dependency or its Vulkan backend — both UI systems coexist
- Lua or any scripting plugin for RmlUi documents
- Re-authoring visual design beyond porting the existing themed look

## Notes

- **Revisit When**: art direction outgrows the ImGui facelift — a designer needs textured/nine-patch panels, CSS-grade animation/transitions, or non-programmer-editable layouts; or a new screen class (e.g. campaign/lobby flows) makes code-authored ImGui layouts a bottleneck.
- Invariant exposure: client-only (no determinism/CRC impact); no `Frame::kiVersion` impact; touches allocation-tracked main-loop paths (see allocation bullet); adds a new third-party dependency requiring explicit license acceptance (RmlUi MIT; FreeType FTL/GPLv2 dual — the open decision to pre-stage for `/external-grill-plan`).
- Scores assume the facelift-era `MenuUtils`/`Wrapper` structure is current when executed; refresh citations then.
