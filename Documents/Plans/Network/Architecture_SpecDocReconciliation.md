# Architecture: Network Spec/Doc Reconciliation

## Context

Source: /external-architecture-review on `Engine/Source/Network` (recursive). `Documents/Architecture/Network.md` and the Network CLAUDE.md files were verified against the code; the constants all match, but four spec/doc claims diverge from verified behavior, and two real invariants are documented nowhere. Doc-only.

## Design

### Documents/Architecture/Network.md
- **Resend cap wording**: spec says "capped at `kiMaxResendFrames = 8` per call" (`Network.md:12`); code caps 8 **per slot** per call (`ServerSend.cpp:205` resets `iSlotResendCount` per slot — a 16-slot client can receive up to 128 resends per tick). `Server/CLAUDE.md:25` already states it correctly; align the spec. [~5m]
- **ACK packet contents**: spec lists per-slot `(slot, epoch, floor, bitfield-low, bitfield-high)` (`Network.md:10`) but omits the trailing 8-byte client timestamp echo (`ClientSend.cpp:48-49` / `ServerReceive.cpp:95-99`) that drives pipeline RTT. Add the field. [~5m]

### Engine/Source/Network/CLAUDE.md (+ NetworkManager.h comments)
- **"Channel 1 reserved" is false**: `NetworkManager.h:14,17` comments and `Network/CLAUDE.md:9` say channel 1 is "reserved" unreliable — the ACK stream, the protocol's highest-frequency client→server message, is sent on it (`ClientSend.cpp:51`). Correct both. [~5m]
- **Spec-coverage claim**: `Network/CLAUDE.md:5` says the "full protocol flow" lives in `Network.md`, but the spec contains no handshake (hello/version/GUID), subscription lifecycle/epoch wire format, discovery, or static-data message — those live only in CLAUDE.md prose and code comments. Narrow the claim to what the spec actually covers (clock/ACK/resend/jitter machinery). Extending the spec instead is a larger, optional follow-up. [~10m]
- **Thread contract**: the entire Network area is main-thread-only (verified: all three `enet_host_service` call sites, discovery socket polls, and sends run on the main frame loop; sim state and `SendPacket`'s workbuffer use are unsynchronized). No CLAUDE.md states it. Add an explicit "main thread only — no locks by design" invariant line to `Network/CLAUDE.md`. (A code-side thread-affinity assert is staged in `Refactor_QuickWinMechanics.md`.) [~5m]

### Engine/Source/Network/Client/CLAUDE.md
- **Receive-time vs apply-time invariant**: full-state receipt mutates slot ACK/epoch/state immediately (`ClientReceive.cpp:211-215`) while the frame payload is adopted later by the game `ClientDataReceiver`; drain-per-poll (`Client.cpp:80-87`) means a game layer that skips a drain loses the frame but keeps the activated slot. Document the "activation and adoption must both happen this frame" contract where the next reader will look. [~10m]

## Critical files

- `Documents/Architecture/Network.md`
- `Engine/Source/Network/CLAUDE.md`, `Engine/Source/Network/NetworkManager.h` (comment lines only)
- `Engine/Source/Network/Client/CLAUDE.md`

## Out of scope

- `Documents/Architecture/FrameUpdatePipeline.md` corrections (phantom `ClientSession::PostRender`, missing client `UpdateSubscriptions`/ceiling-clamp steps) — appended to `Common/StaleDocClaimsSweep.md` item 11, which owns that diagram's pass via `/update-architecture-diagrams`.
- The `Network/CLAUDE.md` `SendSimplePacket` "(see children)" dangling pointer — `Common/StaleDocClaimsSweep.md` item 7.
- Extending `Network.md` into a full protocol spec (handshake/subscription/discovery sections) — optional follow-up, not this pass.
- Any code change (the thread-affinity assert lives in `Refactor_QuickWinMechanics.md`).

## Notes

- Documentation/comment-only — Risks 0, no build or runtime impact. If `Architecture_WireFormatPairing.md` lands first, refresh the cited send/receive line numbers.

## Verification Notes

All divergences verified real against source and docs (2026-06-10):
- Resend cap: `Network.md:12` says "capped at `kiMaxResendFrames = 8` per call"; code resets `iSlotResendCount` per slot (`ServerSend.cpp:204-205`) so the cap is per slot per call (up to 8 × 16 active slots per client per tick). `Server/CLAUDE.md` already states "per slot per tick" correctly.
- ACK timestamp echo: `Network.md:10` lists only the per-slot 5-tuple; the trailing 8-byte client timestamp is written at `ClientSend.cpp:48-49` and read at `ServerReceive.cpp:95-99` — confirmed missing from the spec.
- "Channel 1 reserved" false: comment at `NetworkManager.h:14` ("Channel 1: Control Unreliable (reserved)") and `Network/CLAUDE.md:9` ("channel 1 reserved unreliable"); the ACK stream is sent on `kuiChannelUnreliable` at `ClientSend.cpp:51`. Confirmed.
- Spec-coverage claim: `Network/CLAUDE.md:5` does carry a qualifying parenthetical "(reconciliation, ACK/resend, clock correction, epoch guard)", but the word "full protocol flow" still overclaims — `Network.md` contains no handshake, subscription wire format, discovery, or static-data coverage (verified by reading the whole spec). Narrowing remains warranted.
- Thread contract: all three `enet_host_service` sites (`Server.cpp:69`, `Client.cpp:89`, `Client.cpp:50` dtor drain) plus the discovery socket polls run on the main frame loop; sim statics (`NetworkSimulation.h:87,108`) and `SendPacket`'s workbuffer use are unsynchronized; no Network CLAUDE.md states main-thread-only. Confirmed missing.
- Receive-vs-apply: slot ACK/epoch/state mutated at receive time (`ClientReceive.cpp:211-215`) while the frame payload rides the drain buffers cleared at the next `Poll` (`Client.cpp:80-86`); the hub documents drain-per-poll generically but not the "activation and adoption must both happen this frame" pairing. Confirmed missing from `Client/CLAUDE.md`.
