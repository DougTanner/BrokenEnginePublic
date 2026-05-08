# Shader Polish Followups (TileSize + Readonly)

## Context

Two trivial single-line GLSL polish items surfaced by the cross-impact audit during execution of `08_Smoke`. Each is the same pattern as a fix that landed for the smoke shaders; both apply to sibling shaders that the original plan did not cover.

## Design

### Item 1 — `Engine/Data/Shaders/Wind/WindDeposit.frag`

The deposit fragment shader hardcodes the compute-tile divisor as a literal `8`:

```glsl
ivec2 i2TileCoord = ivec2(gl_FragCoord.xy) / 8;
```

Replace with `kiComputeTileSize` from `ShaderLayoutsBase.h` (already reachable through the existing `ShaderLayouts.h` include, same as the smoke deposit fragment after `08_Smoke`):

```glsl
ivec2 i2TileCoord = ivec2(gl_FragCoord.xy) / kiComputeTileSize;
```

This keeps the deposit-side tile coordinate in lockstep with the spread compute workgroups (also `kiComputeTileSize × kiComputeTileSize`), so a future change to the constant cannot silently desync the two halves of the wind hierarchical dispatch.

### Item 2 — `Engine/Data/Shaders/Smoke/SmokeOccupancyDilateRemap.comp`

The dilate-remap variant binds the same `occupancyBuffer` SSBO that `SmokeOccupancyDilate.comp` does, and like its sibling only ever reads from it (the only access is a bitwise `&` test inside the dilate loop; writes go to `activeTileBuffer`). After `08_Smoke` qualified the sibling buffer with `readonly`, the same qualifier should land here for symmetry:

```glsl
layout (binding = ?) readonly buffer occupancyBuffer
{
    uint occupancy[];
};
```

Find the `occupancyBuffer` declaration in the file and add `readonly` between the binding and the `buffer` keyword. Confirm via Read that all accesses are pure loads.

## Out of scope

- Auditing other shaders for similar `/ 8` literals (e.g., any future deposit shader). Only `WindDeposit.frag` was flagged by the cross-impact agent.
- Auditing other SSBOs in either compute file for missing `readonly` / `writeonly`. Only the `occupancyBuffer` in `SmokeOccupancyDilateRemap.comp` was flagged.
- Renaming, reformatting, or restructuring beyond the single-line qualifier and divisor changes.
- Any C++ side. Both items are GLSL-only; the runtime descriptor types and pipeline layouts are unaffected.

## Acceptance criteria

- `WindDeposit.frag` no longer contains a literal `/ 8` for tile-coord division; the compute-tile divisor is `/ kiComputeTileSize`.
- `SmokeOccupancyDilateRemap.comp`'s `occupancyBuffer` SSBO declaration carries the `readonly` qualifier.
- Sandbox `BrokenEngineSandbox.sln` Debug client builds cleanly via `/compile`; the DataPacker pre-build re-emits SPIR-V for both shaders without glslang or spirv-cross errors.

## Critical files

- `Engine/Data/Shaders/Wind/WindDeposit.frag` (line ~47 at the time of writing)
- `Engine/Data/Shaders/Smoke/SmokeOccupancyDilateRemap.comp` (the `occupancyBuffer` declaration)

## Notes

- These two changes are mechanical and have no behavioral consequence: `kiComputeTileSize` is `8` per `ShaderLayoutsBase.h:138`, so the wind divisor is numerically unchanged; `readonly` is a hint qualifier that may unlock optimizer read-paths but never alters semantics for read-only access.
- Surfaced by the `/next-plan` cross-impact audit during the `08_Smoke` execution session. The original `08_Smoke` plan was scoped to four files in `Engine/Data/Shaders/Smoke/` and did not touch sibling deposit/dilate variants.
