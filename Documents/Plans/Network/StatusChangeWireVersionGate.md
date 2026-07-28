<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# StatusChange Wire-Layout Changes Are Not Gated by `kuiProtocolVersion`

## Context

`engine::kuiProtocolVersion` (`Engine/Source/Network/NetworkProtocol.h:71`, currently `7`) is by convention bumped for engine packet-type changes, but the `StatusChange` batch payload layouts — the `TransferData` arms declared in `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` and encoded by `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` (`StatusChangeItemWireSize` plus the per-type `Serialize*Transfer`/`Deserialize*Transfer` pairs) — change without touching it. The 2026-07-21 transfer-sentinel work widened `kTransferSpaceship` 64 -> 68 B and `kTransferMissile` 76 -> 80 B with no `kuiProtocolVersion` change; commit `bf095534` changed the missile wire size the same way. This is an established repo pattern, not a fresh regression — which is why it wants a deliberate decision rather than a silent fix.

**What is actually broken, stated precisely.** The `Server::ClientHello` handshake (`Engine/Source/Network/Server/ServerReceive.cpp:249`) performs three rejection checks in sequence: protocol version (`:257-268`), then frame version via `game::NetworkSessionContract::GetFrameVersion()` (`:270-281`), then the pack integrity token (`:283-295`). `game::Frame::kiVersion` is the sum of a base constant, `engine::kiNavDataVersion`, and every collection's `kiVersion` (`Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp:36`), so a StatusChange layout change that *also* shifts CRC'd collection state is caught incidentally by the frame-version gate — a straddling client is rejected, not left to misparse. The 2026-07-21 change is in that category (it bumped `MissilesPostRender`, `PlayersPostRender`, and `SpaceshipsInterpolate`), so no live misparse exists today.

The reachable gap is a **StatusChange wire-layout change that shifts no CRC'd state and therefore bumps no `kiVersion`**. Nothing then rejects the straddling client, and both `kuiProtocolVersion` and `Frame::kiVersion` report compatible while the transfer group is decoded against the wrong layout. `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md` Option B — dropping the 4-byte `fArrivalGracePeriod` from two transfer arms — is a concrete instance already queued. The gate is therefore load-bearing by accident, and the accident is documented nowhere.

## Design

**Decision plan (present options).** Establish one explicit rule and make it enforceable:

- **Option A — policy only.** Document in `Engine/Source/Network/AGENTS.md` (the network hub) and `Documents/Architecture/Network.md` that any `StatusChange`/`TransferData` wire-layout change bumps `kuiProtocolVersion`, and note that `Frame::kiVersion` is a *separate* gate covering sim state rather than wire layout. Zero code change; relies on future authors reading it.
- **Option B — derive a layout signature.** Compute a compile-time hash over the `TransferData` arm layouts (sizes and field order, the same information `NetworkSerialization.cpp` encodes) and fold it into the handshake — either into `kuiProtocolVersion` or as a fourth handshake field alongside frame version and pack token. A layout edit then rejects straddling clients automatically, with no author discipline required.
- **Option C — accept and document the coupling.** Declare `Frame::kiVersion` the intended gate for everything sim-side including transfer payloads, and add the missing rule that any `TransferData` layout edit must bump a collection `kiVersion` even when no CRC'd state changed.

Option B is the only one that cannot be forgotten; Options A and C are cheap. The choice is a wire-protocol architecture decision and belongs to the user. **Do not implement until the user has selected one option**; the selected option's scope entry below then becomes the entire implementation surface.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change for the selected option and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants permission to touch only the named regions plus the mechanical necessities (includes, declarations) the named change requires.

**In scope — regions by option (only the user-selected option applies):**

- Option A (documentation only):
	- `Engine/Source/Network/AGENTS.md` — add the wire-layout bump rule and the `Frame::kiVersion`-vs-wire-layout distinction to the existing protocol/ownership guidance.
	- `Documents/Architecture/Network.md` — record the same rule in the protocol reference.
- Option B (layout signature):
	- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` / its header — derive the compile-time layout signature from the same size/order facts `StatusChangeItemWireSize` (`:149-170`) and the `Serialize*Transfer`/`Deserialize*Transfer` pairs (`:13-142`) encode; no change to the encode/decode logic itself.
	- `Engine/Source/Network/NetworkProtocol.h` — `kuiProtocolVersion` (`:71`): bump for the handshake payload change, and fold the signature here if that sub-choice is taken.
	- `Engine/Source/Network/Server/ServerReceive.cpp` — `Server::ClientHello` gate sequence (`:257-295`): compare the signature, mirroring the existing three reject-with-reason blocks.
	- The `ClientHello` message layout and its client-side send site — only the mechanical field addition/comparison the fourth-field sub-choice requires; no other message changes.
	- `Engine/Source/Network/AGENTS.md` and `Documents/Architecture/Network.md` — document the new gate.
- Option C (documentation plus bump rule):
	- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — extend the existing bump-rule comment (`:33-35`) above `Frame::kiVersion` (`:36`) to cover `TransferData` layout edits.
	- `Engine/Source/Network/AGENTS.md` and `Documents/Architecture/Network.md` — record the rule that `Frame::kiVersion` intentionally gates transfer payload layout.

**Out of scope (all options):**

- Restructuring the `StatusChange` codec itself, or converting messages to paired write/read definitions — completed separately by the wire-format pairing work.
- Retroactively bumping `kuiProtocolVersion` for already-landed layout changes; the gap is forward-looking.
- Save/replay versioning, which `Frame::kiVersion` already covers correctly.
- Backward compatibility for straddling builds — rejection is the wanted outcome, not negotiation.
- Any edit to `StatusChange.h` payload structs, `Serialize*`/`Deserialize*` bodies, or `StatusChangeItemWireSize` values — this plan gates layout changes, it does not make one.

## Critical files

- `Engine/Source/Network/NetworkProtocol.h` — `kuiProtocolVersion` (`:71`).
- `Engine/Source/Network/Server/ServerReceive.cpp` — `Server::ClientHello` gates: protocol (`:257-268`), frame version (`:270-281`), pack token (`:283-295`).
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — `TransferData` (`:87-182`), whose per-type wire arms are ungated.
- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — `StatusChangeItemWireSize` (`:149-170`) and the `Serialize*Transfer`/`Deserialize*Transfer` pairs (`:13-142`) those layouts drive.
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — `Frame::kiVersion` composition (`:36`) and its existing bump-rule comment (`:33-35`).
- `Engine/Source/Network/AGENTS.md` — network hub documentation to carry whichever rule is chosen.
- `Documents/Architecture/Network.md` — protocol reference to update with whichever rule is chosen.

## Risk tier

Tier 3 — wire/protocol surface. Option B changes handshake bytes directly; Options A and C constrain future wire-affecting edits. Invariants: matched-build client/server interop must be unaffected; `Frame::kiVersion` semantics unchanged except the Option C comment/rule addition.

## Acceptance criteria

- One rule is chosen, recorded, and reachable from the network hub `Engine/Source/Network/AGENTS.md`.
- A deliberate one-field `TransferData` layout edit with no CRC'd state change is demonstrated to be rejected at handshake (Option B), or the rule requiring the author to bump the gate is in the documentation the author would read (Options A/C).
- Existing client/server interop is unaffected for matched builds.

## Coordination

- `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md`: its Option B is the concrete reachable instance of this gap. If that option is chosen, the two must co-land or this plan must land first.
- The completed wire-format pairing and FleetGuid request re-key changes: incompatible wire changes may share one new `kuiProtocolVersion` only when atomically co-landed; otherwise each incompatible release bumps beyond the current version 7. Whatever rule this plan establishes governs those bumps.

## Notes

- **Invariant exposure: wire protocol.** Option B changes the handshake payload and therefore requires its own `kuiProtocolVersion` bump; Options A and C change no bytes. No CRC/determinism/`.pack` exposure — this is transport and version gating, not sim state.
- Client and server land together regardless of option.
- Live verification (Option B): two builds straddling a deliberate layout edit, confirming the connection is rejected with a clear reason rather than accepted (`/agent-harness`).
