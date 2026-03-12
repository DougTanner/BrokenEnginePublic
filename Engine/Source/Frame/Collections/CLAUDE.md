# Collections - SOA Collection Framework and Engine-Level Collections

## Overview

Template-based Structure-of-Arrays (SOA) collection system providing memory management, serialization, GPU rendering, and deterministic CRC validation. Engine-level collections cover lights, effects, audio, physics, and wind simulation, split between always-compiled (server-relevant) and client-only (visual/audio) via `#ifdef BT_CLIENT`.

## Key Systems

- **Collection\<T\>** (`Collection.h`) - CRTP base class managing count, capacity, and contiguous SOA buffers. Optional `CollectionFlags::kIdToIndex` enables stable external references via strongly-typed `id_t<T>` wrappers. Also provides `TypeRegistry` (static type config sharing), `ControllerTypeRegistry` (keyframe animation), deterministic serialization with CRC validation, and `ServerCompare()` for field-level desync diagnosis
- **Server Collections** (always compiled) - [Explosions](Explosions/CLAUDE.md) (composite effects), [Pushers](Pushers/CLAUDE.md) (physics force fields with zone-based acceleration)
- **Client Collections** (`#ifdef BT_CLIENT`) - [AreaLights](AreaLights/CLAUDE.md), [Billboards](Billboards/CLAUDE.md), [PointLights](PointLights/CLAUDE.md), [Puffs](Puffs/CLAUDE.md), [SmokeTrails](SmokeTrails/CLAUDE.md), [HexShields](HexShields/CLAUDE.md), [WindTrails](WindTrails/CLAUDE.md), [WindRadials](WindRadials/CLAUDE.md), [Sounds](Sounds/CLAUDE.md)

## Architecture Notes

- **Sync pattern**: Collections with external ownership use `SyncData` structs and `Sync()` methods. Owners MUST call `Sync()` every frame for each owned element until removal
- **Controller pattern**: Fire-and-forget elements spawned via `AddControlled()` with keyframe animation that auto-destroys on expiry (used by PointLights, Puffs, WindRadials)
- **Render-only state pattern**: SmokeTrails and WindTrails use file-scope statics keyed by UUID for previous-position tracking, keeping render state out of dual-buffered frame data
- **GPU pipeline**: Each renderable collection owns its pipeline and buffers. The three-phase render pipeline (BeginRender/Render/EndRender) accumulates capacity across active frames
- **Dual CRC system**: `ServerCollectionCrc()` uses `SharedMembers()` (when available) to exclude client-only fields from cross-build validation. `ServerCollectionRead()` deserializes server-format streams into zero-initialized full buffers
- **Extern template**: Every collection header declares `extern template struct Collection<T>` to reduce compile times
- **File splitting**: Large collections split across multiple `.cpp` files (core, update, render) sharing a single `.h`
- **Adding members**: Use the `add-collection-member` skill for the checklist when adding new SOA member pointers

## See Also

- [Frame/CLAUDE.md](../CLAUDE.md) - Frame update pipeline and dual-buffer architecture
- [Game collections](../../../../Projects/BrokenEngineSandbox/Source/Frame/Collections/CLAUDE.md) - Game-specific collection implementations
- [FrameUpdatePipeline](../../../../Documents/Architecture/FrameUpdatePipeline.md) - Architecture diagrams
