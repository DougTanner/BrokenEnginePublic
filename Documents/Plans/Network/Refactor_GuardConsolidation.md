# Refactor: Consolidate Connection & Handshake Gates

Source: /external-refactor-clean on Engine/Source/Network (recursive)

Two identical textual gates are duplicated in high-multiplicity:
1. `if (!mbConnected || mpServerPeer == nullptr) { return; }` — 14 sites in `Client::Send*`.
2. `if (pClient == nullptr || !pClient->bHandshakeComplete) { return; }` — 12 sites in `Server::Receive*`.

Converting each to a one-line helper makes the invariant searchable and eliminates copy-paste drift (the sandbox game-side `ClientSession::CanSend()` already exists; this plan creates its engine-side twin).

## Changes

### Engine/Source/Network/Client/Client.h
- Add `bool CanSend() const { return mbConnected && mpServerPeer != nullptr; }` (private or public; private preferred). [~5m]

### Engine/Source/Network/Client/ClientSend.cpp
- Replace the 14 gate sites (lines 14, 58, 80, 105, 127, 187, 206, 225, 244, 263, 280, 297, 314, 331) with `if (!CanSend()) { return; }`. Most sites disappear after `Refactor_SendBoilerplate.md` — do this before or after that migration; either order works. [~15m]

### Engine/Source/Network/Server/Server.h
- Add private helper `ClientConnection* FindHandshakenClient(int64_t iClientId);` returning the client only when `bHandshakeComplete` is true, else nullptr. Implementation calls existing `FindClient` + checks the flag. [~15m]

### Engine/Source/Network/Server/ServerReceive.cpp
- Replace the 12 gate sites (lines 18, 111, 317, 404, 424, 445, 474, 491, 508, 525, 542 in handlers, plus `Server.cpp:232`) with `ClientConnection* pClient = FindHandshakenClient(iClientId); if (pClient == nullptr) { return; }`. [~30m]
- Investigate: `ClientDesyncReport` (line ~129) and `ClientDebugFrameRequest` (line ~149) do NOT currently check `bHandshakeComplete`. Confirm with the protocol owner whether pre-handshake desync/debug-frame packets are intentional; if yes, add a comment documenting the deliberate omission. [~10m]

### Engine/Source/Network/Server/ServerReceive.cpp — delete unreachable checks
- Lines 468, 485, 502, 519, 536: `if (iSize < 1) return;` inside SaveRequest, LoadRequest, ReplayRecord, ReplayPlayback, ResetRequest. Unreachable — `Server::Receive` already gates on `iSize < 1` at line 171. Remove all 5. [~5m]

## Verification
- Rebuild both client and server.
- Run a local session: connect, send each packet type. Confirm identical behavior.

## Verification Notes
Verified — all gate line refs accurate at commit d08678d3:
- `ClientSend.cpp` 14 sites: 14, 58, 80, 105, 127, 187, 206, 225, 244, 263, 280, 297, 314, 331 (all confirmed).
- `ServerReceive.cpp` 11 sites: 18, 111, 317, 404, 424, 445, 474, 491, 508, 525, 542 (plan said 12 but the 12th is `Server.cpp:232` inside the default/game-packet branch — also confirmed).
- Dead-code `iSize < 1` claim is bulletproof: `Server.cpp:171` (entry `Server::Receive`) already returns if `iSize < 1` before the dispatch switch; the 5 inner gates at `ServerReceive.cpp:468, 485, 502, 519, 536` are strictly dominated and unreachable. Safe to delete.
