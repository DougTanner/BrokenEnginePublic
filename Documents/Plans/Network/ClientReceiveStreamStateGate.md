<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-01T21:20:40.222Z","dependsOn":[]} -->
# Gate Client Receive Deserialization on Stream State

## Context

Found while verifying the completed `FleetRandomStateLoadValidation` change. Pre-existing and outside that change's boundary: it makes no difference whether the random-engine state is validated, because the gap is that a short read is never noticed at all.

`common::Read<T>` does not throw and does not report failure (`Common/Serialization.h:87-92`, `:94-99`, `:103-108`). It calls `rStream.read(...)` and returns; a stream that runs out of bytes sets `failbit`, leaves the destination object at whatever value it already held, and every subsequent `Read` on that stream is a silent no-op.

The save path already closes this by checking the stream after the whole read completes: `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp:1252-1256` logs `ReadGrid {} aborted: stream failure after read` and returns `false`, so the staged grid is never adopted.

The client network receive path has no equivalent gate at either of its two stream-deserialization sites in `Engine/Source/Network/Client/ClientReceive.cpp`:

- `DecompressAndReadFrame` (`:14-35`) builds `std::istringstream frameStream` at `:31`, calls `game::NetworkSessionContract::ReadFrame(frameStream, *pFrame)` at `:33` — which is `Frame::ServerRead` (`Projects/BrokenEngineSandbox/Source/Network/NetworkSessionContract.h:37-40`) — and returns the frame at `:34` without inspecting `frameStream`. Its two callers are `Client::ServerCoordFullState` (`:178`) and `Client::ServerDebugFrame` (`:389`); both treat any non-null return as a good frame.
- `Client::ServerCoordStaticData` builds `std::istringstream staticStream` at `:284`, calls `received.staticData.Read(staticStream, /*bIncludeNavData=*/true)` at `:288`, and pushes the result into `mReceivedStaticData` at `:291` without inspecting `staticStream`.

Concretely, a decompressed payload shorter than the reader expects leaves default-initialized or partially-read values behind. `FramePostRenderBase::ServerRead` (`Engine/Source/Frame/FrameBase.cpp:146-160`) reads the random state at `:149`, `uiNextUuid` at `:151`, `uiFrameId` at `:153`, then the alignments and every shared collection; whichever of those the truncation reaches keeps its constructed value and the client installs the frame as authoritative server state.

`Client::Receive` already catches `std::exception` and drops the offending packet (`Engine/Source/Network/Client/Client.cpp:290-297`). That covers readers that *throw* — a corrupt count, an `.at()` miss, `CorruptStreamException`. It cannot cover a silent `failbit`, because nothing is thrown. That is exactly the residual gap.

This is a trust-boundary gap on network input, so a short or malformed payload must be rejected, not repaired.

## Design

Add the same post-read stream check the save path already uses, at each of the two client receive stream sites, and drop the packet when it fails. No format, layout, or protocol change.

1. In `DecompressAndReadFrame` (`Engine/Source/Network/Client/ClientReceive.cpp:14-35`), after `ReadFrame` returns, test `frameStream` and return `nullptr` when the stream is not good. Returning `nullptr` reuses the existing failure channel, so both callers already handle it by dropping the packet.
2. Because `nullptr` now means either a decompression failure or a truncated frame, restate the two caller log messages so they name the outcome rather than only LZ4: `Client::ServerCoordFullState` (`:180`) and `Client::ServerDebugFrame` (the matching message after `:389`). Keep their level and their existing coord/tick fields; keep them allocation-free.
3. In `Client::ServerCoordStaticData`, after `received.staticData.Read(staticStream, ...)` at `:288`, test `staticStream` and, when it is not good, log one `kNetwork` `kWarning` naming the coord and slot and `return` before the `mReceivedStaticData.push_back` at `:291`, so no partially read static data enters the buffer.

Decisions already made, do not re-open:

- Drop the packet; do not disconnect and do not attempt partial use. Dropping matches the existing receive-path policy stated at `Client.cpp:292-296` (let the server resend rather than tear down the client), and matches `ReadGrid`'s refusal to adopt.
- Test the stream object itself (its boolean conversion, which is false when `failbit` or `badbit` is set) rather than `eof()`. A reader that consumed exactly all bytes sets `eofbit` on the final successful read in some paths and must still be accepted; only `failbit`/`badbit` mean the data did not arrive.
- Do not add a check inside `common::Read`, and do not make it throw. That template is on the deterministic save, replay, and pack read paths as well; changing it is a different, wider change with its own risk surface.

## Critical files

- `Engine/Source/Network/Client/ClientReceive.cpp` — `DecompressAndReadFrame` (`:14-35`), `Client::ServerCoordFullState` (`:155-180`), `Client::ServerCoordStaticData` (`:262-292`), `Client::ServerDebugFrame` (`:373-392`). The only edited file.
- `Common/Serialization.h` — `Read` overloads at `:87-92`, `:94-99`, `:103-108`; read-only, the reason the failure is silent.
- `Projects/BrokenEngineSandbox/Source/Save/GameSaveLoad.cpp` — `:1252-1256`, the existing post-read stream gate this mirrors; read-only.
- `Engine/Source/Frame/FrameBase.cpp` — `FramePostRenderBase::Read` (`:127-144`) and `ServerRead` (`:146-160`), the scalars and collections a truncated payload leaves untouched; read-only.
- `Engine/Source/Network/Client/Client.cpp` — `:290-297`, the existing throw-only drop; read-only.
- `Projects/BrokenEngineSandbox/Source/Network/NetworkSessionContract.h` — `ReadFrame` at `:37-40`; read-only.

## In scope

- `Engine/Source/Network/Client/ClientReceive.cpp` — `DecompressAndReadFrame` (`:14-35`): a post-read stream-state test after the `ReadFrame` call at `:33`, returning `nullptr` when the stream is not good.
- `Engine/Source/Network/Client/ClientReceive.cpp` — the two `DecompressAndReadFrame` failure log messages in `Client::ServerCoordFullState` (`:180`) and `Client::ServerDebugFrame` (after `:389`): reword so they cover a truncated read as well as a decompression failure.
- `Engine/Source/Network/Client/ClientReceive.cpp` — `Client::ServerCoordStaticData`: a post-read stream-state test after `:288`, plus one `kNetwork` `kWarning` and an early `return` before the `mReceivedStaticData.push_back` at `:291`.

## Out of scope

- Any change to `common::Read`/`common::Write` in `Common/Serialization.h`, including making them throw or report.
- Any change to `Frame`/`FramePostRenderBase` read order, collection read code, `Frame::kiVersion`, `engine::kuiProtocolVersion`, wire layouts, `NetworkMessages`, or compression.
- Server-side receive paths, save/replay/`.pack` read paths, and the existing `GameSaveLoad` gate.
- Disconnect, resync-request, or retry policy on a rejected packet; slot, epoch, ACK, and subscription state machines.
- Adding backward compatibility, new commands, or unit tests.

## Risk tier and invariants

Change Workflow Tier 3 — trigger: trust-boundary handling of untrusted network input on the client receive path (the Tier-2 exclusion list in the root `AGENTS.md` names trust boundaries).

Invariants the implementation must hold:

- Bytes on the wire, packet layout, protocol version, and `Frame::kiVersion` are unchanged; no peer-compatibility impact.
- A well-formed payload behaves exactly as today, so shared CRC and reconciliation are bit-identical for valid input.
- The rejection path allocates nothing beyond what already runs there; the new logs are allocation-free like their neighbours.
- No slot, epoch, ACK, or subscription state is changed by a rejected static-data payload, and no frame from a truncated payload reaches hydration.

## Acceptance criteria

- A full-state payload whose decompressed bytes are shorter than the frame reader consumes is dropped: `DecompressAndReadFrame` returns `nullptr`, one warning is logged naming the coord and tick, and no `game::Frame` built from that payload is buffered for hydration.
- A truncated static-data payload is dropped: one `kNetwork` `kWarning` is logged and `mReceivedStaticData` gains no entry.
- A truncated debug-frame payload is dropped through the same path with its existing log fields intact.
- Normal client/server play over a live connection is unaffected: subscriptions complete, full state hydrates, and the client and server report matching CRCs — no new warnings appear for valid traffic.
- Debug client and server builds compile.

## Notes

- Line citations are tip-of-2026-08-01; refresh them if the client receive path moves first.
- The static-data site was confirmed independently while verifying the frame site; both share one root cause, one file, one invariant, and one verification pass, so they are deliberately recorded as a single change rather than split.
