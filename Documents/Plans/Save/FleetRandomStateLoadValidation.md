<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-25T16:52:11.000Z","dependsOn":[]} -->
# Validate Fleet RNG State on Load

## Context

`common::RandomEngine` is xorshift64 (`Common/Math/Random.h:34-42`). State zero is a fixed point: `0 ^ (0 << 13) ^ ... == 0`, so once `uiState` reaches zero every subsequent `RandomNext` returns zero forever.

The codebase treats nonzero state as an invariant and enforces it at both seeding entry points:

```cpp
// Common/Math/Random.cpp:17-21  (Seed)
uiState = uiValue ^ (uiValue >> 31);
if (uiState == 0)
{
    uiState = 1;
}
```

`TimeSeed` repeats the same guard (`Random.cpp:27-31`), and the default member initializer is nonzero (`Random.h:11`, `0xe220a8397b1dcdaf`). No legitimate in-process path can produce zero state.

The fleet save/load path bypasses that invariant. `ReadFleetData` assigns the serialized value straight into the engine with no validation:

```cpp
// Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetSerialization.cpp:184
common::Read(rFileStream, rRandom.uiState);
```

A save file is a trust boundary — the root `AGENTS.md` requires validating anything opaque to the current code unit, explicitly including file reads. `ReadGrid` already documents this for exactly this load path (`GameSaveLoad.cpp:557-559`): *"Trust boundary (save file): a corrupt count/capacity anywhere in the grid / fleet / frame deserialization throws CorruptStreamException … abort the load gracefully."* The fleet RNG state is read inside that boundary but is the one scalar never checked, so a corrupt or crafted save installs a permanently-zero fleet RNG.

The consequence is not confined to weak randomness. `ProcessCreateFleetRequests` mints each fleet identifier from exactly two draws:

```cpp
// ServerFleetManager.cpp
rNewFleet.guid.uiHigh = common::RandomNext(mRandomEngine);
rNewFleet.guid.uiLow = common::RandomNext(mRandomEngine);
```

With zero state every fleet is minted `{0, 0}`, which collides with the repository-wide "absent fleet" sentinel `FleetGuid::IsValid()` (`Fleet.h:13`, true only when a lane is nonzero) and makes every fleet's identifier identical. Downstream, guid-keyed fleet resolution selects the first match, spawn dedup collapses distinct fleets into one key, and `mRememberedFleetGuid` restore (`FleetSelection.cpp:128`) silently fails. Fleet identity is now the addressing mechanism for client requests and for server queues that outlive a poll, so a degenerate identifier misdirects real operations rather than merely weakening randomness.

Found by adversarial review during the completed FleetGuid request re-key. That change consumed the existing `IsValid()` sentinel convention; it did not introduce this gap, and the gap predates it.

## Design

Reject a zero fleet RNG state at the load trust boundary, in `ReadFleetData` (`ServerFleetSerialization.cpp:152-185`).

- Read the value into a local, then validate before installing it into `rRandom`.
- Reject a zero state by throwing `common::CorruptStreamException` (`Common/Serialization.h:13`). Do **not** substitute a reseeded nonzero state: a save that reached this point is corrupt, and silently repairing one field while the rest of the file is unverified produces a plausible-looking but arbitrary world.
- `ReadGrid` already wraps `ReadFleetData` in the `try` documented at `GameSaveLoad.cpp:557-559` and aborts the load gracefully, clearing partial state, so throwing is sufficient and needs no new failure path. The direct precedent is three lines above the call: `iNextGlobalId <= 0` throws `CorruptStreamException("iNextGlobalId")` (`GameSaveLoad.cpp:570-573`) — the same shape of scalar sanity check on a deserialized value. Match it.

Do not infer the failure mode from other fields in `ReadFleetData`: its existing handling is deliberately not uniform (malformed indices throw, non-finite floats are normalized). The throw above is the specified behavior.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetSerialization.cpp` — `ReadFleetData` (`:152-185`), specifically the `rRandom.uiState` read at `:184` and the function's existing corrupt-input handling.
- `Common/Math/Random.h` / `.cpp` — read-only: the zero-state fixed point and the two existing seeding guards that establish the invariant.
- `Projects/BrokenEngineSandbox/Source/Network/Server/ServerFleetManager.cpp` — read-only: fleet identifier minting, the consumer that makes a degenerate state harmful.

## Out of scope

- `Common::RandomEngine` itself. The invariant is already correct at both seeding entry points; the defect is a load path that bypasses them. Do not add a guard inside `RandomNext` — that would cost a branch in a deterministic hot path to compensate for an unvalidated file read.
- Every other field in the save format. Only the fleet RNG state is proven degenerate-on-zero here; a broader save-validation audit is separate work.
- The `FleetGuid::IsValid()` sentinel convention. It is established, used by the reconnect path and by the fleet request/queue identity, and is correct given a nonzero RNG.
- Replay determinism and `Frame::kiVersion`. Rejecting corrupt input changes no valid-input behavior.
- Save format layout. This adds validation, not a field or a version bump.

## Risk tier and invariants

**Tier 3** — trust-boundary work. The root `AGENTS.md` excludes trust boundaries from Tier 2, and this change alters what a save file is permitted to install. No wire, layout, or `kiVersion` exposure; runtime risk is low because only rejection of already-invalid input is added.

- A valid save must load with byte-identical behavior to today; validation must reject only genuinely invalid input.
- No partial application: a rejected save must not leave fleets half-installed.
- The save format is unchanged, so no `kiVersion` movement and no compatibility shim.

## Acceptance criteria

- A save whose serialized fleet RNG state is zero is rejected at load with a `kError` log, and no fleet state from that save is left installed.
- An ordinary save round-trips unchanged: save, load, and confirm fleet identifiers and subsequent RNG draws match a run without the save cycle.
- After a rejected load, creating a fleet still mints a valid nonzero identifier.

## Notes

- The mechanism is verified, but reaching it requires a corrupt or crafted save; no in-process path produces zero state. That is precisely why it belongs at a trust boundary rather than being defended downstream.
- Adversarial review also raised, as a pre-existing residual outside this plan, that `ClientGuid` functions as an unauthenticated bearer identity at `Engine/Source/Network/Server/ServerReceive.cpp:306-336`. That is a separate trust question and is not addressed here.
