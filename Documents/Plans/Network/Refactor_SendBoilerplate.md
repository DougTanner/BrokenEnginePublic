# Refactor: Collapse Send* Boilerplate (Client + ClientSession)

Source: /external-refactor-clean on Engine/Source/Network/Client + Projects/BrokenEngineSandbox/Source/Network/Client

Depends on `Refactor_WorkbufferRaii.md`.

`Client::Send*` methods (engine side) and `ClientSession::Send*` methods (game side) are ~20 near-duplicate functions of ~15-20 lines each. Each does: (a) connected/peer gate, (b) workbuffer push, (c) emit packet-type byte, (d) emit payload, (e) SendPacket, (f) workbuffer pop. A variadic member template absorbs (a)-(c), (e), (f); cleanly compresses 16 methods while leaving the 4 special cases hand-written.

## Changes

### Engine/Source/Network/Client/Client.h
- Add private member template (alongside existing `Send*` methods):
  ```cpp
  template <typename... TArgs>
  void SendSimplePacket(engine::PacketType eType, uint8_t uiChannel, uint32_t uiPacketFlags, TArgs&&... args);
  ```
- Add `bool CanSend() const { return mbConnected && mpServerPeer != nullptr; }` (the gate the helper and existing methods share). [~15m]

### Engine/Source/Network/Client/ClientSend.cpp — compress these 12 methods:
- `SendSpawnRequest` (56-76) → 3-line body. [~5m]
- `SendDebugFrameRequest` (103-123) → needs `SendSimplePacketCoord` overload that appends a `GridCoord` tail, or trailing-GridCoord variadic. [~15m]
- `SendDesyncReport` (78-101) → same pattern as `SendDebugFrameRequest`. [~15m]
- `SendUnsubscribeOnly` (185-202) → 3-line body. [~5m]
- `SendResyncRequest` (204-221) → 3-line body (keep LOG outside helper). [~5m]
- `SendPauseRequest` (223-240) → 3-line body. [~5m]
- `SendTimespeedRequest` (242-259) → 3-line body. [~5m]
- `SendSaveRequest` (261-276) → 1-line body. [~5m]
- `SendLoadRequest` (278-293) → 1-line body. [~5m]
- `SendResetRequest` (295-310) → 1-line body. [~5m]
- `SendReplayRecordRequest` (312-327) → 1-line body. [~5m]
- `SendReplayPlaybackRequest` (329-344) → 1-line body. [~5m]

### Engine/Source/Network/Client/ClientSend.cpp — do NOT compress (keep hand-written):
- `SendAck` (12-54): variable-length loop with timestamp capture. Only swap `Push/Pop` for `ScopedWorkbufferFrame` and gate for `CanSend()`. [~10m]
- `SendSubscribe` (125-177): 40 lines of slot allocation + placeholder logic. Same scoped-RAII-only change. [~10m]
- `SendUnsubscribe` (179-183): 3-line forwarder. Leave. [~0m]
- `SendHello` (346-389): file I/O + variable-length config. Scoped-RAII-only. [~10m]

### Projects/BrokenEngineSandbox/Source/Network/Client/ClientSession.cpp
- 6 game-side `Send*` methods at 383-533 have the identical pattern. Either (a) expose the engine `SendSimplePacket` via a passthrough on `ClientSession` that calls `mpClientNetwork->SendSimplePacket(...)`, or (b) add a sibling `SendSimplePacket` on `ClientSession` that uses the same template shape. Prefer (a) — single source of truth. [~30m]
- Compress the 6 methods to 1-3 line bodies each. [~15m]

## Verification
- After each batch: rebuild client config; confirm wire format unchanged by running a local session and inspecting packet bytes with the existing `LOG(kNetwork, kVerbose, ...)` hooks.
- Net line reduction target: `ClientSend.cpp` 393 → ~190, `ClientSession.cpp` 537 → ~390.

## Verification Notes
Verified — all 12 listed compress-target line ranges and 4 keep-hand-written ranges in `ClientSend.cpp` match actual file content at commit d08678d3. Workbuffer Push/Pop pairs and `CanSend()` gate pattern consistent. Game-side `ClientSession::Send*` methods at 383-533 match the identical pattern.
