# Refactor: Plan Claim Diagnostics

## Context
Source: /external-refactor-clean on `Tools/` recursively. `EnumerateClaims` returns a failing exit code when its claim directory cannot be inspected, but unlike later enumeration failures it emits no decisive diagnostic.

## Design

### `Tools/WorktreeCli/PlanCommands.cpp` — `EnumerateClaims`
- At lines 179-186, distinguish an absent row directory from `std::filesystem::exists` failure: return an empty claim array only for absence, and call `Fail("could not inspect plan row claim directory")` before returning false for an OS error; preserve the documented diagnostic-and-skip behavior for an individually corrupt claim. [~15m]

## Critical files
- `Tools/WorktreeCli/PlanCommands.cpp`
- WorktreeCli plan queue/row fixtures

## Out of scope
- Moving the symbol into the `PlanCoordination` module proposed by `Architecture_PlanCoordinationContract.md`.
- Changing claim enumeration, corrupt-record recovery, schemas, or exit codes.

## Acceptance criteria
- Missing claim directories still produce an empty array and success.
- Directory-inspection failures produce the existing failure exit code plus a decisive diagnostic.
- Queue list/lock/steal fixtures preserve claim ordering and corrupt-record skip behavior.

## Notes
- Invariant exposure: plan-row diagnostic contract only; no engine determinism/CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- If `Architecture_PlanCoordinationContract.md` lands first, apply this fix to the moved `EnumerateClaims` implementation.
