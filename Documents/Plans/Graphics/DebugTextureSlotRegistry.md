# Debug Texture Slot Registry

## Context

The F2 debug-texture overlay currently wires slots through a hand-maintained sum:

- `kiMaxDebugTextures = kiMaxSpreadPasses + 16` in `Engine/Data/Shaders/ShaderLayoutsBase.h:127` — a magic constant chosen to fit the current contributors with slack.
- `RenderTargetTexturesLighting.cpp::CreateLightingTextures` mid-function declares `static constexpr int64_t kiTerrainDebugSlotCount = 4` and computes lighting indices as `kiTerrainDebugSlotCount + 0`, `kiTerrainDebugSlotCount + 2 + i`, etc.
- Adding a new debug-texture category requires: (a) bumping `kiMaxDebugTextures` by hand if the slack is consumed, (b) bumping the offset for every later category, (c) remembering to extend `RecreatePipelineGroups` flag-gating in `PipelineManager.cpp:707-731` to include the new category's destroy flag(s), and (d) adding a new format constant in `ShaderLayoutsBase.h` and a matching `else if` branch in `DebugTexture.frag`.

This is fragile. The next addition (e.g. shadow textures, smoke textures, water textures) will quietly break unless every step is followed.

## Proposed Direction (one option)

A small typed registry along the lines of:

```cpp
enum DebugSlotCategory
{
    kDebugSlotTerrainColor,
    kDebugSlotTerrainElevation,
    kDebugSlotTerrainAO,
    kDebugSlotTerrainNormal,
    kDebugSlotLightingDeposit,
    kDebugSlotDepositCombined,
    kDebugSlotSpreadFirst,        // base; spread passes occupy [kDebugSlotSpreadFirst, kDebugSlotSpreadFirst + iPassCount)
    kDebugSlotCombineOutput,
    kDebugSlotCount,
};
```

with `kiMaxDebugTextures = kDebugSlotCount + kiMaxSpreadPasses - 1` so the constant derives from the registry rather than being a magic number. Then `RegisterDebugSlot(kDebugSlotTerrainColor, &mTerrainColorTexture, kiDebugTextureFormatRgb)` style wiring keeps slot ordering and texture wiring in one place per category.

## Files Affected

- `Engine/Data/Shaders/ShaderLayoutsBase.h` — replace magic `+16` with derived constant
- `Engine/Source/Graphics/Managers/RenderTargetTexturesLighting.cpp::CreateLightingTextures` — replace inline arithmetic with named-slot writes
- `Engine/Source/Graphics/Managers/PipelineManager.cpp:707,731` — gate debug pipeline rebuild on the union of destroy flags for every registered category (could be derived from the registry)

## Out of Scope

- Don't pursue if the debug overlay's contributor list is expected to stay small; the current design works for ≤6 categories. Worth doing once a 5th or 6th category lands.
- This is purely engineering hygiene — no observable behavior change.
