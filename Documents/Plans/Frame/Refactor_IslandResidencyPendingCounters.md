# Refactor: Island Residency Pending-Scan Counters (IslandTerrain)

## Context

Spun out of `Graphics/Managers/Refactor_PerFramePerf.md` (the TextureManager pending-adoption counter — landed). That plan's Step 6 sibling sweep flagged `IslandTerrain::AnyEvictionPending` and `AnyRestorationPending` as the same shape — both scan the **entire** `mIslands` map every frame, called on the same churn pre-scan line as the (now-counter-backed) `AnyAdoptionPending`:

```cpp
// Engine/Source/Graphics/Graphics.cpp:212 (RenderGlobal churn pre-scan)
if (gpIslandTerrain->AnyEvictionPending() || gpIslandTerrain->AnyRestorationPending() || gpTextureManager->AnyAdoptionPending())
{
    WaitAllFramebufferFencesIdle();
}
```

- `IslandTerrain::AnyEvictionPending` (`Engine/Source/Frame/IslandTerrain.cpp:628-645`): full `mIslands` scan; per-template predicate `miTextureSlot != 0 && mbGpuResident && miRefCount == 0 && (gpGraphics->muiFrameCounter - muiLastUsedRenderFrame) > kuiGraceRenderFrames`.
- `IslandTerrain::AnyRestorationPending` (`Engine/Source/Frame/IslandTerrain.cpp:647-684`): full `mIslands` scan; per-template predicate `!mbGpuResident && miTextureSlot >= 0 && (all 4 residency-channel chunks — colors/normals/AO/masks — at `>= kReady`)`.

The texture sibling was classified **Identical** and was the easy case (clean state-machine transition sites). These two were classified **Related**, deliberately deferred, because their predicates are *derived*, not event-driven — a naïve "increment/decrement at transition sites" counter cannot mirror them exactly. This plan resolves that.

## Why a plain counter doesn't work here (and what does)

The eviction predicate has **no discrete event** for two of its terms:
- `miRefCount` is recomputed from scratch every frame in `Islands::UpdateActiveIslands` (`IslandTerrain.cpp:107-110/143`), not mutated at hookable sites.
- The grace term `(muiFrameCounter - muiLastUsedRenderFrame) > kuiGraceRenderFrames` crosses its threshold purely because `muiFrameCounter` advanced — **no code runs** at the crossing frame.

So this plan does NOT try to maintain an exact "eviction-eligible count". Instead it maintains a cheaper **candidate** count that captures the common steady-state early-out, leaving only a small filtered scan for the rare non-zero case:

- **Eviction candidate count** = number of templates with `miTextureSlot != 0 && mbGpuResident && miRefCount == 0`. Steady state (every resident island referenced) → 0 → `AnyEvictionPending` returns false in O(1). When > 0, scan **only** the candidate set for the grace term. Maintain the count where `miRefCount` is (re)assigned in `UpdateActiveIslands` and where residency/slot flip in the mint/evict/restore sweeps — fold the count into the existing per-frame `miRefCount` recompute (it already touches every active template), so no new full pass is added.
- **Restoration candidate count** = number of templates with `!mbGpuResident && miTextureSlot >= 0` (evicted-but-slotted, awaiting their chunks). Early-return if 0; when > 0, scan only those for the 4-channel `kReady` check. Maintain at the residency/slot transition sites (eviction sets non-resident+slotted → +1; restoration clears it → −1; re-mint above slot 0 changes slot).

This is a different, more involved edit than the texture counter — hence its own plan and a higher risk score.

## Design

1. Add two `int64_t` candidate counters to `IslandTerrain` (client-only residency state). They need not be atomic: `miRefCount`, residency flags, slots, and both sweeps are all main-thread / render-path (unlike the texture counter, which crossed the upload thread). Confirm at execution that nothing touches these from `Dispatch()` workers — the sim hot path (`FrameElevation`/`FrameNormal`) reads the elevation grid, not residency state.
2. **Eviction candidate count**: maintain at every `miRefCount` (re)assignment (`Islands::UpdateActiveIslands`, `IslandTerrain.cpp:107-110/143`) and at residency/slot flips in `EvictionSweep`/`RestorationSweep`/`AcquireTextureSlot`. `AnyEvictionPending` → `if (count == 0) return false;` then scan only candidates for the grace term.
3. **Restoration candidate count**: maintain at the residency/slot flips. `AnyRestorationPending` → `if (count == 0) return false;` then scan only candidates for the 4-channel `kReady` conjunction.
4. Preserve the existing `gpGraphics == nullptr || gpTextureManager == nullptr` guards (`:630-633`, `:649-652`) — boot/teardown windows.
5. Keep the comments that document each predicate's mirror to `EvictionSweep`/`RestorationSweep`; the sweeps' own logic is unchanged (they still iterate and act — only the cheap *pre-scan* gate changes).

## Critical files
- `Engine/Source/Frame/IslandTerrain.cpp` — `AnyEvictionPending` (`:628`), `AnyRestorationPending` (`:647`), `EvictionSweep`, `RestorationSweep`, `AcquireTextureSlot`, and `Islands::UpdateActiveIslands` refCount recompute (`:107-110/143`)
- `Engine/Source/Frame/IslandTerrain.h` — the two counter members

## Out of scope
- The `EvictionSweep`/`RestorationSweep` bodies themselves (they still do the real work; only the pre-scan gate is optimized).
- The texture `AnyAdoptionPending` counter — already landed in `Refactor_PerFramePerf.md`.
- The debug elevation-high max-reduction at `GlobalUniforms.cpp` (sweep candidate A2 — dropped as not worth it: a max-reduction, not a counter; `mbGpuResident` is the common case, not sparse; debug-only).

## Acceptance criteria
- Steady-state frames (all resident islands referenced, none restoring) no longer iterate `mIslands` in either pre-scan.
- The candidate counts agree with a full scan under churn (island stream-in/out, LRU eviction, device-loss recovery, grace-window expiry).
- Eviction and restoration still fire on exactly the same frames as before (validate via island stream-in/out playtest).

## Notes
- **Coordinate with `Frame/IslandTerrainSplit.md`.** Both `AnyEvictionPending` (`:628`) and `AnyRestorationPending` (`:647`) sit inside the `#if defined(BT_CLIENT)` GPU-residency span (`:410-832`) that the split relocates into a new `IslandTerrainResidency.cpp`. If the split lands first, re-target these functions (and the counter members may move too); if this lands first, the split relocates the new code with it. Either order works — co-schedule or refresh citations. (See the `IslandTerrain.cpp` File Group in `Order.md`.)
- Client/graphics-only; no CRC/determinism/`kiVersion`/network exposure (residency state is excluded from CRC — it is client-only GPU bookkeeping). The risk is a stale candidate counter: stuck-zero would skip a needed eviction (stale GPU slot lingers past grace) or restoration (island stays on the slot-0 placeholder) — visible under churn, hence the playtest gate.
- One pre-staged grill decision: whether the eviction candidate count is worth the maintenance complexity vs. simply gating both pre-scans behind a single coarse `miGpuResidentCount > 0` (almost always true, so a much weaker early-out) — measure the steady-state scan cost first if uncertain.
