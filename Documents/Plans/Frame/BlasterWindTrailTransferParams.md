# Blaster Wind-Trail Transfer Params Never Populated Server-Side

## Context

Confirmed by the 2026-07-03 Frame review sweep: blaster wind trails are permanently lost at every server-authoritative cell handoff — the exact case their transfer wire fields exist for. The `TransferRequest` build (`Blasters.cpp:244-248`) fills `.fWindTrailIntensity/Width/LengthMultiplier` only under `#if defined(BT_CLIENT)`, but during replay the client consumes the *server's* transfer StatusChanges (`ReconcileReplayTick.cpp:277` — `frameInput.statusChanges = updateIt->second.statusChanges`; the client's own harvest is skipped), and the server serializes the fields (`NetworkSerialization.cpp:19-21`) from its never-filled defaults (0, 0, 1). `SpawnTransfer.cpp:34-36` then spawns the arriving blaster with intensity 0, and `ClientInit` (`Blasters.cpp:88`) creates a wind trail only when `> 0` — so the trail silently dies at the boundary. The Blasters AGENTS.md "visuals survive a cell handoff" claim is false on this path.

Root cause is structural, not a missing `#ifdef` removal: on the server the values are dropped at `Spawn` — they live only in client-only SOA fields (`pfWindTrailIntensities` etc.), so the server has nothing to put on the wire. Client-only visuals, outside the CRC — cosmetic fidelity bug, not a desync.

## Design

Two viable shapes (grill decision — see Notes):

- **A — re-derive at `ClientInit` (recommended).** The trail params are computed at fire time from blaster/weapon constants; if they are (or can be made) a pure function of state that *does* survive transfer (alignment/type constants already carried), delete the three wire fields (`TransferData` + `NetworkSerialization.cpp:19-21` + `SpawnTransfer.cpp:34-36` pass-through) and have `ClientInit` derive the params the same way the original fire path does. Kills the dead wire bytes and the false-claim path with no shared-state growth. Precondition to verify at implementation: the fire-time params are not per-shot randomized or tweak-drifted in a way arrival-time re-derivation can't reproduce acceptably (visual-only tolerance).
- **B — promote the params to shared SOA fields.** Server then carries real values end-to-end. Costs: three new shared floats per blaster entering `SharedMembers()`/CRC/`Write`/`Read`/`LogDifferences` + `BlastersPostRender::kiVersion` bump — shared-state growth for a purely client visual, against the `ClientMembers()` pattern's grain. Take only if A's precondition fails.

Either way, correct the Blasters AGENTS.md handoff claim to match the landed behavior.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.cpp` — `TransferRequest` build, `ClientInit`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.h` — SOA membership (option B only)
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — `TransferData` blaster arm
- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — blaster `TransferData` codec
- `Projects/BrokenEngineSandbox/Source/SpawnTransfer.cpp` — arrival pass-through
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/AGENTS.md` — handoff claim

## Out of scope

- Missile smoke-trail transfer handling (separate mechanism, id-carried — verified working).
- The player/spaceship/missile sentinel-conflation transfer bugs — `Frame/TransferSentinelConflation.md`.
- Any change to wind-trail rendering itself.

## Acceptance criteria

- A blaster crossing a cell boundary on a connected client keeps a visually continuous wind trail (server-authoritative replay path, not just local prediction).
- No wire field in the blaster `TransferData` arm is serialized from a value the sender never populates.

## Notes

- **Invariant exposure.** Option A: removes wire fields from the StatusChange payload → save/replay payload layout change → `BlastersPostRender::kiVersion` bump; zero CRC/determinism impact (all edits client-only or codec). Option B: adds shared CRC'd members → `kiVersion` bump + CRC stream change. `ClientInit` runs outside the CRC'd phases on the client only — `#ifdef BT_CLIENT` scope stays as-is.
- **Single open decision for `/external-grill-plan`:** option A vs B (A recommended; B only if fire-time params prove non-derivable at arrival).
- Shares `StatusChange.h`/`NetworkSerialization.cpp`/`SpawnTransfer.cpp` with the transfer trio — co-schedule under the same version bump (see Order.md Dependencies).
