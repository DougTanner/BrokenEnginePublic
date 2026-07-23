<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# StatusChange Wire-Layout Changes Are Not Gated by `kuiProtocolVersion`

## Context

`engine::kuiProtocolVersion` (`Engine/Source/Network/NetworkProtocol.h:64`, currently `7`) is by convention bumped for engine packet-type changes, but the `StatusChange` batch payload layouts — the `TransferData` arms in `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h`, encoded by `Network/NetworkSerialization.cpp` — change without touching it. The 2026-07-21 transfer-sentinel work widened `kTransferSpaceship` 64 -> 68 B and `kTransferMissile` 76 -> 80 B with no `kuiProtocolVersion` change; commit `bf095534` changed the missile wire size the same way. This is an established repo pattern, not a fresh regression — which is why it wants a deliberate decision rather than a silent fix.

**What is actually broken, stated precisely.** The `ClientHello` handshake performs three checks in sequence (`Engine/Source/Network/Server/ServerReceive.cpp`): protocol version (`:278-282`), then `game::Frame::kiVersion` (`:290-301`), then the pack integrity token (`:303-315`). `Frame::kiVersion` is the sum of every collection's `kiVersion` (`Frame.cpp:36`), so a StatusChange layout change that *also* shifts CRC'd collection state is caught incidentally by the frame-version gate — a straddling client is rejected, not left to misparse. The 2026-07-21 change is in that category (it bumped `MissilesPostRender`, `PlayersPostRender`, and `SpaceshipsInterpolate`), so no live misparse exists today.

The reachable gap is a **StatusChange wire-layout change that shifts no CRC'd state and therefore bumps no `kiVersion`**. Nothing then rejects the straddling client, and both `kuiProtocolVersion` and `Frame::kiVersion` report compatible while the transfer group is decoded against the wrong layout. `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md` Option B — dropping the 4-byte `fArrivalGracePeriod` from two transfer arms — is a concrete instance already queued. The gate is therefore load-bearing by accident, and the accident is documented nowhere.

## Design

**Decision plan (present options).** Establish one explicit rule and make it enforceable:

- **Option A — policy only.** Document in the network hub AGENTS.md and `Documents/Architecture/Network.md` that any `StatusChange`/`TransferData` wire-layout change bumps `kuiProtocolVersion`, and note that `Frame::kiVersion` is a *separate* gate covering sim state rather than wire layout. Zero code change; relies on future authors reading it.
- **Option B — derive a layout signature.** Compute a compile-time hash over the `TransferData` arm layouts (sizes and field order, the same information `NetworkSerialization.cpp` encodes) and fold it into the handshake — either into `kuiProtocolVersion` or as a fourth handshake field alongside frame version and pack token. A layout edit then rejects straddling clients automatically, with no author discipline required.
- **Option C — accept and document the coupling.** Declare `Frame::kiVersion` the intended gate for everything sim-side including transfer payloads, and add the missing rule that any `TransferData` layout edit must bump a collection `kiVersion` even when no CRC'd state changed.

Option B is the only one that cannot be forgotten; Options A and C are cheap. The choice is a wire-protocol architecture decision and belongs to the user.

## Critical files

- `Engine/Source/Network/NetworkProtocol.h` — `kuiProtocolVersion` (`:64`).
- `Engine/Source/Network/Server/ServerReceive.cpp` — `ClientHello` gates: protocol (`:278-282`), frame version (`:290-301`), pack token (`:303-315`).
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — `TransferData` arms whose layout is ungated.
- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — the encoder/decoder pair those layouts drive.
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:33-36` — `Frame::kiVersion` composition and its existing bump rule comment.
- `Documents/Architecture/Network.md` — protocol reference to update with whichever rule is chosen.

## Out of scope

- Restructuring the `StatusChange` codec itself, or converting messages to paired write/read definitions — `Network/Architecture_WireFormatPairing.md` and `Network/WireFormatPairingGameSide.md`.
- Retroactively bumping `kuiProtocolVersion` for already-landed layout changes; the gap is forward-looking.
- Save/replay versioning, which `Frame::kiVersion` already covers correctly.
- Backward compatibility for straddling builds — rejection is the wanted outcome, not negotiation.

## Acceptance criteria

- One rule is chosen, recorded, and reachable from the network hub AGENTS.md.
- A deliberate one-field `TransferData` layout edit with no CRC'd state change is demonstrated to be rejected at handshake (Option B), or the rule requiring the author to bump the gate is in the documentation the author would read (Options A/C).
- Existing client/server interop is unaffected for matched builds.

## Coordination

- `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md`: its Option B is the concrete reachable instance of this gap. If that option is chosen, the two must co-land or this plan must land first.
- `Documents/Plans/Network/Architecture_WireFormatPairing.md`, `Documents/Plans/Network/WireFormatPairingGameSide.md`, `Documents/Plans/Network/FleetRequestsByGuid.md`: incompatible wire changes may share one new `kuiProtocolVersion` only when atomically co-landed; otherwise each incompatible release bumps beyond the current version 7. Whatever rule this plan establishes governs those bumps.

## Notes

- **Invariant exposure: wire protocol.** Option B changes the handshake payload and therefore requires its own `kuiProtocolVersion` bump; Options A and C change no bytes. No CRC/determinism/`.pack` exposure — this is transport and version gating, not sim state.
- Client and server land together regardless of option.
- Live verification: two builds straddling a deliberate layout edit, confirming the connection is rejected with a clear reason rather than accepted.
