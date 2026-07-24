<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:49:13.000Z","dependsOn":[]} -->
# Refactor: Landing Lock Command Coherence

## Context

Source: /external-refactor-clean on `Tools/` recursively.

`toolcli::RunLandingLockCommand` in `Tools/WorktreeCli/LandingLockCommands.cpp` (currently lines 109-291, ~1,393 `bt-token-v1`) handles all six landing-lock verbs (`claim`, `status`, `refresh`, `recover`, `release`, `steal`) in one function. Sibling conflict paths print sanitized machine-readable lease state via `PrintMetadata(landing::LandingStatus(...))` or `{"held":false}` before returning `kiExitStateConflict` (exit 2), but four reachable exit-2 paths return silently:

- `refresh` when no lock record exists (`bExists` false, so the `if (bExists)` print is skipped);
- `refresh` clock rollback (`uiHeartbeatTicks < lease->uiHeartbeatTicks`);
- `recover` when no lock record exists (same skipped print);
- `recover` revalidation mismatch (guarded re-read where `revalidatedMetadata != metadata`).

Scripted consumers (`.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1`, `Test-LandingLockStatusFixtures.ps1`) parse stdout on conflict, so a silent exit 2 gives them no lease state to report.

## Design

Both changes are internal to `LandingLockCommands.cpp`. `RunLandingLockCommand`'s signature and `LandingLockCommands.h` are unchanged; new helpers are file-local additions to the existing anonymous namespace.

### 1. Verb-handler split of `RunLandingLockCommand`

- Keep in `RunLandingLockCommand`: verb parsing/validation, `ParseLocator`, the required-argument and `landing::IsValidLeaseDuration` checks, `EnsureParentDirectory`, `coordination::Guard` acquisition, and the initial existence check plus `ReadMetadata` load.
- Extract one file-local handler per transition verb — claim, refresh, recover, and a combined release/steal handler (release and steal share the existing joint no-record/wrong-owner conflict check at the current lines 265-276) — each taking the already-loaded context (locator, metadata, `bExists`, and the parsed arguments it needs) and returning the exit code. Handler names are trivial local detail (e.g. `HandleClaim`, `HandleRefresh`, `HandleRecover`, `HandleReleaseOrSteal`).
- The single `Guard` is acquired once in `RunLandingLockCommand` before the record load and spans every handler's full critical section; handlers acquire nothing.
- The `status` verb may stay inline or become a handler — trivial local choice.
- Do not generalize the split to harness or plan locks.

### 2. Safe conflict-status emitter for all exit-2 paths

- Add one file-local emitter used by every landing-lock exit-2 return: it prints `PrintMetadata(landing::LandingStatus(metadata, *locator))` when a readable current record exists, and `{"held":false}\n` when no record exists. This makes the four silent paths listed in Context emit state; already-emitting conflict paths route through the same emitter unchanged.
- Recover revalidation mismatch: emit the sanitized status of the newly re-read valid record (`revalidatedMetadata`), not the stale `metadata`.
- Successful `release` keeps its empty stdout; all success outputs and all exit codes are byte-for-byte unchanged.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change below, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way.

### In scope

- `Tools/WorktreeCli/LandingLockCommands.cpp`: `RunLandingLockCommand` only, plus new file-local handler/emitter functions added to the existing anonymous namespace, plus any mechanical necessities (forward declarations) those additions require. `MakeLandingLocator` and `ParseLocator` are untouched.
- `.agents/skills/finalize-changes/scripts/Test-LandingLockStatusFixtures.ps1`: assertion additions/adjustments only, covering the newly emitting exit-2 paths; no restructuring of the fixture harness.

### Out of scope

- Lease duration rules, ownership rules, recovery authorization (`AllRegisteredWorktreesClear`), lease validation (`ValidateLandingLease`), private-field filtering (`LandingStatus`), or lock storage/locator resolution — `LandingLockLifecycle.h`/`.cpp` and `CoordinationStore` are untouched.
- Replacing argument parsing or unifying the landing/harness/plan lock state machines.
- Changing any success output, the successful-release empty stdout, or any exit code.
- `Tools/WorktreeCli/LandingLockCommands.h` (no signature change), every other WorktreeCli command, and `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1` (compatibility is verified, not edited; new output appears only where none existed).

## Critical files

- `Tools/WorktreeCli/LandingLockCommands.cpp` — the only C++ file changed.
- `Tools/WorktreeCli/LandingLockCommands.h` — read for context; unchanged.
- `Tools/WorktreeCli/LandingLockLifecycle.h` — consumed API (`LandingLease`, `ValidateLandingLease`, `LandingStatus`, `NewLandingMetadata`); unchanged.
- `.agents/skills/finalize-changes/scripts/Invoke-FinalizeLanding.ps1` — downstream consumer; compatibility check only.
- `.agents/skills/finalize-changes/scripts/Test-LandingLockStatusFixtures.ps1` — existing landing-lock fixtures; assertions extended.

## Risk tier

Tier 3 trigger: primary landing coordination and the machine-readable conflict contract consumed by finalize scripting.

Invariants: landing lease ownership and primary-mutation coordination semantics are preserved exactly; no engine CRC, `.pack`, replay, client/server, or allocation-tracked runtime exposure.

## Acceptance criteria

- Claim, refresh, recover, and release/steal transition handlers are independently auditable functions, with the single `coordination::Guard` acquired in `RunLandingLockCommand` spanning each critical section.
- Every negative transition returns exit 2 with sanitized current state or `{"held":false}` on stdout — specifically the four previously silent paths: refresh with no record, refresh clock rollback, recover with no record, and recover revalidation mismatch (which emits the re-read record's sanitized status).
- All success outputs, successful-release empty stdout, private-record filtering, and exit codes are unchanged; `Invoke-FinalizeLanding.ps1` behavior remains compatible without edits.
- `Test-LandingLockStatusFixtures.ps1` passes with its extended assertions; WorktreeCli compiles.

## Notes

- Original effort anchors: handler split ~1h; conflict-status emitter ~30m.
- Unspecified edge retained from the original plan: a recover revalidation re-read that fails to parse (record present but unreadable under the guard) still exits 2; the original plan defines emitter output only for "readable record" and "absent record", so this race path's output is left to the readable/absent rule as implemented.
