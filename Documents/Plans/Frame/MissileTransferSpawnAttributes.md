<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Missile Per-Instance Attributes Re-Rolled at Every Cell Crossing

## Context

`MissilesPostRender::Spawn(Frame&, const SpawnInfo&)` (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp:437-487`) draws three per-instance attributes from the shared random engine on every non-falling spawn, including the transfer-arrival path where `rInfo.bTransfer` is true (`SpawnTransfer.cpp:41-72` — the `kTransferMissile` arm sets `.bTransfer = true` at `:66`):

- `Missiles.cpp:472` — `fExhaustLength = (flags & kFalling) ? 0.0f : kfMissileExhaustLength + common::Random<kfMissileExhaustLengthRandom>(...)`
- `Missiles.cpp:475` — `pfDeltaRotationMax[iIndex] = (flags & kFalling) ? 0.0f : kfDeltaRotationLimitMin + common::Random<kfDeltaRotationLimitRandom>(...)`
- `Missiles.cpp:479` — `fPitch = (flags & kFalling) ? 0.0f : kfMissilePitchMin + common::Random<kfMissilePitchRandom>(...)`

None of the three is carried in `TransferData` (`Frame/StatusChange.h:87-182`), so this is a different defect class from the "zero = unset" sentinel conflation already fixed: those fields were carried and then discarded; these were never carried. All three are in `MissilesPostRender::SharedMembers()` (`Missiles.h:135`) and therefore CRC'd. Both sides re-draw identically from the deterministic stream, so this is gameplay/presentation state loss, not a desync.

Severity differs per field:

- **`pfDeltaRotationMax` — the one that matters.** Written only at `Spawn`; it is the turn-rate ceiling consumed by the clamp at `MissilesUpdate.cpp:227` (`common::MinAbs(fDeltaRotation, rCurrent.pfDeltaRotationMax[i])`) and by the acceleration blend at `MissilesUpdate.cpp:133`. Re-rolling it at every crossing means a missile's maximum turn authority changes discontinuously each time it leaves a cell, partially undercutting the ramp-delay restoration the transfer-sentinel fix already landed (`pfDeltaRotationDelays` gating at `Missiles.cpp:468-470`).
- **`pfPitches` — audible.** Written only at `Spawn`; feeds the shared looping-voice pitch (`Missiles.h:27-30`). A missile's engine tone jumps at each cell boundary.
- **`pfExhaustLengths` — cosmetic and self-correcting.** `MissilesUpdate.cpp:160` re-randomizes it every live PostRender tick anyway (`Missiles.h:23-26` documents this), so the arrival draw is overwritten on the next tick.

**Decision — `fExhaustLength` is NOT carried.** The value is provably overwritten on the next live tick (`MissilesUpdate.cpp:160`), so carrying it would add wire bytes with no observable effect beyond one tick; minimum sufficient change excludes it. Its arrival draw stays exactly as written today and remains deterministic (both builds execute it under identical shared-state conditions). This plan carries exactly two attributes: `fDeltaRotationMax` and `fPitch`.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change below, add no abstractions, configuration, refactors, or fixes to adjacent code, and touch nothing in a named file beyond the named regions plus the mechanical necessities (includes, declarations) those regions require.

**In scope — exact regions:**

- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h`
  - `struct TransferData`: add `float fDeltaRotationMax = 0.0f;` and `float fPitch = 0.0f;` alongside the existing "Missile timers" block (`:156-162`), and add both to the `SharedMembers()` tie (`:89-108`) so `operator==` compares them.
- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp`
  - `SerializeMissileTransfer` (`:36-53`): write both new floats.
  - `DeserializeMissileTransfer` (`:103-118`): read both new floats at the mirrored positions.
  - `StatusChangeItemWireSize`, `kTransferMissile` case (`:165`): `5 * kiF32` becomes `7 * kiF32`.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.h`
  - `MissilesPostRender::SpawnInfo` (`:154-173`): add `float fDeltaRotationMax = 0.0f;` and `float fPitch = 0.0f;` members.
  - `MissilesPostRender::kiVersion` (`:102`): bump `8` to `9`.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp`
  - `MissilesPostRender::Transfer`, `TransferRequest` build (`:372-390`): populate `.fDeltaRotationMax = rCurrentPostRender.pfDeltaRotationMax[i]` and `.fPitch = rCurrentPostRender.pfPitches[i]`.
  - `MissilesPostRender::Spawn(Frame&, const SpawnInfo&)`, the two draw sites (`:475`, `:479`): gate on `rInfo.bTransfer` (see Design).
- `Projects/BrokenEngineSandbox/Source/SpawnTransfer.cpp`
  - `SpawnTransfer`, `kTransferMissile` arm (`:41-72`): pass `.fDeltaRotationMax = rData.fDeltaRotationMax` and `.fPitch = rData.fPitch` in the `MissilesPostRender::Spawn` designated-initializer call.

**Out of scope:**

- Carrying `pfExhaustLengths` — decided against above; the arrival draw at `Missiles.cpp:472` is untouched.
- Other collections' un-carried spawn attributes — this plan covers missiles only.
- The W-invariant breach on the same transfer path — `Documents/Plans/Frame/MissileVelocityWInvariantBreach.md`.
- Arrival-grace handling — `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md`.
- Any change to `engine::kiMaxStatusChangeBytesPerItem` (the new size fits; see Coordination).
- Any change to the `Frame::kiVersion` base literal (`Frame.cpp:36`) — see Design step 4.

## Design

Carry the two surviving per-instance attributes through transfer and restore them on arrival, mirroring the shape the sentinel fix established for `pfDeltaRotationDelays` (`Missiles.cpp:468-470`):

1. **Wire/data:** add `fDeltaRotationMax` and `fPitch` to `TransferData` (member declarations + `SharedMembers()` tie) and to the missile codec arms in `NetworkSerialization.cpp` — write and read in the same relative position in both helpers (place both immediately after `fDeltaRotation` in each). Update the `kTransferMissile` row of `StatusChangeItemWireSize` to match (`80` bytes becomes `88`).
2. **Capture:** in `MissilesPostRender::Transfer`, populate the two new `TransferRequest` fields from `pfDeltaRotationMax[i]` / `pfPitches[i]`. In `SpawnTransfer`'s `kTransferMissile` arm, forward them into the new `SpawnInfo` members.
3. **Restore:** in `MissilesPostRender::Spawn(Frame&, const SpawnInfo&)`, change the two draw sites to restore the carried value verbatim on transfer and otherwise keep today's expression unchanged, exactly the `pfDeltaRotationDelays` pattern:
   - `pfDeltaRotationMax[iIndex] = rInfo.bTransfer ? rInfo.fDeltaRotationMax : ((flags & kFalling) ? 0.0f : kfDeltaRotationLimitMin + common::Random<kfDeltaRotationLimitRandom>(rFrame.postRender.randomEngine));`
   - `float fPitch = rInfo.bTransfer ? rInfo.fPitch : ((flags & kFalling) ? 0.0f : kfMissilePitchMin + common::Random<kfMissilePitchRandom>(rFrame.postRender.randomEngine));`
   A falling transfer arrival restores whatever the source cell stored, per the "arrival restores carried state verbatim" collection rule; the `kFalling` zeroing applies only to genuine spawns. The exhaust draw at `Missiles.cpp:472` is untouched and still executes on arrival.
4. **Versioning:** removing two draws from the transfer-arrival path changes the deterministic RNG stream, and `TransferData` layout changes the StatusChange wire/save payload — both expected. Bump `MissilesPostRender::kiVersion` `8` to `9`; it propagates into `Frame::kiVersion` (`Frame.cpp:36`) and invalidates saves/replays. Do not also increment the `Frame.cpp:36` base literal — per its own comment (`Frame.cpp:33-35`), the base bump is only for CRC shifts *without* a contributing collection bump.

## Risk tier

**Tier 3.** Triggers: StatusChange wire/save layout change, deterministic RNG stream change, CRC'd member semantics — all inside the `/fp:strict` CRC'd tick. Client and server land together. No `.pack` change.

## Acceptance criteria

- A homing missile crossing a cell boundary keeps its original `pfDeltaRotationMax` (observable: per-missile turn-rate ceiling identical before and after the crossing) and its original `pfPitches` value.
- No `common::Random` draw for a carried attribute (`pfDeltaRotationMax`, `pfPitches`) occurs on the transfer-arrival path; the exhaust draw still occurs.
- Client and server CRCs agree across a run containing missile cell crossings.
- Live verification: harness run with missiles crossing cell boundaries; compare per-missile `pfDeltaRotationMax` across the crossing.

## Coordination

- **Shared per-item wire budget.** `engine::kiMaxStatusChangeBytesPerItem = 120` (`Engine/Source/Network/NetworkSerialization.h:17`) caps every serialized StatusChange item, enforced by the per-item `ASSERT` in `SerializeGroup` (`NetworkSerialization.cpp:239`). `kTransferMissile` is 80 B today (`3 * kiVec4 + kiU32 + 5 * kiF32 + kiI64`); two added floats take it to 88 B, comfortably inside the cap — but re-check the constant before landing, since `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md` also moves per-item sizes. (The completed player-transfer preservation work adds no wire bytes.)
- Frame version/save/replay batch with `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md`: all remaining members shift CRC'd tick state, so co-landing consolidates the save/replay invalidation into one window. The completed player-transfer preservation work already consumed its collection bumps (`MissilesPostRender` 7→8, `PlayersPostRender` 18→19, `SpaceshipsInterpolate` 1→2), and this plan's own `MissilesPostRender::kiVersion` bump (Design step 4) supplies its invalidation when it lands alone.
- `Documents/Plans/Network/StatusChangeWireVersionGate.md`: this plan changes `kTransferMissile`'s wire layout, so whichever of the two lands second must honour the version-gate policy the other establishes.
- `Documents/Plans/Frame/MissileVelocityWInvariantBreach.md` also bumps `MissilesPostRender::kiVersion` and edits the same missile transfer seam. Do not interleave: whichever lands second re-verifies the other's citations in `Missiles.cpp` / `MissilesUpdate.cpp` against current source first.
