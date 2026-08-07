<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T12:56:23.596Z","dependsOn":[]} -->
# Correlate desync debug-frame responses with the active request

## Context

/external-deep-analysis (2026-08-07) of `Projects/BrokenEngineSandbox/Source/Network/Client` found that `ClientDesyncManager::PollDebugFrameResponse` (`ClientDesyncManager.cpp:40`) consumes any `ReceivedDebugFrame` as if it answered the active desync. The server response carries the requested tick and coordinate (`ServerDebugFrameMessage`), but engine receive retains only the decompressed frame (`Engine/Source/Network/Client/ClientReceive.cpp:381,396`; `Engine/Source/Network/Client/Client.h:63`), so the consumer cannot check identity.

Reachability was externally verified (Phase 3, VEC-EXT-001, VERIFIED): vendored ENet 1.3.18 orders reliable packets per channel only (`ThirdParty/enet/peer.c:811-869`; `ThirdParty/enet/docs/design.dox:70-72`). The debug-frame response is sent reliably on channel 0 (`Engine/Source/Network/Server/ServerReceive.cpp:219`) while full states go reliably on channels ≥ 2 (`Engine/Source/Network/Server/ServerSend.cpp:46,77`), so a response delayed past `kDesyncDebugTimeout` can arrive after recovery completes and be consumed by a later desync's wait, producing a misleading per-field diff against the wrong client snapshot and clearing the diagnostic stall early. This contradicts the matching-response comparison flow in `Documents/Architecture/GameReconciliation.md` ("Desync Recovery and Optional Debug Frames") and `Projects/BrokenEngineSandbox/Source/Network/Client/AGENTS.md:27`.

Exposure is diagnostic-only: `kbDesyncDebugFrames` is a manual, disabled-by-default switch (`Projects/BrokenEngineSandbox/Source/Pch.h`), but when it is on, this defect corrupts exactly the diagnostic being relied upon.

## Design

Preserve the wire tick and coordinate through the engine receive layer into `ReceivedDebugFrame`, and make `PollDebugFrameResponse` consume a frame only when both match the active `mDesyncDebugState` request. A non-matching response is discarded (logged at `kDebug`) and does not satisfy the active wait. No wire-format change: tick and coordinate are already in `ServerDebugFrameMessage`; only client-side retention changes.

## Critical files

- `Engine/Source/Network/Client/ClientReceive.cpp`
- `Engine/Source/Network/Client/Client.h` (`ReceivedDebugFrame` shape)
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDesyncManager.cpp`

## In scope

- `ReceivedDebugFrame` retention of tick/coord and its producer in engine receive
- `PollDebugFrameResponse` identity check and non-matching-response discard

## Out of scope

- Wire format and protocol version (no packet byte changes)
- Desync recovery/disconnect policy and timeout values
- Any deterministic simulation or CRC state

## Risk tier and invariants

Change Workflow Tier 3 — the decided design changes the Engine-owned `ReceivedDebugFrame` payload contract and its game-side consumer, which is independently owned cross-subsystem integration. No wire, determinism, or CRC exposure: the debug-frame path is outside deterministic frame execution, only active under the manual `kbDesyncDebugFrames` switch (which must match between client and server builds), and the packet bytes are unchanged.

## Acceptance criteria

- With `kbDesyncDebugFrames` enabled, a matching response completes the diagnostic exactly as before.
- A synthetic non-matching response (wrong tick or coord) is discarded and the wait continues to the real response or timeout.
- Client and server build clean with the switch both on and off.
