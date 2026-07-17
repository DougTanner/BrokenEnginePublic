# ExponentialDecay Input Policy

## Context

`common::ExponentialDecay` in `Common/Math/MathUtils.h:191-195` clamps only the lower bound of its Padé result. A negative scaled time can therefore return a factor above `1.0f`, `-2.0f` divides by zero, and non-finite products rely on incidental floating-point/`std::max` behavior. This contradicts the broad `Common/AGENTS.md` statement that both exponential helpers "never overshoot."

The four current call sites pass positive rates and the fixed positive simulation timestep, so no reachable bug is known. The helper feeds CRC'd client/server simulation, however, and its undocumented edge semantics are a latent misuse hazard.

## Design

1. Decide the supported negative and non-finite scaled-time semantics during `/external-grill-plan` (see Notes).
2. Apply the chosen policy in `common::ExponentialDecay` while preserving the existing arithmetic sequence and bit results for ordinary finite positive products.
3. Update the helper comment and `Common/AGENTS.md` so the no-overshoot claim states the exact supported domain and behavior.
4. Re-scan all call sites and compile both client and server; do not add unit tests.

## Critical files

- `Common/Math/MathUtils.h` — `common::ExponentialDecay` implementation and local contract.
- `Common/AGENTS.md` — deterministic exponential-helper contract.

## Out of scope

- Further changes to `common::ExponentialInterpolant`, which uses the unclamped Padé form; every sim caller stays at scaled time ≤ 0.625 and the two uncapped-DeltaTime UI call sites clamp the interpolant locally.
- Replacing the Padé (1,1) approximation or changing ordinary finite positive decay results.
- Retuning rates, timestep behavior, or any existing call site.
- Adding tests or changing replay, CRC, wire, save, `.pack`, or `kiVersion` formats.

## Acceptance criteria

- Negative, zero, NaN, and positive-infinity scaled-time behavior is intentional and documented.
- Existing finite positive inputs retain their exact expression and bit results.
- `Common/AGENTS.md` no longer makes a broader never-overshoot claim than the chosen contract supports.
- All call sites remain valid under the chosen contract, and both client and server compile.

## Notes

- Open grill decision:
  - **A (recommended): total no-decay fallback.** Treat non-positive or NaN scaled time as no elapsed decay (`1.0f`), positive infinity as complete decay (`0.0f`), and keep the existing clamped expression for finite positive products. This makes the returned factor `[0, 1]` for every input.
  - **B: documented precondition.** Require a finite non-negative scaled time, preserve current edge behavior, and narrow both comments to that domain. This trusts all internal callers but leaves future misuse hazardous.
- This helper executes in shared CRC-fed simulation. The intended change is edge-only and must remain bit-identical for every currently reachable input.
- No allocation-tracked-path, guard-scope, serialized-layout, `.pack`, replay-format, wire, or `kiVersion` change.
