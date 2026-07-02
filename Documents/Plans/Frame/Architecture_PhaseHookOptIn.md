# Architecture: Opt-In Collection Phase Hooks

## Context
Source: /external-architecture-review on Engine/Source (recursive). The `ForEach*` fold helpers call every phase hook on every collection unconditionally, so all 12 engine collections (plus game collections) must define every hook even when empty — ~70+ empty out-of-line stubs, a 12-static-method interface for collections that implement ≤2 hooks with logic, and a `[[maybe_unused]]` uniform-signature discipline the hub CLAUDE.md must codify as a rule. Separately, two collections are excluded from the render walk by a phantom-parameter trick plus a hand-maintained parallel type list.

## Design

### Engine/Source/Frame/FrameUtils.h
- Make phase hooks opt-in in the fold helpers (`ForEach*`, FrameUtils.h:96-136) via `if constexpr (requires { TS::Update(...); })` — a collection that doesn't declare a hook is skipped at compile time [~1h]
- Delete the now-unneeded empty stubs across the engine collections (representative: `WindTrailsUpdate.cpp:23-29,60-66`; `AreaLightsUpdate.cpp:8-10,26-32,65-71`; `Sounds.cpp:35-50` including the empty `Render`; `BillboardsUpdate.cpp:26-32,64-70`) and the game-layer collections that carry the same stubs [~1h]

### Engine/Source/Frame/FrameBase.h
- Replace the exclusion-by-signature-mismatch convention: `SmokeTrailsInterpolate::Render` / `WindTrailsInterpolate::Render` carry an unused `uint16_t uiFrameId` solely so the fold's signature match fails, and the hand-written `InterpolateRenderTypes` list (FrameBase.h:253-256) must track `Collections()` by hand. With opt-in hooks, express manual-render as an explicit trait (e.g. `static constexpr bool kbManualRender`) checked via `if constexpr` in the fold; delete the phantom parameter and the parallel list [~30m]

### Documentation
- Update `Engine/Source/Frame/Collections/CLAUDE.md` (and the add-collection skill's checklist reference if it names the stub requirement): hooks are now opt-in; the `[[maybe_unused]]` uniform-signature rule for stubs is retired [~15m]

## Critical files
- `Engine/Source/Frame/FrameUtils.h`, `FrameBase.h`
- All engine collection Update/Render TUs (stub deletion)
- `Projects/BrokenEngineSandbox/Source/Frame/` game collections (same stub deletion)
- `Engine/Source/Frame/Collections/CLAUDE.md`

## Out of scope
- Any change to phase *ordering* or the `Collections()` tuple contents
- The copy/zero-init contract and helper dedup plans (separate sessions, same files)
- `kCollectionCount` / registration shotgun-surface reduction beyond the render list (the add-collection skill owns that checklist)

## Notes
- Invariant exposure: MODERATE — no CRC/serialization change (hooks are call-sites, not data), but a hook silently not matching the `requires` signature would be *skipped* instead of a compile error; mitigate with a `static_assert` that each collection declares at least the mandatory hooks (`Update`/`Members`), and verify server build compiles both fold variants. Cross-layer edit (game collections carry stubs too)
- Grill decision: `requires`-expression opt-in vs explicit per-collection trait flags — recommend `requires` (zero per-collection boilerplate), with the manual-render trait as the one explicit flag
