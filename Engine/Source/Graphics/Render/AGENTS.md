# Render - Per-Frame GPU Data

Client-only population of mapped Global and Main layouts before their record-once command buffers submit. Each render subsystem owns its layout region; tunable-dependent staging remains separate from per-frame phase state.

## Ordering and Publication

- Global population precedes Main. Within Global, Smoke precedes Wind because wind consumes smoke's current and previous world areas.
- Main rendering runs collection `BeginRender`, per-coord `Render`, collection `EndRender`, lighting-spread gate publication, then debug publication, with the camera coord first. The no-renderable-coord path still runs begin/end publication so every indirect count reaches zero instead of ghost-drawing prior-frame instances.
- Lighting deposit clears its bounded rectangle by indirect compute every frame, then deposit raster loads its attachments and discards fragments outside that rectangle. This leaves the rest of each attachment untouched without relying on command-buffer re-recording.
- The lighting spread, combine, temporal, and history-copy chain shares one update interval. A refresh atomically advances its current and history mappings and validity bounds, then publishes bounded work; a skipped interval retains all of them and publishes zero chain work while deposit continues. Reset resolves from current data only, and history is refreshed only after temporal resolution.
- Downstream amplitude gates may clear an upstream count only when they also skip the corresponding array write. Keep count and data publication paired.
- Write-combined mapped layouts are write-only. Compute dependent values in CPU staging state and copy each populated region once.

## Precision and History

- Water phase and camera-relative UV origins are computed in `double`, reduced with `std::fmod`, then cast to float. Integrate reduced phase from per-frame size, speed, and delta time so tunable changes do not jump.
- Rotate camera origins into the shader's pattern space before reduction, and preserve integral shader-side wrap multipliers. These CPU reductions are the precision owner for distant-world water sampling.
- Shadow and lighting footprints snap to their deposit grids and publish current/previous areas for temporal sampling. Recreation resets seed history from the current area for one frame.
- Smoke and wind spread remap through current and previous world areas. Their enable, disable, recreate, and occupancy paths must clear or drain stale tiles without relying on command-buffer re-recording.

## Area Roles

The visible area anchors water and geometry coverage. Shadow uses a sunward-expanded, texel-snapped footprint; lighting uses the light-deposit grid shared by spread, combine, and temporal passes. Grid snapping and camera-height policy belong to Graphics (`../AGENTS.md`).

## See Also

- Water shaders (`../../../Data/Shaders/Water/AGENTS.md`) - Shader-side phase and sampling constraints
- Smoke shaders (`../../../Data/Shaders/Smoke/AGENTS.md`) and Wind shaders (`../../../Data/Shaders/Wind/AGENTS.md`) - Temporal remapping
