# Shader Review — Wind

Files: `WindDeposit.frag`, `WindOccupancyDilate.comp`, `WindSpreadOne.comp`, `WindSpreadTwo.comp`

## PASS

- `Engine/Data/Shaders/Wind/WindDeposit.frag` — scalar layout, correct set 1 per-pipeline bindings, `flat` integer varying, zero-length normalize guarded at line 33.
- `Engine/Data/Shaders/Wind/WindOccupancyDilate.comp` — `atomicAdd` compaction, bounds guard, no divergence hazards worth flagging.

## EXECUTED (prior session)

- `WindSpreadCommon.h:12, 15-18, 35, 41` — `texture()` → `textureLod(..., 0.0f)` to eliminate undefined-LOD in compute stage.
- `WindSpreadOne.comp:75` and `WindSpreadTwo.comp:75` — occupancy epsilon threshold (`> 1e-10f` squared magnitude) prevents denormal latch.
- Prior concern about `WindSpreadCommon.h:70` `pow(fDecayRate, fTimeScale)` producing NaN is wrapper-guaranteed safe (`gWindDecayHigh` max 0.999, `gWindDecayLow` max 0.9 keep `fDecayRate = 1 - mix(...)` strictly positive); comment above the wrapper constructors in `Engine/Source/Ui/WrapperBase.cpp`.

## REMAINING

### `WindSpreadOne.comp` / `WindSpreadTwo.comp`

Performance:
- line 46 — `activeTiles[gl_WorkGroupID.x]` is workgroup-uniform; could hoist into a `shared uint` initialized by invocation 0. Compiler likely already scalarizes; defer unless profiler shows gain.

### Vulkan/API (coupled with `02_Water.md` and `06_Lighting.md`)

- lines 9-35 in both `WindSpreadOne.comp` and `WindSpreadTwo.comp` — no `layout(set = N, ...)` qualifier; all bindings default to set 0. Per convention, per-pipeline SSBOs/samplers belong at `set = 1`. All Wind compute shaders share this deviation.
- **Must coordinate with C++ pipeline-layout side** (`Engine/Source/Graphics/...`) and land in one session with the sibling descriptor-set audits in `02_Water.md` and `06_Lighting.md` so the convention lands consistently subsystem-wide.
