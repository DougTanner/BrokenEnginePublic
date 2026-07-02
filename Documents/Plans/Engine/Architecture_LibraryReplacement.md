# Architecture: Library Replacement — TextManager → imgui

## Context
Source: /external-architecture-review on Engine/Source (recursive). The library-replacement survey found the codebase already outsources nearly every wheel-shaped problem (compression, allocation, networking, clipping, image I/O, audio device layer, stack capture, noise). The one substantive candidate: the bespoke overlay-text stack. **Decision plan (present options).**

## Design

### Candidate: delete the TextManager overlay-text stack, render via imgui draw lists
- **Module/cluster**: `Engine/Source/Graphics/Managers/TextManager.h` (152 lines) + `TextManager.cpp` (120 lines); plus the text-pass glue: `kPipelineText`/quad-buffer entries in `PipelineManager`/`BufferManager`, the `kGpuTimerText` record block in `CommandBufferRecordMain.cpp`, `Text` shaders under `Engine/Data/Shaders`, and downstream the DataPacker font-bake path and the eager `Font` chunk type in `FileManager`
- **Lines removable**: ~250 in Engine/Source directly; realistically 400-600 repo-wide once shaders + DataPacker font baking + `common::Character` go
- **Proposed library**: **imgui** (MIT — on the ThirdParty/CLAUDE.md allow list; **already imported**, so this is coverage extension, not a new import). Its background/foreground draw-list text is the standard for debug/profile overlays; `ImGuiManager` already exists and re-records its CB per frame
- **Fit**: every `TextAreas` consumer is debug/profile (`kTextDebug`, `kTextGraphics`, `kTextProfile*` — `TextManager.h:8-21`); no gameplay text exists. A bespoke Vulkan pipeline + bitmap-font atlas + pack-file font chunk is maintained solely to draw overlay text imgui can draw
- **Risks**: (a) text moves out of the record-once Main pass into the ImGui submission — GPU-cost attribution changes and the `kGpuTimerText` row dies; (b) visual change (imgui font vs baked EFIGS atlas); (c) `ProfileScreens.cpp` screen-fraction `fX/fY` positioning ports to draw-list coordinates; (d) overlay volume (4096 chars/area) is trivially within the ImGui CB budget. No determinism exposure (client-only, presentation-only)
- **Confidence**: MEDIUM. If taken: also skip the `WriteQuads` de-template item in `Graphics/Refactor_TextureManagersQuickWins.md` (moot) [~1-2 days]

### Rider: drop the unused tinyobjloader import
- `ThirdParty/CLAUDE.md:35` notes tinyobjloader is compiled with **zero call sites** — inverse cleanup; remove from the ThirdParty build + docs [~15m]

### Evaluated and rejected (recorded so future sweeps don't re-litigate)
- **Recast/Detour** (zlib) for the nav cluster (~1,450 LOC): REJECTED — nav runs on the shared client/server sim path with bit-reproducible CRC requirements; current design was built around Clipper2's int64 determinism; Recast is float-based with no cross-machine guarantee
- **Tracy** (BSD-3) for the profiler: complement, not replacement — the in-house system's load-bearing features (always-on overlay, server GDI display, allocation-count diffs) aren't what Tracy provides; not filed
- **vk-bootstrap** (MIT) for instance/device boot: marginal — most of `InstanceManager`/`DeviceManager` is policy, not boilerplate; net deletion modest; not filed
- **Crashpad/Breakpad, ozz-animation, tinyspline, DirectXTK Keyboard** (previously rejected, documented): all rejected on integration weight or design mismatch
- Copyleft-tempting list: none — best-in-class was permissive for every cluster surveyed

## Critical files
- `Engine/Source/Graphics/Managers/TextManager.{h,cpp}`, `PipelineManager.cpp`, `BufferManager.cpp`, `CommandBufferRecordMain.cpp`, `ImGuiManager.{h,cpp}`
- `Engine/Source/Profile/ProfileScreens.cpp`
- `Engine/Data/Shaders/Text*`, DataPacker font-bake path, `Engine/Source/File/FileManager.*` (Font eager chunk)

## Out of scope
- Any other library import or replacement (all rejected above)
- Gameplay/world-space text (none exists; if ever needed, that's a Features/ item)
- ImGui version upgrades

## Notes
- Invariant exposure: none for determinism/CRC/wire. Removing the `Font` pack type touches `.pack` contents and the DataPacker — likely a `DataHeader::kiVersion` bump (repack-all); flag at grill. The `kGpuTimerText` removal touches the game GPU-timer enum the Profile overlay reads
- Grill decision: proceed with the replacement vs accept the bespoke stack (the deciding question: is the EFIGS baked-atlas look worth a dedicated pipeline + asset type?). Decision plan — deliverable is the choice; implementation follows as its own execution
