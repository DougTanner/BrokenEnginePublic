# Island Bindless-Slot Descriptor Robustness

## Context

Two pre-existing latent bugs in the per-island bindless-slot descriptor machinery, surfaced during an audit of the recent island-eviction / slot-hardening work. **Both are out of scope of that hardening** — the hardening added a slot free-list, `UnregisterBindingsForKey`, per-slot single-element array writes, and elevation eviction, but it did not introduce either bug. The island-browser churn the hardening enables (slots minted and reclaimed repeatedly) simply makes Issue 1 *reachable*; Issue 2 remains unreachable at the current ~3-template ship count.

Both live in the same machinery — `PipelineManager::VerifyAllDescriptorGenerations`, `TextureDescriptors` array bindings, the `kiMaxIslands` budget, and `IslandTerrain::AcquireTextureSlot` per-slot registration — so they are tracked as one plan.

Related: `Documents/Plans/Frame/Bugfix_BindlessArrayStaleAfterPipelineRecreate.md` covers a *different* failure (resident slots not re-patched after `PipelineManager` reconstruct → grey terrain). This plan is about the descriptor-staleness *verifier* tripping `DEBUG_BREAK`, and about slot exhaustion.

---

## Issue 1 (should-fix, latent DEBUG_BREAK): cross-slot snapshot staleness in `VerifyAllDescriptorGenerations`

### Mechanism

Per-island bindless bindings are registered in `IslandTerrain::AcquireTextureSlot` (`Engine/Source/Frame/IslandTerrain.cpp:340`) via:

```cpp
rTextureDescriptors.RegisterTextureBinding(bindingKey, rConsumer.pPipeline, rConsumer.iBinding, rConsumer.samplerFlags, nullptr, ppArray, rConsumer.iCount, iSlot);
```

with `iCount = kiMaxIslands` and `iArrayIndex = iSlot`. For an array binding, `RegisterTextureBinding` snapshots the **entire** width-`kiMaxIslands` array of `Texture*` plus per-element generations (`Engine/Source/Graphics/Managers/TextureDescriptors.cpp:217-229` — the `textures.assign(ppTextures, ppTextures + iCount)` + `uiTextureGenerations.resize(iCount)` loop), even though the binding logically owns only element `iArrayIndex`. The owning index is recorded in `TextureBinding::iArrayIndex` (`Engine/Source/Graphics/Managers/TextureDescriptors.h:62`).

Single-element writes correctly honor `iArrayIndex`: `WriteArrayBindingDescriptors` (`TextureDescriptors.cpp:137-169`) touches only `textures.at(iArrayIndex)` and updates only `uiTextureGenerations.at(iArrayIndex)` (line 167) — leaving the *other* elements of the snapshot frozen at their registration-time pointers/generations.

But the verifier does **not** honor `iArrayIndex`. `PipelineManager::VerifyAllDescriptorGenerations` (`Engine/Source/Graphics/Managers/PipelineManager.cpp:805-819`, called at CB-record from `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp:13`) iterates **all** `iCount` elements of every binding's snapshot:

```cpp
for (int64_t i = 0; i < iCount; ++i)
{
    Texture* pTexture = rBinding.textures.at(i);
    if (pTexture == nullptr || pTexture->muiGeneration == 0) { continue; }
    if (pTexture->muiGeneration != rBinding.uiTextureGenerations.at(i) || pTexture->mVkImage == VK_NULL_HANDLE)
    {
        ... DEBUG_BREAK();
    }
}
```

### Hazard

If island A first-mints a slot while island B is already resident, A's snapshot captures B's live `Texture*` at B's index (`ppArray` is the shared full-width array). When B is later evicted, `EvictionSweep` (`IslandTerrain.cpp:477` / `:491`) calls `Texture::FreeGpuResources` → `Texture::Destroy` (`Engine/Source/Graphics/Objects/Texture.cpp:432-456`), which nulls `mVkImage` (line 443) **without** bumping `muiGeneration` (generation is bumped only on create/adopt, lines 137/218 — `Texture.cpp`'s own comment and `Objects/CLAUDE.md` confirm "destroy does NOT reset" the generation). A's surviving snapshot at B's index now has non-zero generation **and** null `mVkImage` → the `pTexture->mVkImage == VK_NULL_HANDLE` clause trips `DEBUG_BREAK` on the next CB re-record.

CBs are record-once, so this fires only on a re-record event — resize, settings change, or device-loss recovery — **not** per frame. That intermittency is exactly why it is latent rather than already-observed.

The hardening's `UnregisterBindingsForKey` (`IslandTerrain.cpp:500-503`) erases only the **evicted** island's own records (its `rCrc` + its 4 channel CRCs). It does not touch **other** islands' cross-slot snapshots that reference the evicted island's `Texture*`, so it does not address this.

### Proposed fix (recommend A)

- **A. Make `VerifyAllDescriptorGenerations` honor `iArrayIndex`.** For a binding with `iArrayIndex >= 0`, verify only that one element instead of the whole array — mirrors what `WriteArrayBindingDescriptors` already does, which is the only element the binding actually owns and keeps current. Single-call-site change in `PipelineManager.cpp:805-819`; least invasive; aligns verifier semantics with the write semantics. **Recommended.**
- B. Have single-element array bindings store a 1-element snapshot (`textures`/`uiTextureGenerations` of size 1) instead of the full `kiMaxIslands` array. Reduces per-binding memory and makes the verifier's full-walk correct without special-casing, but reshapes `RegisterTextureBinding` and `WriteArrayBindingDescriptors`'s index arithmetic (they currently index by absolute `iArrayIndex`). More churn for the same effect.

Option A is the minimal correctness-preserving fix. Confirm at execution time that the array loop's per-element `continue` (null OR gen==0) and the `iArrayIndex` field have not shifted.

---

## Issue 2 (should-fix, latent capacity off-by-one): island slot exhaustion at `kiMaxIslands`

### Mechanism

`Islands` ctor asserts `miTemplateCount <= shaders::kiMaxIslands` (`Engine/Source/Graphics/Islands.cpp:23`, `kiMaxIslands = 64` at `Engine/Data/Shaders/ShaderLayoutsBase.h:124`). But slot 0 is permanently reserved as the neutral placeholder anchor — `IslandTerrain::miNextTextureSlot` starts at `1` (`Engine/Source/Frame/IslandTerrain.h:157`). So usable slots are 1..63 (63 islands), not 64.

If all 64 templates are simultaneously referenced/minted, the 64th first-mint computes `iSlot = miNextTextureSlot++ = 64` (`IslandTerrain.cpp:290`), which:
- fails `ASSERT(iSlot >= 1 && iSlot < shaders::kiMaxIslands)` (`IslandTerrain.cpp:297`), and
- in a release build, indexes `.at(64)` on the 64-element `mRenderTargetTextures.m*Textures` vectors (sized to exactly `shaders::kiMaxIslands` at `Engine/Source/Graphics/Managers/TextureManager.cpp:261-265`) → `std::out_of_range` (`IslandTerrain.cpp:309-313`).

The free-list reuse path (`IslandTerrain.cpp:288-296`) does not change the ceiling: it only recycles already-minted indices, so the high-water mark still marches toward 64 whenever 64 distinct templates are concurrently resident. The pre-hardening monotonic `miNextTextureSlot++` had the identical ceiling — **this is pre-existing**. Unreachable today (~3 kIsland chunks ship; `miTemplateCount` is the count of `ChunkFlags::kIsland` chunks, `IslandTerrain.cpp:22-54`).

### Proposed fix (recommend tightening the assert)

- **A. Tighten the `Islands` ctor assert to `miTemplateCount < shaders::kiMaxIslands`.** One-character change that makes the boot-time guard match the real usable budget (slot 0 reserved). Fails fast and honestly at boot if a 64-template asset set ever ships, rather than crashing mid-session on the 64th mint. **Recommended** as the simplest correctness-preserving option. Pairs naturally with a one-line comment noting slot 0 is the reserved placeholder so 63 templates is the ceiling.
- B. Size the bindless / shader arrays to `kiMaxIslands + 1` so 64 real templates fit alongside slot 0. Correct but invasive: the `mRenderTargetTextures` vector sizing (`TextureManager.cpp:261-265`), the `RegisterTextureBinding` `iCount`, **and** the shader-side array dimension (`Terrain.frag` + `kiMaxIslands` in `ShaderLayoutsBase.h:124`) must all move in lockstep. Only worth it if 64 simultaneously-resident islands becomes a real target.

Recommend A unless the template budget is expected to grow to 64. If B is ever chosen, the shader-side array dimension must stay consistent with the C++ side (`Terrain.frag` samples the bindless arrays declared from `kiMaxIslands`).

---

## Critical files

- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — `VerifyAllDescriptorGenerations` array loop (`:805-819`); Issue 1 fix site (Option A).
- `Engine/Source/Graphics/Managers/TextureDescriptors.cpp` / `.h` — `RegisterTextureBinding` full-width snapshot (`:217-229`), `WriteArrayBindingDescriptors` single-element write (`:137-169`), `TextureBinding::iArrayIndex` (`.h:62`); Issue 1 Option B site.
- `Engine/Source/Graphics/Objects/Texture.cpp` — `Destroy`/`FreeGpuResources` null image without generation bump (`:432-464`); the root invariant behind Issue 1.
- `Engine/Source/Frame/IslandTerrain.cpp` — per-slot registration (`:340`), first-mint slot assert (`:297`), `EvictionSweep` (`:435-509`); registration/exhaustion sites for both issues.
- `Engine/Source/Graphics/Islands.cpp` — ctor budget assert (`:23`); Issue 2 fix site (Option A).
- `Engine/Source/Graphics/Managers/TextureManager.cpp` — `mRenderTargetTextures` vector sizing (`:261-265`); Issue 2 Option B site.
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — `kiMaxIslands = 64` (`:124`); shared budget constant, shader-side consistency for Issue 2 Option B.

## Acceptance criteria

- Issue 1: a churn sequence (mint A while B resident → evict B → trigger a CB re-record via resize/settings/device-loss) no longer trips `VerifyAllDescriptorGenerations`'s `DEBUG_BREAK`; the verifier still catches a genuinely stale snapshot for the element the binding owns.
- Issue 2: boot-time guard reflects the true usable slot budget (slot 0 reserved); the `iSlot == kiMaxIslands` first-mint path can no longer index out of range.

## Out of scope

- The grey-terrain re-patch-after-pipeline-recreate bug in `Documents/Plans/Frame/Bugfix_BindlessArrayStaleAfterPipelineRecreate.md` — different failure mode, tracked there.
- The eviction / free-list / single-element-write hardening itself — already landed and correct; this plan only addresses the two latent gaps it left untouched.
- Growing the actual island template count, or any change to how many islands ship.
- Reworking the per-slot registration into the co-located form tracked by `Graphics/CoLocatePerSlotDescriptorRegistration.md` (listed under `IslandTerrain.cpp` File Groups in `Order.md`) — that architectural refactor would absorb these fixes but is not a prerequisite.

## Incidental observations (do not over-invest)

- `Documents/Plans/Frame/Bugfix_BindlessArrayStaleAfterPipelineRecreate.md` cites stale line anchors for `RestorationSweep` / `UpdateArrayBindingsForKey` (`IslandTerrain.cpp:441` / `:475`); those are now ~`:536` (the residency loop) / ~`:570` (the `UpdateArrayBindingsForKey` call). Refresh when that plan is next touched.
- `CommandBufferManager::SubmitMainToQueue` (`Engine/Source/Graphics/Managers/CommandBufferManager.cpp:163-164`) unconditionally `vkResetFences` but submits Main with `VK_NULL_HANDLE` (`bSignalFence` is always `false` from its only caller, `Graphics.cpp:231`); the fence is re-signaled by the always-following UI submit. Correct today but a fragile reset/signal split — worth an explicit comment if that code is touched.
