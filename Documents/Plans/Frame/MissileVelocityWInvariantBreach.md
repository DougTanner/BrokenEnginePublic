<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Missile Velocity W-Invariant Breach at Cell Transfer

## Context

Reproduced twice on 2026-07-21 (server, Debug, two independent harness runs; ticks 9393 and 5929, identical stack):

```
Assert failed: "XMVectorGetW(vec) == 0.0f" at Common\Math\MathUtils.h:48 in common::ValidateVector<false>
  common::ValidateVector<0>                     | MathUtils.h:48
  game::MissilesPostRender::Transfer            | Missiles.cpp:403
  engine::ForEachPostRenderTransfer<...>        | FrameUtils.h:123
  game::FramePostRender::Transfer               | Frame.cpp:189
```

`Missiles.cpp:403` is `common::ValidateVector<false>(request.data.vecVelocity)` on the cell-transfer path. A missile reaches it with a non-zero `W` in `pVecVelocities[i]`, breaking the repo direction/velocity W=0 invariant. This is a hard crash on the server, not a desync — both sides compute identically.

**Mechanism of the W pollution (verified by inspection).** `MissilesUpdate.cpp:155`:

```cpp
vecVelocity = XMVector3Rotate(vecVelocity, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, <jitter angle>));
```

The Windows SDK `XMVector3Rotate` forces the *input* W to 0 but does not force the *output* W. For a pure-Z quaternion the sandwich reduces the W lane to `qw*(vz*qz) - qz*(vz*qw)` — two differently-grouped float products. They are bit-exact zero only when `velocity.z == 0`; otherwise a finite residue of order `|vz| * 1e-7` survives. Nothing downstream restores W: `MissilesUpdate.cpp:230` and `:234` and `MissilesPostRender::Fall` (`Missiles.cpp:501`) all use `XMVectorSetZ`, which touches only the Z lane. Once `fTime >= kfMissileLifetime` the propulsion branch (`MissilesUpdate.cpp:125`) stops running entirely, so a polluted W freezes for the missile's remaining life until a cell crossing trips the assert.

**Unresolved: what produces a non-zero missile `velocity.z`.** Inside the propulsion branch, `MissilesUpdate.cpp:230` zeroes `velocity.z` at the end of every live tick, so the rotate at `:155` should see `vz == 0`. The producer of the non-zero `vz` that makes the residue non-zero was *not* identified. Two hypotheses survive:

- **H1** — some entity's Z diverges from `gBaseHeight`, giving a missile a permanently non-zero `direction.z`; `MissilesUpdate.cpp:137`'s `XMVectorMultiplyAdd(..., pVecDirections[i], vecVelocity)` then re-introduces `vz` each tick before `:155` runs. Predicts `direction.z != 0` at the assert.
- **H2** — a velocity write outside the traced set. Predicts `direction.z == 0` while `velocity.z != 0` at the assert.

Intermittency: a 900-sample run cleared 73 transfers untouched, so the condition is rare.

## Design

Both halves are required. **Fixing only (a) is not acceptable on its own** — the `ValidateVector` assert is the sole signal currently exposing (b), and restoring W silences it while leaving an unexplained out-of-plane missile velocity in CRC'd state.

**(b) first — identify the `velocity.z` producer.** Add temporary diagnostics, reproduce, then remove them:

- dump all four lanes of `pVecVelocities[i]` and `pVecDirections[i]` plus `pFlags[i]` and `pfTimes[i]` immediately before `Missiles.cpp:403`; and/or
- assert `XMVectorGetZ(vecVelocity) == 0.0f` immediately before `MissilesUpdate.cpp:155`.

One reproduction discriminates H1 from H2. Fix the producer that the evidence names; do not guess between them.

**(a) — restore the W invariant after the rotate.** Once (b) is understood, make `MissilesUpdate.cpp:155` leave W at exactly 0 (for example by re-zeroing the W lane on the rotate result). Keep the fix at the rotate site rather than at the transfer boundary, so the invariant holds for every consumer of `pVecVelocities`, not just `Transfer`.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/MissilesUpdate.cpp` — jitter rotate (`:155`), propulsion-branch gate (`:125`), direction accumulate (`:137`), XY-plane clamp (`:230`), falling branch (`:234`).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp` — `MissilesPostRender::Transfer` validation (`:401-403`), `MissilesPostRender::Fall` (`:501`).
- `Common/Math/MathUtils.h` — `ValidateVector` (`:41-50`), the invariant being asserted.
- Whatever the (b) diagnosis names as the `velocity.z` / `direction.z` producer — unknown until the discriminating run.

## Out of scope

- Weakening, removing, or tolerance-widening `ValidateVector` — the assert is correct; the data is wrong.
- A blanket W-scrub at every collection's transfer boundary; this plan fixes one verified producer chain.
- Missile transfer payload completeness (`Frame/MissileTransferSpawnAttributes.md`) and arrival-grace handling (`Frame/TransferArrivalGracePeriodDiscarded.md`).
- The `kfMissileLifetime` expiry path's other behavior — only the frozen-W consequence is in scope.

## Acceptance criteria

- The discriminating diagnostic ran and its output is recorded in the session evidence, naming H1 or H2 and the concrete producer.
- The producer is fixed; a missile's `pVecVelocities[i].z` is zero (or explained) whenever `MissilesUpdate.cpp:155` executes.
- `XMVectorGetW(pVecVelocities[i]) == 0.0f` holds after the jitter rotate.
- Harness repro: 12-24 players with `useMissiles:true` at coord `[0,0]`, fleet destination toggled to `[-1,0]`, sustained crossings of `x = -450`, Debug server, run long enough to exceed the ~6k-9k tick range where both reproductions landed — no `ValidateVector` assert.
- All temporary diagnostics removed before landing.

## Coordination

- Frame version/save/replay batch with `Documents/Plans/Frame/MissileTransferSpawnAttributes.md`, `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md`, and `Documents/Plans/Frame/PlayerTransferUuidPreservation.md`: all shift CRC'd tick state, so co-landing consolidates the save/replay invalidation into one window. There is no last-lander bump to wait for — the plan that anchored this batch has landed and consumed its own collection bumps (`MissilesPostRender` 7→8, `PlayersPostRender` 18→19, `SpaceshipsInterpolate` 1→2) — so a member landing alone owns its own increment of the `Frame.cpp:36` base literal.
- `Documents/Plans/Frame/MissileTransferSpawnAttributes.md` also bumps `MissilesPostRender::kiVersion` and edits the same missile transfer seam. Do not interleave: whichever lands second re-verifies the other's citations in `Missiles.cpp` / `MissilesUpdate.cpp` against current source first.

## Notes

- **Invariant exposure: CRC / `kiVersion`.** `pVecVelocities` is in `MissilesPostRender::SharedMembers()` (`Missiles.h:135`) and therefore CRC'd. Changing the stored W bits (and any fix to the `velocity.z` producer) shifts computed frame CRCs, so `MissilesPostRender::kiVersion` must bump, propagating to `Frame::kiVersion` (`Frame.cpp:36`) and invalidating straddling saves/replays.
- Inside the `/fp:strict` CRC'd tick. Client and server land together. No wire-layout or `.pack` change.
- Server Debug build required for the assert; the harness repro is the acceptance signal, not a unit test.
- Both original crash logs were session scratch and are not retained; the assert text, frame stack, tick numbers, and repro shape above are the durable record.
