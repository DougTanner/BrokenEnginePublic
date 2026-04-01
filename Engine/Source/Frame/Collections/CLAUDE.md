# Collections - SOA Collection Framework and Engine-Level Collections

## Overview

Template-based Structure-of-Arrays (SOA) collection system providing memory management, serialization, GPU rendering, and deterministic CRC validation. Engine-level collections cover lights, effects, audio, physics, and wind simulation, split between always-compiled (server-relevant) and client-only (visual/audio) via `#ifdef BT_CLIENT`. Also defines `global_player_t`, the stable cross-transfer, cross-session identity type for players, assigned at first spawn and carried in `TransferData`.

## Key Systems

- **Collection\<T\>** (`Collection.h`) - CRTP base class managing count, capacity, and contiguous SOA buffers. Optional `CollectionFlags::kIdToIndex` enables stable external references via strongly-typed `id_t<T>` wrappers. Also provides `TypeRegistry` (static type config sharing), `ControllerTypeRegistry` (keyframe animation), deterministic serialization with CRC validation, and `LogDifferences()` for field-level desync diagnosis via `common::LogDifference`. `TypeRegistry::RegisterType()` automatically calls `RegisterLightingTextureCrc()` (client-only) for any type with a non-zero `crc` field, triggering pre-blur registration at startup
- **CollectionMemory** (`CollectionMemory.h`) - SOA memory management templates used by all collections: buffer sizing, 64-byte-aligned allocation/reallocation, capacity growth with data copy, and element add/remove/swap helpers for both basic and indexable (ID-mapped) paired Interpolate/PostRender collections
- **Server Collections** (always compiled) - [Explosions](Explosions/CLAUDE.md) (composite effects), [Pushers](Pushers/CLAUDE.md) (physics force fields with zone-based acceleration)
- **Client Collections** (`#ifdef BT_CLIENT`) - [AreaLights](AreaLights/CLAUDE.md), [Billboards](Billboards/CLAUDE.md), [PointLights](PointLights/CLAUDE.md), [Puffs](Puffs/CLAUDE.md), [SmokeTrails](SmokeTrails/CLAUDE.md), [HexShields](HexShields/CLAUDE.md), [WindTrails](WindTrails/CLAUDE.md), [WindRadials](WindRadials/CLAUDE.md), [Sounds](Sounds/CLAUDE.md)

## Architecture Notes

- **Sync pattern**: Collections with external ownership use `SyncData` structs and `Sync()` methods. Owners MUST call `Sync()` every frame for each owned element until removal
- **Controller pattern**: Fire-and-forget elements spawned via `AddControlled()` with keyframe animation that auto-destroys on expiry (used by PointLights, Puffs, WindRadials). The shared `InterpolateKeyframes<TControllerType>()` template in `Collection.h` works with any controller type that has `uiKeyframeCount`, `pfTimes[]`, `keyframes[]`, and a static `Lerp()` on the keyframe type. `DestroyExpiredControlled()` (also in `Collection.h`) iterates a collection and invokes a caller-supplied lambda for each expired controlled element, handling the `kuiInvalidControllerType` guard and `bDestroysSelf` check
- **Render-only state pattern**: WindTrails uses file-scope statics keyed by UUID for previous-position tracking, keeping render state out of dual-buffered frame data. `EraseStaleRenderState<TMapType, TAccessor>()` (in `Collection.h`) prunes entries whose IDs are no longer present in any active frame, called from `BeginRender()` with a lambda that projects the collection's `idToIndexMap`
- **GPU pipeline**: Each renderable collection owns its pipeline and buffers. The three-phase render pipeline (BeginRender/Render/EndRender) uses `AccumulateRenderCapacity()` to pre-size GPU buffers across all active grid coordinates, then fills them per-frame in `Render()`
- **Dual CRC system**: `SharedCollectionCrc()` uses `SharedMembers()` (when available) to exclude client-only and server-only fields from cross-build validation. `SharedCollectionRead()` deserializes server-format streams into zero-initialized full buffers
- **Extern template**: Every collection header declares `extern template struct Collection<T>` to reduce compile times
- **File splitting**: Large collections split across multiple `.cpp` files (core, update, render) sharing a single `.h`
- **Adding members**: Use the `add-collection-member` skill for the checklist when adding new SOA member pointers

## See Also

- [Frame/CLAUDE.md](../CLAUDE.md) - Frame update pipeline and dual-buffer architecture
- [Game collections](../../../../Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md) - Game-specific collection implementations
- [FrameUpdatePipeline](../../../../Documents/Architecture/FrameUpdatePipeline.md) - Main loop and RunFrameTick phase ordering — update this diagram if collection phases change
