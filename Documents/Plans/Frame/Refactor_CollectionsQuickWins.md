# Refactor: Collections Quick Wins

## Context
Source: /external-refactor-clean on Engine/Source (recursive). Small mechanical items across the eleven collection subdirectories. The standout is a cross-sibling drift where half the render loops dropped the GPU-buffer capacity ASSERT the other half carry. Hot-path allocation and DirectXMath sweeps came back fully clean.

## Design

### Missing GPU-buffer capacity ASSERT (cross-sibling drift)
- The three-phase render convention has each `Render()` assert `siRendered + rCurrent.iCount <= iBufferCapacity` after `GetDynamicStorageBuffer` — present in PointLights/AreaLights/SmokeTrails/WindTrails, absent (capacity captured into an unused structured-binding local) in `HexShieldsRender.cpp:53`, `BillboardsRender.cpp:50`, `PuffsRender.cpp:48`, `WindRadialsRender.cpp:53`. Add the sibling-majority ASSERT to the four missing sites — guards a real overrun class if `AccumulateRenderCapacity` sizing ever regresses [~5m]

### Dead locals / conversions (render-path)
- Delete the dead `int64_t iFramebuffer = iCommandBuffer;` alias in `PointLightsRender.cpp:35`, `AreaLightsRender.cpp:36`, `HexShieldsRender.cpp:37` (five siblings pass `iCommandBuffer` directly) [~5m]
- Drop the int→float→uint texture-index round trip: keep the integer and cast once at the write (`PointLightsRender.cpp:97/149`, `AreaLightsRender.cpp:83/127`; contrast `BillboardsRender.cpp:100` where the layout field really is float) [~5m]
- Fix the dead initializer + obfuscated `f4Position.w /= f4Position.w;` (`BillboardsRender.cpp:67-72`) — write `= 1.0f` [~5m]
- Flatten the one-member `WindTrailsRenderState` struct + local alias (`WindTrailsRender.cpp:11-16, :74`) to a bare file-scope map, matching the hub's render-only-state pattern [~5m]

### Guard-shape and typing consistency
- Unify the `#if defined(BT_CLIENT)` shape within `ExplosionsPostRender::Spawn` — light blocks wrap the whole `if` (`ExplosionsSpawn.cpp:69-74, :99-104`), puff/wind-radial blocks wrap only the body (:77-82, :107-112, :116-122); pick whole-`if`-inside-guard (narrower binary). Conditions are side-effect-free type-field reads — no Random-draw exposure [~5m]
- Type Billboards flags end-to-end: `SyncData::uiFlags` is raw `uint8_t` (`Billboards.h:45`) reconstructed via member-poke in `BillboardsRender.cpp:56-57`; siblings type theirs (`Pushers.h:55`, `Explosions.h:140`). Make it `BillboardFlags_t` + typed SOA member — client-only collection, follow the add-collection-member checklist (no `SharedMembers()`/wire exposure) [~30m]

### Trivia
- Reword the stale `// Heap: idToIndexMap[]` comment (`SoundsUpdate.cpp:41` — code uses `insert_or_assign`) [~5m]
- Drop the no-op `static_cast<int64_t>` (`PushersUpdate.cpp:128`) [~5m]

## Critical files
- `Engine/Source/Frame/Collections/{PointLights,AreaLights,HexShields,Billboards,Puffs,WindRadials,WindTrails,SmokeTrails,Sounds,Pushers,Explosions}/` render/update TUs
- `Engine/Source/Frame/Collections/Billboards/Billboards.h`

## Out of scope
- `ExplosionsPostRender::Spawn` decomposition (211 lines) — sim-phase RNG-order contract makes extraction HIGH-risk for marginal benefit; **accept as-is** (recorded so future sweeps don't re-file)
- The `std::pow` at `ExplosionsSpawn.cpp:92` — verified NOT a determinism bug (feeds client-only visual spawns, no Random consumption); do not "fix"
- Framework-level copy/zero-init/hook changes (`Architecture_CollectionCopyContract.md`, `Architecture_PhaseHookOptIn.md`, `Architecture_CollectionHelperDedup.md`)

## Notes
- Invariant exposure: LOW — all items are render-path or client-only except the Explosions guard-shape unification, which compiles identically on the server (conditions side-effect-free, no Random calls). The Billboards flags change alters a client-only collection's member layout — no CRC/wire, but run the add-collection-member checklist
- Grill decision: none — mechanical throughout
