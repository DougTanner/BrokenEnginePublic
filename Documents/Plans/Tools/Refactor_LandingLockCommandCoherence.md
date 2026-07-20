# Refactor: Landing Lock Command Coherence

## Context
Source: /external-refactor-clean on `Tools/` recursively. The landing-lock command remains oversized after argument parsing, and several reachable exit-2 paths emit no machine-readable lease state while sibling conflicts do.

## Design

### `Tools/WorktreeCli/LandingLockCommands.cpp` — `RunLandingLockCommand`
- After the parser range, split the 1,393 bt-token-v1 function at lines 109-290 into verb-specific claim, refresh, recover, and release/steal handlers operating on one loaded-record context under the existing `coordination::Guard`; do not generalize harness or plan locks. [~1h]
- Add one safe conflict-status emitter for all landing-lock exit-2 paths at lines 184-188 and 212-275: emit sanitized `LandingStatus` for a readable current record, `{"held":false}` when absent, and the newly re-read valid record after revalidation mismatch; preserve successful release's empty stdout. [~30m]

## Critical files
- `Tools/WorktreeCli/LandingLockCommands.cpp`
- `Tools/WorktreeCli/LandingLockCommands.h`
- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1`
- Existing landing-lock fixtures

## Out of scope
- Changing lease duration, ownership, recovery authorization, validation state, private-field filtering, or lock storage.
- Replacing argument parsing or unifying landing/harness/plan state machines.
- Changing success output or exit codes.

## Acceptance criteria
- Claim, refresh, recover, release, and steal transition handlers are independently auditable while one guard spans each critical section.
- Every negative transition returns current sanitized state or `{"held":false}` with exit code 2, including lease disappearance, clock rollback, and revalidation mismatch.
- Existing successful outputs, private-record filtering, and finalize script behavior remain compatible.

## Notes
- Invariant exposure: landing lease ownership and primary-mutation coordination; no engine CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.
- Tier 3 trigger: primary landing coordination and machine-readable conflict contract.
