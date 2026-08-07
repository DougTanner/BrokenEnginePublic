# Render - Per-Frame GPU Data

Client-only population of mapped Global and Main layouts before their record-once command buffers submit. Each render subsystem owns its layout region; tunable-dependent staging remains separate from per-frame phase state.

Render free-runs between simulation ticks and sits outside the CRC (the per-tick checksum that keeps client and server bit-identical), so nothing computed here may feed simulation state.

## Ordering and Publication

- Global population precedes Main. Within Global, Smoke precedes Wind because wind consumes smoke's current and previous world areas.
- Main rendering runs collection `BeginRender`, per-coord `Render`, collection `EndRender`, lighting-spread gate publication, then debug publication, with the camera coord first. The no-renderable-coord path still runs begin/end publication so every indirect count reaches zero instead of ghost-drawing prior-frame instances.
- Lighting deposit clears its attachments through the render pass load operation every frame, so no separate clear pass is recorded.
- The lighting spread, combine, temporal, and history-copy chain shares one update interval. A refresh atomically advances its current and history area mappings, then publishes full-texture work; a skipped interval retains both and publishes zero indirect draw and compute work — the spread render passes still run their unconditional attachment clears — while the combined outputs, history, and deposit continue. Reset resolves from current data only, and history is refreshed only after temporal resolution.
- Downstream thresholds that the amplitude must cross before the effect applies may clear an upstream count only when they also skip the corresponding array write. Keep count and data publication paired.
- Write-combined mapped layouts are write-only. Compute dependent values in CPU staging state and copy each populated region once.
- State that crosses files, or that something outside this directory resets, lives as `inline` globals in `Render.h`. Single-file edge detectors and latches (the statics that remember the previous frame's value) stay function-local or file-static — do not promote them, because a promoted latch looks externally resettable and invites a second writer.
- Every latch assumes its entry point runs exactly once per frame. The reduced-time accumulators in `WaterUniforms.cpp` integrate per call, so a second call in one frame — an extra preview pass, a second `RenderFrameGlobal` for a capture — silently doubles water scroll speed with no error anywhere.
- The whole day cycle resolves on the CPU in `GlobalUniforms.cpp`: sun and moon angle and direction, the piecewise color and ambient ramps, the separate night-gate and moon envelopes, and every product of those that more than one shader reads. Shaders only read the finished uniform values, so a new day-cycle term belongs here rather than recomputed per pixel.

## Precision and History

- Water phase and camera-relative UV origins are computed in `double`, reduced with `std::fmod`, then cast to float. Integrate reduced phase from per-frame size, speed, and delta time so tunable changes do not jump.
- Rotate camera origins into the shader's pattern space before reduction, and preserve integral shader-side wrap multipliers. These CPU reductions are the precision owner for distant-world water sampling.
- Lighting snaps its footprint to the light-deposit grid; shadow snaps to its own world-sized texel grid. Both publish current and previous areas for temporal sampling, and recreation reseeds history from the current area for one frame.
- Smoke and wind spread remap through current and previous world areas. Their enable, disable, recreate, and occupancy paths must clear or drain stale tiles without relying on command-buffer re-recording.

## Area Roles

The visible area anchors water and geometry coverage. Shadow uses a sunward-expanded, texel-snapped footprint; lighting uses the light-deposit grid shared by spread, combine, and temporal passes. Grid snapping and camera-height policy belong to Graphics.

## See Also

- Water shaders (`../../../Data/Shaders/Water/AGENTS.md`) - Shader-side phase and sampling constraints
- Smoke shaders (`../../../Data/Shaders/Smoke/AGENTS.md`) and Wind shaders (`../../../Data/Shaders/Wind/AGENTS.md`) - Temporal remapping
