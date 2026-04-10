# `/Network/` - Game Networking Sessions

## Overview

Game-level networking sessions that inherit from engine base classes and encapsulate all multiplayer orchestration. `ClientSession` and `ServerSession` are conditionally compiled (`BT_CLIENT` / `BT_SERVER`) and owned by `Game` via `unique_ptr`. Shared packet definitions and serialization live at the root.

## Key Classes

- **GamePacketType** (`GamePacketType.h`) - Game-layer enum extending `engine::PacketType` at `kGamePacketStart`. Defines all game-specific packet identifiers (assign player, player state, player settings, fleet operations, fleet sync, and fleet navigation delay)
- **PlayerEvents** (`PlayerEvents.h/.cpp`) - Parses raw game packets into typed `PlayerEventType` events. Defines `PlayerStateWireType` for wire encoding and `ReceivedPlayerEvent` (carrying `global_id_t`). `ParseFleetSync()` decodes fleet sync packets into a `vector<Fleet>`
- **NetworkSerialization** (`NetworkSerialization.cpp`) - Game-layer implementation of `engine::NetworkSerialization.h`. Binary serialization of `StatusChange` batches with type-grouped encoding and LZ4 compress/decompress variants

## Subdirectories

- [Client/CLAUDE.md](Client/CLAUDE.md) - `ClientSession` and reconciliation pipeline (`ClientDataReceiver`, `ClientDesyncManager`, `ClientReconciler`, `ReconcileReplay`)
- [Server/CLAUDE.md](Server/CLAUDE.md) - `ServerSession` and its four manager classes (`ServerFleetManager`, `ServerTransferManager`, `ServerBroadcaster`, `ServerClientManager`)

## See Also

- Engine network subsystem: [Engine/Source/Network/CLAUDE.md](../../../../Engine/Source/Network/CLAUDE.md)
- [Game Reconciliation](../../../../Documents/Architecture/GameReconciliation.md)
- [Network Architecture](../../../../Documents/Architecture/Network.md)
