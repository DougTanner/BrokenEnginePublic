<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-01T19:56:09.409Z","dependsOn":[]} -->
# Make the materialize fixture's low-space cancellation case reachable or loud

## Context

`.agents/scripts/Test-DataPackerMaterializeData.ps1` owns the executable fixture for `DataPacker --materialize-data`. Its seventh case (lines 184-215) proves that a low-disk-space warning cancels materialization while preserving the output symlink and the source bytes. It creates a sparse file inside the source Data directory so DataPacker's computed allocation is large enough to push projected free space under the warning threshold, without physically consuming that space.

That case is now unreachable on high-capacity or mostly empty volumes, and the fixture still exits `0` when it is skipped.

Verified mechanism, with `A` = `DriveInfo.AvailableFreeSpace` on the scratch volume:

- `.agents/scripts/Test-DataPackerMaterializeData.ps1:188` — `$warningBytes = 10L * 1024 * 1024 * 1024`, mirroring `DataPacker/Source/DiagnosticReporter.cpp:137` (`const uint64_t uiWarning = 10ull << 30;`).
- `.agents/scripts/Test-DataPackerMaterializeData.ps1:189` — `$sparseLength = max(1 MiB, A - $warningBytes + 1 MiB)`.
- `.agents/scripts/Test-DataPackerMaterializeData.ps1:190` — `$conservativeAllocation = $sparseLength + 1 MiB`, so for any volume larger than the threshold it equals `A - 10 GiB + 2 MiB`.
- `.agents/scripts/Test-DataPackerMaterializeData.ps1:191` — `$reserveBytes = max(1 GiB, ceil($conservativeAllocation * 0.05))`, mirroring `DataPacker/Source/DiagnosticReporter.cpp:120` (`const uint64_t uiReserve = (std::max)(1ull << 30, (uiAllocation * 5 + 99) / 100);`).
- `.agents/scripts/Test-DataPackerMaterializeData.ps1:193` — `$sparseReady = $conservativeAllocation + $reserveBytes -le A`.

Substituting reduces that precondition to `$reserveBytes <= 10 GiB - 2 MiB`, and therefore to `0.05 * (A - 10 GiB + 2 MiB) <= 10 GiB - 2 MiB`, i.e. `A <= 21 * 10 GiB - 42 MiB`, about **210 GiB**. Above roughly 210 GiB of free space no allocation value satisfies both halves of DataPacker's own arithmetic at once: reaching the warning branch needs `A - allocation < 10 GiB`, while avoiding the hard-failure branch at `DiagnosticReporter.cpp:122` needs `allocation + max(1 GiB, 5% of allocation) <= A`, and the 5% reserve exceeds the fixed 10 GiB gap once allocation passes about 200 GiB. The two windows stop overlapping.

When the precondition is false the fixture takes `.agents/scripts/Test-DataPackerMaterializeData.ps1:214`:

`Write-Warning "LIMITATION: cancellation setup requires sparse-file support and enough free space; fsutil output: $($sparseOutput -join '; ')"`

`Write-Warning` neither throws under `$ErrorActionPreference = 'Stop'` nor sets a failing exit code, so control falls through to `Write-Host 'PASS DataPacker --materialize-data fixture'` at line 217 and the script exits `0`. The per-case `PASS low-space cancellation preserves links and source` line (line 211) is skipped, but the fixture as a whole reports success with the cancellation path never exercised — a warning line in a long transcript is the only signal.

Before the fix that introduced the fixed threshold, `uiWarning` was `max(10 GiB, 10% of the volume's total size)`, and the fixture mirrored it. A volume-proportional warning kept the window open at every capacity: with `W = 0.1 * total` and `A <= total`, the required `0.05 * (A - W) <= W` always held. Replacing it with an absolute 10 GiB (the accepted fix, which is not being revisited here) removed that scaling and created this gap.

Evidence that the case still runs on this host: after the threshold fix, the fixture was run on a scratch volume with about 76 GB free and exited `0` with all seven `PASS` lines and no `LIMITATION` notice. This is a latent coverage gap on larger or emptier machines, not a present failure.

## Design

Required behaviour: the low-space cancellation case must either actually exercise DataPacker's cancellation path on the host it runs on, or make the fixture fail loudly. A run in which the case did not execute must never be reportable as a passing fixture run.

Sparse-file support genuinely can be absent (a non-NTFS or `fsutil`-refused volume), so the "cannot set up" branch cannot simply be deleted; what it does on the way out is the decision.

The executing session picks one approach. Verified options, with trade-offs:

1. **Fail instead of warn.** Replace line 214's `Write-Warning` with a `throw` (or an explicit non-zero exit) so an unsatisfiable precondition fails the fixture. Smallest change and fully honest, but it turns every host with more than ~210 GiB free — or without sparse-file support — into a hard fixture failure with no way to pass, which would block ordinary verification runs.
2. **Distinct non-passing outcome.** Keep the other six cases passing but end the run with a distinct exit code and a final `SKIPPED`/`INCOMPLETE` line instead of the unconditional `PASS DataPacker --materialize-data fixture` at line 217, so callers can tell partial from full coverage. Honest and non-blocking, but recovers no coverage; every caller of the script must then treat the new exit code correctly.
3. **Let the operator supply a suitable volume.** Add a scratch-root parameter to the `param()` block (lines 2-5) so the fixture's scratch tree, and therefore `$drive` at line 187, can be placed on a volume where the window exists, combined with option 1 or 2 when it does not. Recovers real coverage on machines that have any small volume, but needs caller knowledge and does nothing on single-volume hosts.
4. **Create a controlled small volume.** Have the fixture attach a small VHD for the case. Makes coverage host-independent in principle, but normally requires elevation, mutates machine storage state, and adds cleanup failure modes to a fixture that currently only touches a temp directory.
5. **Drive the branch directly.** Give DataPacker a supported way to reach the warning decision regardless of host free space — for example an environment-gated override of the available-space or threshold inputs to `ReportMaterializationDiskSpace`, in the same style as the existing `BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT` guard the fixture already sets. Makes the case deterministic on every host, but adds a verification-only input to shipping tool code, so it needs explicit user approval and raises the risk tier (see below).

Options 1-4 are fixture-only. Option 5 changes `DataPacker/Source/DiagnosticReporter.cpp` and/or its caller and must be approved before implementation.

Whatever is chosen, keep the fixture's mirrored constants (lines 188 and 191) traceable to `DiagnosticReporter.cpp:120` and `:137`; the fixture drifting from those constants is what produced this gap.

## Critical files

- `.agents/scripts/Test-DataPackerMaterializeData.ps1` — lines 184-215 hold the whole case; lines 2-5 hold the parameter block; line 217 holds the unconditional final `PASS`.
- `DataPacker/Source/DiagnosticReporter.cpp` — `ReportMaterializationDiskSpace` at lines 118-151 defines the reserve, hard-failure, and warning arithmetic the fixture must satisfy; read-only unless option 5 is approved.
- `DataPacker/Source/AGENTS.md` — records that the fixture owns `--materialize-data` coverage and never builds DataPacker.

## In scope

- `.agents/scripts/Test-DataPackerMaterializeData.ps1`, lines 184-215: the sparse-file sizing and `$sparseReady` precondition, the `if ($sparseReady)` assertion block, and the `else` `LIMITATION` branch — change how an unsatisfiable precondition is reported and, for options 3-5, how the case is made satisfiable.
- `.agents/scripts/Test-DataPackerMaterializeData.ps1`, line 217: the final unconditional `PASS DataPacker --materialize-data fixture` line, so it cannot report success for a run that skipped the case.
- `.agents/scripts/Test-DataPackerMaterializeData.ps1`, lines 2-5 `param()` block: only if the chosen option needs a new caller-supplied input.
- `DataPacker/Source/AGENTS.md`: only if the chosen option changes documented fixture invocation or adds a DataPacker verification input.

## Out of scope

- The accepted absolute `10ull << 30` warning threshold at `DataPacker/Source/DiagnosticReporter.cpp:137` and the reserve formula at `:120` — both stay as they are; option 5 adds a gated verification input, it does not change the shipping arithmetic.
- The other six fixture cases (lines up to 182) and the `finally` cleanup block (lines 219-228).
- `FileManager` materialization, staging, and symlink behaviour.
- Any change to `DataPacker/Source/DiagnosticReporter.cpp` unless option 5 is explicitly approved first.
- Converting the fixture into a general test framework, adding unit tests, or restructuring its assertion helpers.

## Risk tier

Tier 1 for options 1-4 — the change is confined to one repository verification script with no public signature, invariant, determinism, serialization, or runtime exposure. Selecting option 5 escalates to Tier 2, because it changes one subsystem's shipping tool behaviour (`DataPacker` diagnostic decision inputs); that selection requires explicit approval before implementation.

## Acceptance criteria

- On a host where the precondition can be satisfied, the fixture still runs the cancellation case and prints `PASS low-space cancellation preserves links and source`, with the existing link and source-byte assertions unchanged.
- On a host or simulated condition where the precondition cannot be satisfied, the fixture does not exit `0` with an unqualified success line. Demonstrate this deterministically — for example by temporarily forcing `$sparseReady` false — and record the observed exit code and final output line.
- If the chosen option recovers coverage (3, 4, or 5), demonstrate the cancellation case executing on a volume or configuration with more than ~210 GiB free, where it currently cannot run.
- The fixture's constants remain consistent with `DataPacker/Source/DiagnosticReporter.cpp:120` and `:137`; state the resulting reachable free-space range after the change.
- No DataPacker build is triggered by the fixture, matching `DataPacker/Source/AGENTS.md`.

## Notes

- Baseline evidence for the current behaviour: commit `274c54576fe6521a533baf2a6c6e6ca9b82381fa` plus the session change that set the fixed 10 GiB threshold.
- The sparse file consumes no physical space, so the `$sparseReady` precondition is not a storage requirement — it is the fixture reproducing DataPacker's own hard-failure guard so the run lands on the warning branch rather than the error branch. Relaxing the precondition alone would move the case onto the wrong branch, not fix it.
