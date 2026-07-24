<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-02T03:39:13.000Z","dependsOn":[]} -->
# Architecture: Opt-In Collection Phase Hooks

## Context

Source: /external-architecture-review on Engine/Source (recursive). The `ForEach*` fold helpers in `Engine/Source/Frame/FrameUtils.h` call every phase hook on every collection unconditionally, so all 11 engine collections (plus the game collections) must define every hook even when empty. Consequences today:

- ~80 empty out-of-line `[[maybe_unused]]`-stub bodies across engine collection TUs, plus the same pattern in the game-layer collections (`Targets` is entirely no-op phase hooks).
- A wide static-method interface for collections that implement only one or two hooks with logic.
- A uniform-signature discipline the docs must codify as a rule (`Engine/Source/Frame/Collections/AGENTS.md`: "Phase hooks use the generic signatures expected by `ForEach*` dispatch, including unused parameters").
- Two collections (`SmokeTrailsInterpolate`, `WindTrailsInterpolate`) are excluded from the interpolate-render walk by a phantom-parameter trick — their `Render` carries an unused trailing `uint16_t uiFrameId` solely so the fold's signature match fails — plus a hand-maintained parallel type list `InterpolateRenderTypes` (`Engine/Source/Frame/FrameBase.h:263-264`) that must track `Collections()` by hand.

Decision record (resolved at planning): dispatch opt-in uses `requires`-expression detection (zero per-collection boilerplate); the manual-render exclusion is the one explicit trait flag (`static constexpr bool kbManualRender`). Explicit per-collection trait flags for every hook were considered and rejected.

## Design

### 1. Opt-in hook dispatch — `Engine/Source/Frame/FrameUtils.h`

In the `ForEach*` fold helpers (`FrameUtils.h:60-136`), make each hook call conditional on the hook's existence: inside the fold, invoke each type through an immediately-invoked lambda containing `if constexpr (requires { TS::Hook(<exact current arguments>); }) TS::Hook(...);`, so a collection that does not declare a hook is skipped at compile time. Exact per-helper policy:

- **Mandatory (unconditional call plus `static_assert` that the hook exists, so absence stays a hard compile error):** `ForEachInterpolateUpdate` (`TS::Update(rCurrent, rPreviousFrame)`) and `ForEachPostRenderUpdate` (`TS::Update(rFrame, rPreviousFrame, rStaticData)`).
- **Opt-in (silent compile-time skip when absent):** `ForEachInterpolateRender`, `ForEachBeginRender`, `ForEachEndRender`, `ForEachRegister`, `ForEachGraphicsResources`, `ForEachPostRenderPreCollision`, `ForEachPostRenderPostCollision`, `ForEachPostRenderAreaDamage`, `ForEachPostRenderTransfer`, `ForEachPostRenderDestroy`, `ForEachPostRenderSpawn`.
- **Untouched:** `AllocateAndCopyCollections` and `LogDifferencesCollections` (not phase hooks; `AllocateAndCopy`/`LogDifferences` stay mandatory), and every helper above `TypeList` in the file. [~1h]

### 2. Manual-render trait replaces the phantom parameter — `FrameBase.h`, SmokeTrails, WindTrails

- Declare `static constexpr bool kbManualRender = true;` on `SmokeTrailsInterpolate` and `WindTrailsInterpolate` (their headers: `Engine/Source/Frame/Collections/SmokeTrails/SmokeTrails.h`, `.../WindTrails/WindTrails.h`). `ForEachInterpolateRender` skips a type via `if constexpr` when the trait is present (detect with `requires { TS::kbManualRender; }`); absence means the collection participates normally.
- Delete the trailing `uint16_t uiFrameId` parameter from `SmokeTrailsInterpolate::Render` (declaration `SmokeTrails.h:62`, definition `SmokeTrailsRender.cpp:40`) and `WindTrailsInterpolate::Render` (declaration `WindTrails.h:58`, definition `WindTrailsRender.cpp:53`) — the parameter is unused in both bodies. Drop the `uiFrameId` argument at the two manual call sites in `RenderFrameMain` (`Engine/Source/Graphics/Render/MainUniforms.cpp:535-536`).
- Delete the hand-written `InterpolateRenderTypes` alias and its explanatory comment (`FrameBase.h:261-264`); change the walk call site `ForEachInterpolateRender(InterpolateRenderTypes{}, ...)` (`Engine/Source/Frame/FrameBase.cpp:268`) to pass `InterpolateTypes{}`. [~30m]

### 3. Delete the now-unneeded empty stubs

For each engine and game collection, delete every phase hook from the opt-in list in step 1 whose out-of-line body is empty (or comment-only): remove both the header declaration and the empty definition — the `requires` check tests declaration presence, so a declared-but-deleted body would be a link error and a declared empty body would still be dispatched. Mandatory `Update` hooks stay even when empty (e.g. `Targets`, whose phase methods are all intentional no-ops today, keeps only its `Update` stubs).

Representative engine sites (verified): `WindTrailsUpdate.cpp:23-29,53-59` (plus the empty `WindTrailsInterpolate::Update` at 19-21, which stays — mandatory); `AreaLightsUpdate.cpp:8-10` (mandatory, stays) and `26-32,58-64`; `Sounds.cpp:11-13,25-40` including the empty `Render`; `BillboardsUpdate.cpp:26-32,57-63`. Apply the same rule to the remaining engine collections (Explosions, HexShields, PointLights, Puffs, Pushers, WindRadials) and the game collections under `Projects/BrokenEngineSandbox/Source/Frame/Collections/` (Blasters, Missiles, Players, Spaceships, Targets). Hooks with logic are untouched. [~1h]

### 4. Documentation and skill updates (same session — do not land the fold change without them)

- `Engine/Source/Frame/Collections/AGENTS.md`: replace the "Phase hooks use the generic signatures … including unused parameters. Do not add per-collection signature variants." bullet with the opt-in rule (declared hooks must match the dispatch signature exactly; undeclared hooks are skipped; `Update` is mandatory; `kbManualRender` opts out of the interpolate-render walk).
- `Engine/Source/Frame/Collections/WindTrails/AGENTS.md` (bullet describing the `uiFrameId` exclusion trick): rewrite for the `kbManualRender` trait.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/AGENTS.md` ("the empty bodies exist only to satisfy the engine `ForEach*` dispatch interface, so don't remove them"): rewrite for the post-deletion state.
- `.agents/skills/add-collection/SKILL.md` (lines 62-63 "Preserve the generic phase signatures exactly, even for no-op hooks" and 125-126 hook checklist): hooks are now opt-in; retire the no-op-stub requirement; add a checklist caution that any hook-signature change silently drops the hook from dispatch. [~15m]

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change below; add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires.

**In scope:**

- `Engine/Source/Frame/FrameUtils.h` — only the `ForEach*` helper bodies listed in step 1.
- `Engine/Source/Frame/FrameBase.h` — only the `InterpolateRenderTypes` alias and its comment (`261-264`).
- `Engine/Source/Frame/FrameBase.cpp` — only the `ForEachInterpolateRender` argument at line 268.
- `SmokeTrails.h`/`SmokeTrailsRender.cpp`, `WindTrails.h`/`WindTrailsRender.cpp` — only the `kbManualRender` declaration and the `Render` `uiFrameId` parameter removal.
- `Engine/Source/Graphics/Render/MainUniforms.cpp` — only the two `Render` call arguments at 535-536.
- Engine and game collection headers/TUs — only deletion of empty opt-in phase-hook declarations and definitions per step 3; no other edits in those files.
- The four documentation/skill regions named in step 4.

**Out of scope:**

- Any change to phase *ordering* or the `Collections()` tuple contents.
- The copy/zero-init contract and helper dedup plans (separate sessions, same files); `AllocateAndCopy`/`LogDifferences` dispatch.
- `kCollectionCount` / registration shotgun-surface reduction beyond the render list (the add-collection skill owns that checklist).
- Any behavior change inside hooks with logic; any signature change other than the `uiFrameId` removal.

## Critical files

- `Engine/Source/Frame/FrameUtils.h`, `FrameBase.h`, `FrameBase.cpp`
- All engine collection headers and Update/Render TUs (stub deletion), `Engine/Source/Graphics/Render/MainUniforms.cpp`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/` game collections (same stub deletion)
- `Engine/Source/Frame/Collections/AGENTS.md`, `.../WindTrails/AGENTS.md`, `Projects/.../Targets/AGENTS.md`, `.agents/skills/add-collection/SKILL.md`

## Risk tier

Tier 3. Trigger: the change rewrites the frame phase-dispatch mechanism spanning independently owned engine and game frame subsystems, and its failure mode is a silent sim-phase skip (see invariants below). No CRC/serialization/data-layout change — hooks are call sites, not data.

Invariant exposure: today a hook-signature mismatch is a hard compile error; with `requires`-based opt-in it becomes a silent compile-time skip. That includes hooks *with logic* on sim-phase families (e.g. `Destroy` on PointLights, the shared Explosions/Pushers and game-collection PostRender hooks) — a skipped sim hook changes behavior identically on both builds (no client/server desync) but silently diverges from old replays. Mitigation: the mandatory-`Update` `static_assert`s in step 1, the diff-decisive acceptance criteria below, and the add-collection checklist caution in step 4; non-mandatory hooks rely on review thereafter.

## Acceptance criteria

- Client and server both compile (`/compile`); the mandatory-`Update` `static_assert`s are present in `ForEachInterpolateUpdate`/`ForEachPostRenderUpdate`.
- Diff-decisive: every deleted hook body was empty or comment-only — no hook containing logic is deleted or stops being dispatched. Reviewer verifies from the diff alone.
- `InterpolateRenderTypes` no longer exists; `SmokeTrailsInterpolate::Render`/`WindTrailsInterpolate::Render` no longer take `uiFrameId`; grep for `uiFrameId` in `Collections/` returns no phase-hook signatures.
- No remaining empty opt-in phase-hook stubs in engine or game collection TUs (empty mandatory `Update` bodies excepted).
- The four documentation/skill regions in step 4 reflect the opt-in contract.

## Coordination

- Never interleave with the live frame-collection series `Documents/Plans/Frame/Architecture_CollectionHelperDedup.md`, `Documents/Plans/Frame/CollectionReadIndexHardening.md`; each later landing must refresh shared collection-header/TU citations.

## Verification Notes

- Verified against source 2026-07-24: fold helpers at `FrameUtils.h:60-136` call hooks unconditionally; empty stub bodies confirmed at the representative engine sites cited in step 3 and throughout the game collections; the phantom `uint16_t uiFrameId` parameter confirmed at `SmokeTrails.h:62`/`WindTrails.h:58` with definitions at `SmokeTrailsRender.cpp:40`/`WindTrailsRender.cpp:53`, manual calls at `MainUniforms.cpp:535-536`; `InterpolateRenderTypes` at `FrameBase.h:263-264`, sole consumer `FrameBase.cpp:268`; engine collection count 11 (`FrameBase.h:231`).
- Drift since the 2026-07-02 plan draft: the uniform-signature/stub convention is documented in `Engine/Source/Frame/Collections/AGENTS.md`, the WindTrails leaf AGENTS.md, and the game Targets leaf AGENTS.md — not in `Engine/Source/Frame/AGENTS.md`, and the SmokeTrails leaf AGENTS.md no longer mentions the exclusion. Step 4 lists the current locations.
