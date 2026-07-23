<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T03:39:13.000Z","dependsOn":[]} -->
# Architecture: Opt-In Collection Phase Hooks

## Context
Source: /external-architecture-review on Engine/Source (recursive). The `ForEach*` fold helpers call every phase hook on every collection unconditionally, so all 11 engine collections (plus the game collections) must define every hook even when empty — ~70+ empty out-of-line stubs across the engine leaves alone, a 12-static-method interface for collections that implement ≤2 hooks with logic, and a `[[maybe_unused]]` uniform-signature discipline the hub AGENTS.md must codify as a rule. Separately, two collections are excluded from the render walk by a phantom-parameter trick plus a hand-maintained parallel type list.

## Design

### Engine/Source/Frame/FrameUtils.h
- Make phase hooks opt-in in the fold helpers (`ForEach*`, FrameUtils.h:60-136) via `if constexpr (requires { TS::Update(...); })` — a collection that doesn't declare a hook is skipped at compile time [~1h]
- Delete the now-unneeded empty stubs across the engine collections (representative: `WindTrailsUpdate.cpp:23-29,53-59`; `AreaLightsUpdate.cpp:8-10,26-32,58-64`; `Sounds.cpp:25-40` including the empty `Render`; `BillboardsUpdate.cpp:26-32,57-63`) and the game-layer collections that carry the same stubs [~1h]

### Engine/Source/Frame/FrameBase.h
- Replace the exclusion-by-signature-mismatch convention: `SmokeTrailsInterpolate::Render` / `WindTrailsInterpolate::Render` carry an unused `uint16_t uiFrameId` solely so the fold's signature match fails, and the hand-written `InterpolateRenderTypes` list (FrameBase.h:263-264) must track `Collections()` by hand. With opt-in hooks, express manual-render as an explicit trait (e.g. `static constexpr bool kbManualRender`) checked via `if constexpr` in the fold; delete the phantom parameter and the parallel list [~30m]

### Documentation
- Update `Engine/Source/Frame/Collections/AGENTS.md` (and the add-collection skill's checklist reference if it names the stub requirement): hooks are now opt-in; the `[[maybe_unused]]` uniform-signature rule for stubs is retired [~15m]

## Critical files
- `Engine/Source/Frame/FrameUtils.h`, `FrameBase.h`
- All engine collection Update/Render TUs (stub deletion)
- `Projects/BrokenEngineSandbox/Source/Frame/` game collections (same stub deletion)
- `Engine/Source/Frame/Collections/AGENTS.md`

## Out of scope
- Any change to phase *ordering* or the `Collections()` tuple contents
- The copy/zero-init contract and helper dedup plans (separate sessions, same files)
- `kCollectionCount` / registration shotgun-surface reduction beyond the render list (the add-collection skill owns that checklist)

## Coordination

- Never interleave with the live frame-collection series `Documents/Plans/Frame/Architecture_CollectionHelperDedup.md`, `Documents/Plans/Frame/CollectionReadIndexHardening.md`; each later landing must refresh shared collection-header/TU citations.

## Notes
- Invariant exposure: MODERATE — no CRC/serialization change (hooks are call-sites, not data), but the failure mode inverts: today a signature mismatch is a hard compile error; with `requires`-based opt-in it becomes a silent phase skip. That includes hooks *with logic* on sim-phase families (e.g. `Destroy` on PointLights, the shared Explosions/Pushers and game-collection PostRender hooks) — a skipped sim hook changes behavior identically on both builds (no client/server desync) but silently diverges from old replays. Mitigate with a `static_assert` on the mandatory hooks (`Update`/`Members`) and treat any hook-signature change as a checklist item in the add-collection skill; accept that non-mandatory hooks rely on review
- This deliberately retires the documented uniform-signature/`[[maybe_unused]]`-stub convention in `Frame/AGENTS.md` and `Frame/Collections/AGENTS.md` — both hubs and the add-collection skill must be updated in the same session (Design covers this); do not land the fold change without the doc/skill updates
- Grill decision: `requires`-expression opt-in vs explicit per-collection trait flags — recommend `requires` (zero per-collection boilerplate), with the manual-render trait as the one explicit flag

## Verification Notes
- Verified against source 2026-07-02: fold helpers at FrameUtils.h:60-136 call hooks unconditionally; ~80 empty out-of-line bodies across the 11 engine collection TUs (plus game-layer stubs); `SmokeTrailsInterpolate::Render`/`WindTrailsInterpolate::Render` carry the unused `uint16_t uiFrameId` phantom parameter (documented as deliberate exclusion in both leaf AGENTS.md files); the hand-written `InterpolateRenderTypes` list sits at FrameBase.h:263-264. Headline claim held; engine collection count corrected 12 → 11
