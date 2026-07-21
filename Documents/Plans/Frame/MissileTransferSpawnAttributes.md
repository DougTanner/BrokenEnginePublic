# Missile Per-Instance Attributes Re-Rolled at Every Cell Crossing

## Context

`MissilesPostRender::Spawn` draws three per-instance attributes from the shared random engine **unconditionally**, including on the transfer-arrival path where `rInfo.bTransfer` is true (`SpawnTransfer.cpp:52-70` sets `.bTransfer = true` for `kTransferMissile`):

- `Missiles.cpp:473` — `pfExhaustLengths[iIndex] = kfMissileExhaustLength + common::Random<kfMissileExhaustLengthRandom>(...)`
- `Missiles.cpp:475` — `pfDeltaRotationMax[iIndex] = kfDeltaRotationLimitMin + common::Random<kfDeltaRotationLimitRandom>(...)`
- `Missiles.cpp:480` — `pfPitches[iIndex] = kfMissilePitchMin + common::Random<kfMissilePitchRandom>(...)`

None of the three is carried in `TransferData` at all (`StatusChange.h`), so this is a different defect class from the "zero = unset" sentinel conflation already fixed: those fields were carried and then discarded; these were never carried. All three are in `MissilesPostRender::SharedMembers()` (`Missiles.h:135`) and therefore CRC'd. Both sides re-draw identically from the deterministic stream, so this is gameplay/presentation state loss, not a desync.

Severity differs per field:

- **`pfDeltaRotationMax` — the one that matters.** It is written only at `Spawn` and is the turn-rate ceiling consumed by the clamp at `MissilesUpdate.cpp:227` (`common::MinAbs(fDeltaRotation, rCurrent.pfDeltaRotationMax[i])`) and by the acceleration blend at `MissilesUpdate.cpp:133`. Re-rolling it at every crossing means a missile's maximum turn authority changes discontinuously each time it leaves a cell, partially undercutting the ramp-delay restoration that the transfer-sentinel fix just landed.
- **`pfPitches` — audible.** Written only at `Spawn`; feeds the shared looping-voice pitch (`Missiles.h:27-29`). A missile's engine tone jumps at each cell boundary.
- **`pfExhaustLengths` — cosmetic and self-correcting.** `MissilesUpdate.cpp` re-randomizes it every live PostRender tick anyway (`Missiles.h:23-25` documents this), so the arrival draw is overwritten on the next tick. Included here only because it shares the fix site; it may be left alone if carrying it is not worth the wire bytes.

## Design

Carry the surviving per-instance attributes through transfer and restore them on arrival, mirroring the shape the sentinel fix established:

1. Add `fDeltaRotationMax` and `fPitch` to the missile arm of `TransferData` (`StatusChange.h`) and to the missile `TransferData` codec (`NetworkSerialization.cpp`); populate them from `pfDeltaRotationMax[i]` / `pfPitches[i]` in the missile `TransferRequest` build (`Missiles.cpp`, `MissilesPostRender::Transfer`).
2. In `MissilesPostRender::Spawn`, gate the three draws on `rInfo.bTransfer` exactly as `pfDeltaRotationDelays` is already gated (`Missiles.cpp:469-472`): on transfer, assign the carried value; otherwise draw. Preserve the existing `kFalling` special cases (`0.0f` exhaust/max/pitch for a falling missile).
3. Removing draws from the transfer path changes the deterministic RNG stream — this is expected and is part of the `kiVersion` bump below, not a defect.
4. Decide whether `fExhaustLength` is carried at all (see Out of scope); if it is not, the arrival draw stays but must remain deterministic.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp` — `MissilesPostRender::Spawn` draws (`:473`, `:475`, `:480`), `MissilesPostRender::Transfer` request build (`:380-405`).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.h` — `SpawnInfo` (`:154-170`), `SharedMembers()` (`:135`), the constexpr-range comments at `:23-29`.
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — missile `TransferData` fields.
- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — missile `TransferData` codec arms.
- `Projects/BrokenEngineSandbox/Source/SpawnTransfer.cpp` — `kTransferMissile` arrival dispatch (`:52-70`).

## Out of scope

- The `pfExhaustLengths` carry if the implementer judges the wire cost unjustified — the value is overwritten on the next live tick either way; record the decision in the change.
- Other collections' un-carried spawn attributes — this plan covers missiles only.
- The W-invariant breach on the same transfer path — `Frame/MissileVelocityWInvariantBreach.md`.
- Arrival-grace handling — `Frame/TransferArrivalGracePeriodDiscarded.md`.

## Acceptance criteria

- A homing missile crossing a cell boundary keeps its original `pfDeltaRotationMax` (observable: turn-rate ceiling unchanged before and after the crossing) and its original pitch.
- No `common::Random` draw for a carried attribute occurs on the transfer-arrival path.
- Client and server CRCs agree across a run containing missile cell crossings.

## Coordination

- **Shared per-item wire budget.** `engine::kiMaxStatusChangeBytesPerItem = 120` (`Engine/Source/Network/NetworkSerialization.h`) caps every serialized StatusChange item, enforced by the per-item `ASSERT` in `SerializeGroup`. `kTransferMissile` is 80 B today; two added floats take it to 88 B, comfortably inside the cap — but re-check the value before landing, since `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md` also moves per-item sizes. (`Documents/Plans/Frame/PlayerTransferUuidPreservation.md` no longer does — its rewritten design adds no wire bytes.)
- Frame version/save/replay batch with `Documents/Plans/Frame/PlayerTransferUuidPreservation.md` and `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md`: all shift CRC'd tick state, so co-landing consolidates the save/replay invalidation into one window. There is no last-lander bump to wait for — the plan that anchored this batch has landed and consumed its own collection bumps (`MissilesPostRender` 7→8, `PlayersPostRender` 18→19, `SpaceshipsInterpolate` 1→2) — so a member landing alone owns its own increment of the `Frame.cpp:36` base literal.
- `Documents/Plans/Network/StatusChangeWireVersionGate.md`: this plan changes `kTransferMissile`'s wire layout, so whichever of the two lands second must honour the version-gate policy the other establishes.
- `Documents/Plans/Frame/MissileVelocityWInvariantBreach.md` also bumps `MissilesPostRender::kiVersion` and edits the same missile transfer seam. Do not interleave: whichever lands second re-verifies the other's citations in `Missiles.cpp` / `MissilesUpdate.cpp` against current source first.

## Notes

- **Invariant exposure: high.** `TransferData` layout change → StatusChange wire and save payload change; removing transfer-path RNG draws changes the deterministic stream; all three fields are CRC'd. Bump `MissilesPostRender::kiVersion`, which propagates to `Frame::kiVersion` (`Frame.cpp:36`) and invalidates saves/replays.
- Inside the `/fp:strict` CRC'd tick. Client and server land together. No `.pack` change.
- Live verification: harness run with missiles crossing cell boundaries; compare per-missile `pfDeltaRotationMax` across the crossing.
