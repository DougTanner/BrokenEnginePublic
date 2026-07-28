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

`Missiles.cpp:403` is `common::ValidateVector<false>(request.data.vecVelocity)` on the cell-transfer path inside `MissilesPostRender::Transfer`. A missile reaches it with a non-zero `W` in `pVecVelocities[i]`, breaking the repo direction/velocity W=0 invariant. This is a hard crash on the server, not a desync — both sides compute identically.

**Mechanism of the W pollution (verified by inspection).** In `MissilesPostRender::Update` (`MissilesUpdate.cpp:155`, the `uiRandom == 0` jitter case):

```cpp
vecVelocity = XMVector3Rotate(vecVelocity, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, <jitter angle>));
```

The Windows SDK `XMVector3Rotate` forces the *input* W to 0 but does not force the *output* W. For a pure-Z quaternion the sandwich reduces the W lane to `qw*(vz*qz) - qz*(vz*qw)` — two differently-grouped float products. They are bit-exact zero only when `velocity.z == 0`; otherwise a finite residue of order `|vz| * 1e-7` survives. Nothing downstream restores W: the XY-plane clamp (`MissilesUpdate.cpp:230`), the falling-branch gravity write (`MissilesUpdate.cpp:234`), and `MissilesPostRender::Fall` (`Missiles.cpp:501`) all use `XMVectorSetZ`, which touches only the Z lane. Once `fTime >= kfMissileLifetime` the propulsion branch (gated at `MissilesUpdate.cpp:125`) stops running entirely, so a polluted W freezes for the missile's remaining life until a cell crossing trips the assert.

**Unresolved: what produces a non-zero missile `velocity.z`.** Inside the propulsion branch, `MissilesUpdate.cpp:230` zeroes `velocity.z` at the end of every live tick, so the rotate at `:155` should see `vz == 0`. The producer of the non-zero `vz` that makes the residue non-zero was *not* identified. Two hypotheses survive:

- **H1** — some entity's Z diverges from `gBaseHeight`, giving a missile a permanently non-zero `direction.z`; the acceleration accumulate `XMVectorMultiplyAdd(..., rCurrentInterpolate.pVecDirections[i], vecVelocity)` (`MissilesUpdate.cpp:136`) then re-introduces `vz` each tick before `:155` runs. Predicts `direction.z != 0` at the assert.
- **H2** — a velocity write outside the traced set. Predicts `direction.z == 0` while `velocity.z != 0` at the assert.

Intermittency: a 900-sample run cleared 73 transfers untouched, so the condition is rare.

## Design

Both halves are required, in order. **Fixing only (a) is not acceptable on its own** — the `ValidateVector` assert is the sole signal currently exposing (b), and restoring W silences it while leaving an unexplained out-of-plane missile velocity in CRC'd state.

**(b) first — identify the `velocity.z` producer.** Add temporary diagnostics, reproduce with the harness scenario in Acceptance criteria, then remove them:

- In `MissilesPostRender::Transfer` (`Missiles.cpp`), immediately before the `ValidateVector` calls at `:401-403`: dump all four lanes of `pVecVelocities[i]` and the interpolate `pVecDirections[i]`, plus `pFlags[i]` and `pfTimes[i]`, for the transferring missile.
- And/or in `MissilesPostRender::Update` (`MissilesUpdate.cpp`), immediately before the rotate at `:155`: assert `XMVectorGetZ(vecVelocity) == 0.0f`.

One reproduction discriminates H1 from H2 via the predictions above. Fix the producer that the evidence names; do not guess between them. The producer fix is bounded to the concrete write site the evidence identifies — no speculative hardening of other writers.

**(a) — restore the W invariant after the rotate.** Once (b) is understood, make the jitter rotate at `MissilesUpdate.cpp:155` leave W at exactly 0 (for example by re-zeroing the W lane on the rotate result, e.g. `XMVectorSetW(..., 0.0f)` — exact form is implementer's choice). Keep the fix at the rotate site rather than at the transfer boundary, so the invariant holds for every consumer of `pVecVelocities`, not just `Transfer`.

**Version bump.** `pVecVelocities` is in `MissilesPostRender::SharedMembers()` (`Missiles.h:135`) and therefore CRC'd. Changing the stored W bits (and any fix to the `velocity.z` producer) shifts computed frame CRCs, so bump `MissilesPostRender::kiVersion` (`Missiles.h:102`, currently 8) by one; it propagates into `Frame::kiVersion` through the sum at `Frame.cpp:36` and invalidates straddling saves/replays.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/MissilesUpdate.cpp` — `MissilesPostRender::Update`: jitter rotate (`:155`), propulsion-branch gate (`:125`), direction accumulate (`:136`), XY-plane clamp (`:230`), falling branch (`:234`).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp` — `MissilesPostRender::Transfer` validation (`:401-403`), `MissilesPostRender::Fall` velocity write (`:501`).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.h` — `MissilesPostRender::kiVersion` (`:102`), `SharedMembers()` (`:135`).
- `Common/Math/MathUtils.h` — `ValidateVector` (`:40-49`), the invariant being asserted. Read-only reference; not edited.
- Whatever the (b) diagnosis names as the `velocity.z` / `direction.z` producer — unknown until the discriminating run.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the acceptance criteria; add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants permission to touch only the named functions/regions plus the mechanical necessities (includes, declarations) the named change requires.

**In scope:**

- `MissilesUpdate.cpp`, `MissilesPostRender::Update` only: (1) temporary diagnostic assert immediately before the `uiRandom == 0` rotate at `:155` (removed before landing); (2) the permanent W re-zero on that rotate's result. No other statement in `Update` changes.
- `Missiles.cpp`, `MissilesPostRender::Transfer` only: temporary diagnostic dump before the `ValidateVector` calls at `:401-403` (removed before landing). No permanent edit to `Missiles.cpp` unless the (b) evidence names a producer inside it.
- `Missiles.h`: the `MissilesPostRender::kiVersion` literal at `:102` only.
- The single producer write site named by the (b) evidence, wherever it lives — fixed minimally, with its own affected-site check.

**Out of scope:**

- Weakening, removing, or tolerance-widening `ValidateVector` — the assert is correct; the data is wrong.
- A blanket W-scrub at every collection's transfer boundary; this plan fixes one verified producer chain.
- Missile transfer payload completeness (`Frame/MissileTransferSpawnAttributes.md`) and arrival-grace handling (`Frame/TransferArrivalGracePeriodDiscarded.md`).
- The `kfMissileLifetime` expiry path's other behavior — only the frozen-W consequence is in scope.
- Any change to `MathUtils.h`, unit tests, or speculative hardening of velocity writers the evidence did not implicate.

## Risk tier

Tier 3 — the change alters bits inside the `/fp:strict` CRC'd deterministic tick state (`pVecVelocities` is CRC'd via `SharedMembers()`), triggering the determinism/CRC exclusion from Tier 2. Invariants: direction/velocity W=0 across all `pVecVelocities` consumers; CRC/version gating via `MissilesPostRender::kiVersion`; client and server land together; no wire-layout or `.pack` change.

## Acceptance criteria

- The discriminating diagnostic ran and its output is recorded in the session evidence, naming H1 or H2 and the concrete producer.
- The producer is fixed; a missile's `pVecVelocities[i].z` is zero (or explained) whenever the rotate at `MissilesUpdate.cpp:155` executes.
- `XMVectorGetW(pVecVelocities[i]) == 0.0f` holds after the jitter rotate.
- Harness repro: 12-24 players with `useMissiles:true` at coord `[0,0]`, fleet destination toggled to `[-1,0]`, sustained crossings of `x = -450`, Debug server, run long enough to exceed the ~6k-9k tick range where both reproductions landed — no `ValidateVector` assert.
- `MissilesPostRender::kiVersion` bumped exactly once.
- All temporary diagnostics removed before landing.

## Coordination

- Frame version/save/replay batch with `Documents/Plans/Frame/MissileTransferSpawnAttributes.md` and `Documents/Plans/Frame/TransferArrivalGracePeriodDiscarded.md`: all remaining members shift CRC'd tick state, so co-landing consolidates the save/replay invalidation into one window. The completed player-transfer preservation work already consumed its collection bumps (`MissilesPostRender` 7→8, `PlayersPostRender` 18→19, `SpaceshipsInterpolate` 1→2), so a member landing alone owns its own increment of the `Frame.cpp:36` base literal.
- `Documents/Plans/Frame/MissileTransferSpawnAttributes.md` also bumps `MissilesPostRender::kiVersion` and edits the same missile transfer seam. Do not interleave: whichever lands second re-verifies the other's citations in `Missiles.cpp` / `MissilesUpdate.cpp` against current source first.

## Notes

- Inside the `/fp:strict` CRC'd tick. Client and server land together. No wire-layout or `.pack` change.
- Server Debug build required for the assert; the harness repro is the acceptance signal, not a unit test.
- Both original crash logs were session scratch and are not retained; the assert text, frame stack, tick numbers, and repro shape above are the durable record.
