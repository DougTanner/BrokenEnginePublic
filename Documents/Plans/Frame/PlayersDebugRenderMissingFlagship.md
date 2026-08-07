<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:59:17.498Z","dependsOn":[]} -->
# Suppress origin debug geometry when the flagship row is missing in PlayersInterpolate::DebugRender

## Context

Followers remain in navigation mode 5 for one tick after the flagship row disappears (documented at `Players/AGENTS.md`). `PlayersInterpolate::DebugRender` (`Frame/Collections/Players/PlayersRender.cpp:187`) initializes `vecFlagshipPosition` to zero at line 253 and only changes it when a flagship row is found. During the missing-row tick, lines 272-281 draw the stored-waypoint line/circle and lines 285-290 unconditionally draw the mode-5 destination circle using that zero vector, placing false debug geometry at `(0, 0, base height)`. Debug rendering is live in Debug builds (`kbDebugRender`) for every renderable coordinate. Client-only, outside the CRC. The function is also the file's complexity peak (cyclomatic complexity 16). Verified by /external-deep-analysis Phase-3 review (2026-08-06); pre-existing debt.

## Design

Extract the flagship lookup into a helper that reports found/not-found, and gate both the mode-5 waypoint geometry and the mode-5 destination circle on a found flagship; all other debug geometry is unchanged. While in the function, decompose `DebugRender` into its existing sequential blocks (combat-aim rendering, navigation rendering) so no resulting function exceeds cyclomatic complexity 10; pure code motion, no behavior change beyond the gating fix.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersRender.cpp`

## In scope

- `PlayersInterpolate::DebugRender`: the flagship lookup, the mode-5 geometry gating, and the block extraction described above.

## Out of scope

- Non-debug rendering; PostRender/shared state; navigation behavior; other files.

## Risk tier and invariants

Change Workflow Tier 2 — client-only debug rendering behavior in one subsystem. Invariants: no shared/CRC state read differently; debug geometry keeps reading fully interpolated positions (`Frame/Collections/AGENTS.md`).

## Acceptance criteria

- /agent-harness client debug-render scenario across flagship death: no marker at the origin during the missing-row tick, correct markers before and after.
- No resulting function in the file exceeds cyclomatic complexity 10 (code-quality-metrics snapshot).
- Client builds clean through `/compile`.
