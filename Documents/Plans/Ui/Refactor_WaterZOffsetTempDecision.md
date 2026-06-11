# Refactor: gWaterZOffsetTemp Promote or Remove

## Context

Source: /external-refactor-clean on `Engine/Source/Ui` (non-recursive). **Decision plan (present options).**
`gWaterZOffsetTemp` (`WaterWrappersBase.h:81`, `WaterWrappersBase.cpp:103`) is marked `// DT: TEMP` end to
end — the wrapper, its uniform write (`Graphics/Render/GlobalUniforms.cpp:528`, member `fWaterZOffsetTemp`,
itself comment-marked), and even the user-facing Tweaks slider label "DT: TEMP Z Offset"
(`Ui/Screens/TweaksScreen/TweaksScreenWater.cpp:86`) — yet it ships as a live tunable. Default is 0.0f
(inert authoring offset). The sibling `// DT: TEMP` markers on `gWaterFresnel`/`gWaterNoiseFrequency`/
`gWaterNoiseAmount` are comment-only deletions owned by `Ui/Refactor_StyleMechanics.md`; this one needs an
actual decision because the "Temp" name is baked into the wrapper symbol, the shader-layout member, and the
slider label.

## Design

Two options — resolve in the grill:

- **Option A — promote (recommended if the offset is still useful for water authoring):** rename
  `gWaterZOffsetTemp` → `gWaterZOffset`, the uniform member `fWaterZOffsetTemp` → `fWaterZOffset` (C++
  layout struct + consuming shader), and the slider label to "Z Offset"; drop all `DT: TEMP` markers.
  Mechanical rename, compile-checked on the C++ side; shader recompile for the renamed member. [~15m]
- **Option B — remove:** delete the wrapper pair lines, the slider registration, the uniform member write,
  the layout member (`ShaderLayoutsBase.h:392`), and the two shader-side consumptions in
  `Engine/Data/Shaders/Water/Water.vert` — `:53` uses it as the literal Z of one position branch
  (`vec3(f2WorldPosition, globalLayout.fWaterZOffsetTemp)` → becomes `0.0`) and `:80` adds it
  (`f3OutPosition.z += ...` → delete the line). Default 0.0f means removal is value-neutral for
  anyone not actively dragging the slider. [~15m]

## Critical files
- `Engine/Source/Ui/WaterWrappersBase.h` (`:81`), `Engine/Source/Ui/WaterWrappersBase.cpp` (`:103`)
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` (`:528`)
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenWater.cpp` (`:86`, slider label "DT: TEMP Z Offset")
- `Engine/Data/Shaders/ShaderLayoutsBase.h` (`:392`, `float fWaterZOffsetTemp INIT; // DT: TEMP`)
- `Engine/Data/Shaders/Water/Water.vert` (`:53` and `:80`, the two consumptions)

## Out of scope
- The other three `// DT: TEMP` markers in the same file — comment-only, owned by
  `Ui/Refactor_StyleMechanics.md` (same file; co-schedule).
- Any change to the offset's default or range — both options are value-preserving for the shipped default.
- The Water wrapper ordering/stale-comment items — `Ui/Architecture_WaterWrapperInvariants.md`.

## Acceptance criteria
- No `DT: TEMP` marker remains on this tunable anywhere in the chain (wrapper, uniform, shader, slider
  label) — either because the chain was renamed (A) or deleted (B); client builds clean and water renders
  identically at the default value.

## Notes
- Client-presentation tuning only — server reads wrappers at defaults and the value never feeds simulation;
  no determinism/CRC, `kiVersion`, replay, or network exposure. Both options require a DataPacker shader
  recompile: A renames the layout member, B removes it (shifting `GlobalLayout` offsets — `ShaderLayoutsBase.h`
  is the shared C++/GLSL header). Grill decision: A vs B (is the authoring offset still wanted?).

## Verification Notes (2026-06-10)
- Full `DT: TEMP` chain re-verified by repo-wide grep of `gWaterZOffsetTemp`/`fWaterZOffsetTemp`: wrapper
  (`WaterWrappersBase.h:81`, `.cpp:103`, default 0.0f range [-10, 10]), uniform write
  (`GlobalUniforms.cpp:528`), slider (`TweaksScreenWater.cpp:86`, engine-side TweaksScreen — original plan
  cited a wrong `Projects/` path, corrected), layout member (`ShaderLayoutsBase.h:392`), and shader
  consumers (`Water.vert:53` — Z of one position branch — and `:80` — additive offset). Every link carries
  the marker. No other consumer exists.
- The shader-side consumer **does** exist, so Option B's scope as written (delete the full chain including
  both `Water.vert` sites) is correct; concrete locations replaced the original "locate at execution" grep
  instruction. Noted that `Water.vert:53` needs `0.0` substituted, not just line deletion.
- Cross-plan boundaries hold: the other three `// DT: TEMP` markers (`WaterWrappersBase.cpp:116-118`) are
  comment-only and owned by `Ui/Refactor_StyleMechanics.md`; same file — co-schedule.
