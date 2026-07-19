# Cell-Transfer "Zero = Unset" Sentinel Conflation

## Context

The 2026-07-03 Frame review sweep confirmed a recurring defect class at the cell-transfer seam: collection `Spawn(SpawnInfo)` overloads treat `0`/non-positive field values as "not provided → use fresh default", but the transfer path feeds them **live values that legitimately reach zero or negative** — so authoritative state is silently replaced at every cell crossing. All instances are deterministic on both sides (destination cell computes identically) — gameplay-correctness bugs, not desyncs:

- **Player shield refilled to max.** `Players.cpp:460`: `pfShields[iIndex] = rInfo.fShield > 0.0f ? rInfo.fShield : kfPlayerShield;`. Shields hit exactly 0.0 routinely (`fShieldDamage = std::min(pfShields[i], fDamage)`, `PlayersCombat.cpp:213-214`); the transfer build passes the live value (`.fShield = rCurrentPostRender.pfShields[i]`, `PlayersNavigation.cpp:94`). A shield-down player crossing a boundary arrives fully shielded, skipping the regen cooldown. The sibling armor ternary (`Players.cpp:459`) is the same latent trap — currently unreachable (out-of-bounds `continue` in PostCollision precedes damage) but wrong for any future `SpawnInfo` caller.
- **Missile homing ramp re-rolled every crossing.** The `pfDeltaRotationDelays` member of `MissilesPostRender` decrements on live-missile ticks, so it is virtually always negative by the time a missile crosses a cell edge; `MissilesPostRender::Spawn` reads `SpawnInfo::fDeltaRotationDelay > 0.0f` as "provided" and otherwise draws a fresh random — every crossing re-randomizes the delay and re-enters the soft-launch ramp (`fDelayPercent` scaling in `MissilesPostRender::Update`), making homing missiles go briefly sluggish at each boundary. Contradicts the Missiles AGENTS.md claim that the boost-ramp delay is preserved.
- **Spaceship rotation/freeze state dropped.** The Spaceships `TransferRequest` build (`Spaceships.cpp:417-430`) carries position/direction/velocity/alignment/health/cooldown/grace but omits `pfDeltaRotations` and `pfFreezeTimes`; arrival `Spawn` re-inits both to 0 (`Spaceships.cpp:608-609`). Reachable while frozen: `ApplyTerrainBounce` moves position outside the Interpolate integration (`SpaceshipsNavigation.cpp:125`), so a frozen ship can be bounced out of bounds and arrive unfrozen.

## Design

Separate "spawn with defaults" from "restore transferred state" so no live value is ever squeezed through a magnitude sentinel:

1. Give the transfer arrival path authoritative semantics. Preferred shape: a `bool bTransfer` (or a `kTransfer` flag on the existing `SpawnInfo` flags) that switches the affected assignments from `value > 0 ? value : default` to unconditional `value`. `SpawnTransfer` and the `kRespawnPlayer`/`kSpawnPlayer` frame paths already know which case they are — no new wire data needed for the player/missile fields already carried.
2. Apply to: `Players.cpp:459-460` (armor + shield) and the missile `fDeltaRotationDelay` spawn assignment.
3. Add `fDeltaRotation` + `fFreezeTime` to the Spaceships `TransferData` payload, the `TransferRequest` build (`Spaceships.cpp:417-430`), the wire codec (`NetworkSerialization.cpp`), and arrival `Spawn` — restoring instead of zeroing.
4. Update the Missiles AGENTS.md boost-ramp claim if wording survives the fix, and the Collections hub note if the transfer contract gains the explicit flag.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp` — `PlayersPostRender::Spawn` ternaries
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — player `TransferRequest` build
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp` — `MissilesPostRender::Spawn` ternaries
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp` — `TransferRequest` build + arrival `Spawn`
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — `TransferData` spaceship fields (+ optional transfer flag)
- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — spaceship `TransferData` codec arms
- `Projects/BrokenEngineSandbox/Source/SpawnTransfer.cpp` — arrival dispatch

## Out of scope

- Blaster wind-deposit continuity across cell handoff — a brief discontinuity while the client regenerates the deposit emitter is acceptable.
- Player uuid preservation across transfer — owned by `Frame/PlayerTransferUuidPreservation.md`.
- Arrival-grace or pending-countdown semantics — already correct (ride in `TransferData` by design).

## Acceptance criteria

- A player at exactly 0.0 shield crossing a cell boundary arrives at 0.0 shield with its regen cooldown state intact.
- A missile past its ramp crossing a boundary keeps full turn authority (no fresh ramp, no fresh RNG draw for the delay on the transfer path).
- A frozen spaceship bounced across a boundary arrives still frozen with its `fDeltaRotation` preserved.

## Coordination

- Frame version/save/replay batch with `Documents/Plans/Frame/PlayerTransferUuidPreservation.md` and `Documents/Plans/Frame/FireCooldownNegativeFloor.md`: co-land behind one consolidated `Frame::kiVersion` change and one save/replay invalidation; the last lander owns the bump.

## Notes

- **Invariant exposure: high.** All sites are inside the `/fp:strict` CRC'd tick. Removing the transfer-path RNG draw (missile delay re-roll) changes the deterministic RNG stream; adding spaceship `TransferData` fields changes the StatusChange wire/save payload layout → bump the affected collection `kiVersion`s (propagates to `Frame::kiVersion`, invalidating saves/replays). Client and server land together.
- **Single open decision for `/external-grill-plan`:** the mechanism — explicit transfer flag on `SpawnInfo` (recommended; one bool, keeps a single Spawn entry point) vs a dedicated transfer-spawn overload. Also confirm the armor ternary should be fixed now despite being currently unreachable (recommended: yes, same edit).
- Co-schedule with `Frame/PlayerTransferUuidPreservation.md` — shared `TransferData`/codec surface, one shared version bump (see Order.md Dependencies). Shares `PlayersNavigation.cpp` with `Frame/FlagshipLossNavFallback.md` — refresh citations if not co-scheduled.
