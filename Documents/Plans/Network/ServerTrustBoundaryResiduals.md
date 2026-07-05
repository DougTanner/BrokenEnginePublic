# Server Trust-Boundary Residuals

Two mechanical server trust-boundary parse gaps in the server net path — one wire-dispatch backstop, one file-read validation — left uncovered by the current dispatch/parse hardening.

## Context

Engine `Server::Receive` is wrapped in a log-and-drop try/catch and wire-path `fNavigationDelay` is validated at parse, but two boundaries stay uncovered:

1. **Game-packet dispatch has no backstop.** `ServerSession::ParseReceivedGamePackets` (`ServerSession.cpp:118`) switches on untrusted `uiPacketType` over `engine::gpServer->DrainReceivedGamePackets()` with no try/catch. It runs from `PreTickNetwork` — a *different* call stack than engine `Server::Receive`, so that session's backstop does not protect it. A throw from any handler propagates uncaught through `GameBase::ServerUpdate`. The `kClientSaveRequest`/`kClientLoadRequest`/`kClientResetRequest` handlers (`:199-223`) call `mGameSaveLoad.ServerSave/Load/Reset` (file I/O). The concurrently-landed save/load hardening already made the load path non-throwing/all-or-nothing (`ServerLoad()` returns bool → `ServerReset()` fallback at `:208-215`), so this reduces to a pure defense-in-depth backstop with the same shape as engine `Client::Receive`/`Server::Receive`.

2. **`fNavigationDelay` file-read path is unvalidated.** The wire path now clamps via `ValidateNavigationDelay` (`ServerSession.cpp:113`, `[0.0f,60.0f]`, NaN/Inf→60.0f), but `Fleet::fNavigationDelay` *also* enters via the save/replay file path — `ReadFleet` (`ServerFleetManagerUtils.cpp:89`, `common::Read(rFileStream, rFleet.fNavigationDelay)`). A save written before the wire fix, or a tampered/corrupt file, can load a NaN delay that permanently freezes fleet nav (every `fFrameChangeTimer <= 0` comparison against NaN is false forever). File input is a trust boundary; `ReadFleet` already rejects a negative flagship index (`:74-82`) from the landed save session, so the finite-check belongs at the same site.

## Design

**Residual 1 — dispatch backstop.** Wrap the per-packet dispatch body inside `ParseReceivedGamePackets`'s `for` loop in `try { switch(eType) … } catch (const std::exception& e)`, logging at `kNetwork`/`kWarning` with the packet type and `e.what()` and continuing to the next packet — matching the engine receive backstops verbatim. Drop, never re-throw; a malformed/hostile packet must not tear down `ServerUpdate`.

**Residual 2 — file-read clamp.** Apply the same finite-check+clamp immediately after `common::Read(rFileStream, rFleet.fNavigationDelay)` in `ReadFleet`: `rFleet.fNavigationDelay = std::isfinite(rFleet.fNavigationDelay) ? std::clamp(rFleet.fNavigationDelay, 0.0f, 60.0f) : 60.0f;`. KISS: inline the one-liner (the wire-path `ValidateNavigationDelay` helper is a `static` in a different TU — do not hoist a shared header for one call). Read-side clamp of already-persisted data: no `Frame::kiVersion` bump, no stream-format change, no CRC/wire impact.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerSession.cpp` — `ServerSession::ParseReceivedGamePackets` dispatch try/catch (Residual 1).
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManagerUtils.cpp` — `ReadFleet` `fNavigationDelay` finite-check+clamp (Residual 2). NOTE: `Network/AuditSweepQuickWins.md` renames this file to `ServerFleetSerialization.cpp` — refresh this cite if that lands first.

## Invariant exposure

Server-only (`BT_SERVER`). No wire format, no `kuiProtocolVersion`, no `Frame::kiVersion`, no CRC/determinism exposure: the try/catch backstops request-queueing in `PreTickNetwork` (not the CRC'd sim tick), and the clamp is a read-side correction of already-persisted data.

## Out of scope

- Spawn-rate / free-agent mint bounding — `Network/ServerSpawnRateBounding.md`.
- Wire-path `fNavigationDelay` validation, DoS queue caps, `ClientHello` null-reject/idempotency — already present in the server net path.
- Engine `Server::Receive`/`Client::Receive` backstops — already wrapped.
- Any new save/replay stream field or format/version change.
