# Carried `fArrivalGracePeriod` Discarded at Transfer Arrival

## Context

`fArrivalGracePeriod` is packed, serialized, transmitted — and then thrown away at arrival:

- Producers: `Spaceships.cpp:422` (`.fArrivalGracePeriod = rCurrentPostRender.pfArrivalGracePeriods[i]`) and `PlayersNavigation.cpp:105` (same shape).
- Wire: written at `NetworkSerialization.cpp:32` and `:71`, read back at `:99` and `:136`; the field lives at `StatusChange.h:151` and is in the shared `TransferData` tuple (`StatusChange.h:100`).
- Consumers: `SpawnTransfer.cpp:24` (`kTransferSpaceship`) and `SpawnTransfer.cpp:91` (`kTransferPlayer`) both write `.fArrivalGracePeriod = kfArrivalGracePeriod` — the constant — discarding `rData.fArrivalGracePeriod` entirely.

Consequence: an entity crossing a cell boundary is granted a **fresh full** grace period every crossing instead of the remainder it had. The grace period gates target acquisition (`Spaceships.cpp:92`, `:684`) and decays per tick (`Spaceships.cpp:681`), so repeated crossings can keep an entity perpetually inside its arrival grace. Deterministic on both sides — a gameplay-correctness bug, not a desync. Secondary cost: 4 wire bytes per item wasted on both `kTransferSpaceship` and `kTransferPlayer`.

Same "carried value defaulted at arrival" class as the transfer-sentinel work already landed on these functions.

## Design

Two viable resolutions; pick one, do not do both. **Decision plan (present options).**

- **Option A — consume the carried value.** Change `SpawnTransfer.cpp:24` and `:91` to `.fArrivalGracePeriod = rData.fArrivalGracePeriod`. Two-line change, no wire change, no `TransferData` layout change. Restores the intent the producers and codec already implement. Verify the `Spaceships.cpp:625` `rInfo.fArrivalGracePeriod <= 0.0f` early-out and `Players`' equivalent still behave correctly for an arriving entity whose remaining grace is exactly `0.0f`.
- **Option B — stop paying for it.** Remove `fArrivalGracePeriod` from `TransferData`, both producers, and all four codec sites, and keep the constant at arrival as deliberate behavior. Shrinks `kTransferSpaceship` and `kTransferPlayer` by 4 B each; requires a wire-layout change and its version handling.

Option A is the recommended default: the producers and the codec already express the intent, the current arrival behavior looks like an oversight rather than a decision, and it costs no wire change. Option B is only right if fresh-grace-on-arrival is the *wanted* behavior — that determination belongs to the user, not to the implementer.

## Critical files

- `Projects/BrokenEngineSandbox/Source/SpawnTransfer.cpp` — the two discard sites (`:24`, `:91`).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp` — producer (`:422`), arrival `Spawn` (`:625`, `:638`), per-tick decay (`:681-684`, `:720`).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — producer (`:105`).
- `Projects/BrokenEngineSandbox/Source/Network/NetworkSerialization.cpp` — codec sites (`:32`, `:71`, `:99`, `:136`).
- `Projects/BrokenEngineSandbox/Source/Frame/StatusChange.h` — `fArrivalGracePeriod` (`:151`) and the shared tuple (`:100`).

## Out of scope

- `fTransferLockTimer`, also hardcoded to `1.0f` in the same `kTransferPlayer` initializer immediately above `SpawnTransfer.cpp:91` — not evidenced as a defect; confirm intent only, do not change it here.
- Blaster and missile arrival fields — `Frame/MissileTransferSpawnAttributes.md`.
- Grace-period *duration* tuning or the target-acquisition rules it gates.
- Two surviving magnitude tests of this same "carried value defaulted at arrival" class. Both sit in CRC'd code and both are proven non-defects, so neither is fixed here — recorded so the class is not lost:
  - `Players.cpp:469` — `pfFrameChangeTimers[iIndex] = (rInfo.fFrameChangeTimer > 0.0f) ? rInfo.fFrameChangeTimer : fRandomTimer;`. `SpawnInfo::fFrameChangeTimer` (`Players.h:334`) has no producer repo-wide and `TransferData` does not carry it, so the carried branch is dead and every spawn takes the fresh draw.
  - `Spaceships.cpp:634` — `pfHealths[iIndex] = rInfo.fHealth > 0.0f ? rInfo.fHealth : kfSpaceshipHealth;`. Here the default branch is the live genuine-spawn path (`Frame.cpp:377` leaves `fHealth` at `0.0f`) and the magnitude test is the only spawn-vs-transfer selector, because `SpaceshipsPostRender::SpawnInfo` has no `bTransfer` marker; it never misclassifies, since a spaceship at or below zero health explodes before it can be marked for transfer.
  - `Frame/Collections/AGENTS.md` names both as proven-safe legacy. Converting either to an explicit marker is separate work needing its own `kiVersion` handling.

## Acceptance criteria

- The chosen option is recorded with the user's confirmation of the intended arrival behavior.
- Option A: a spaceship or player with partially elapsed grace that crosses a cell boundary arrives with the *remaining* grace, and one with zero remaining grace can be targeted immediately on arrival.
- Option B: `fArrivalGracePeriod` appears in no producer, codec, or `TransferData` field, and per-item sizes drop by 4 B for both transfer types.
- Client and server CRCs agree across a run containing player and spaceship cell crossings.

## Coordination

- **Shared per-item wire budget.** `engine::kiMaxStatusChangeBytesPerItem = 120` (`Engine/Source/Network/NetworkSerialization.h`) caps every serialized StatusChange item. `kTransferPlayer` is 116 B today, leaving 4 B of headroom. Option B here removes 4 B from that item; Option A adds none. `Documents/Plans/Frame/PlayerTransferUuidPreservation.md` was rewritten on 2026-07-21 and its recommended design now adds no wire bytes, so it no longer competes for this headroom — but re-measure rather than trusting either figure if another plan on this seam lands first, and honour whatever version-gate policy `Documents/Plans/Network/StatusChangeWireVersionGate.md` establishes for a wire-layout change.
- Frame version/save/replay batch with `Documents/Plans/Frame/PlayerTransferUuidPreservation.md` and `Documents/Plans/Frame/MissileTransferSpawnAttributes.md`: all shift CRC'd tick state, so co-landing consolidates the save/replay invalidation into one window. There is no last-lander bump to wait for — the plan that anchored this batch has landed and consumed its own collection bumps (`MissilesPostRender` 7→8, `PlayersPostRender` 18→19, `SpaceshipsInterpolate` 1→2) — so a member landing alone owns its own increment of the `Frame.cpp:36` base literal.
- `Documents/Plans/Network/StatusChangeWireVersionGate.md`: Option B is exactly the case that plan exists for — a StatusChange wire-layout change with no CRC-visible state change, which today bumps no version gate. If Option B is chosen, the two must co-land or the version-gate plan must land first.

## Notes

- **Invariant exposure.** `pfArrivalGracePeriods` is CRC'd on both collections (`Spaceships.h:144` `SharedMembers()`; `Players.h:295-305` `SharedCrcMembers()`). Either option changes stored values at arrival, so `SpaceshipsPostRender::kiVersion` and `PlayersPostRender::kiVersion` bump, propagating to `Frame::kiVersion` (`Frame.cpp:36`) and invalidating saves/replays. Option B additionally changes the wire layout.
- Inside the `/fp:strict` CRC'd tick. Client and server land together. No `.pack` change, no RNG-stream change.
