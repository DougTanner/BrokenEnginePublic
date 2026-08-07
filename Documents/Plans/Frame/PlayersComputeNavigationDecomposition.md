<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T22:04:22.522Z","dependsOn":[]} -->
# Decompose PlayersPostRender::ComputeNavigation and unify its duplicated path-query block

## Context

`PlayersPostRender::ComputeNavigation` (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp:151-505`) has cyclomatic complexity 58 (the highest in the repository corpus; file structuralErosion 91.5%) and combines fleet overrides, flagship following, timer transitions, throttle decisions, obstacle probes, five navigation modes, and debug publication. Modes 4 and 5 execute a byte-for-byte identical 36-physical-line path-query block (lines 314-349 and 383-418: path query, profiling, fallback direction, client debug publication) — same-file removable duplication, distinct from the deliberate cross-collection mirrors, which stay untouched. Verified by /external-deep-analysis over `Projects/BrokenEngineSandbox/Source/Frame/Collections` (2026-08-06, Phase-3 confirmed); pre-existing debt. This work was previously owned by the since-removed `FrameCollectionsDeepAnalysis.md` wrapper plan.

## Design

Behavior-preserving in-function decomposition: extract sequential helpers for mode transitions, the recompute decision, and the shared path-query block, static/file-local to `PlayersNavigation.cpp`. Constraints that make this safe and are non-negotiable:

- Random-draw schedule unchanged: mode 5's three mirror draws stay before its path-query call (current lines 308-312) and mode 4's draws stay at their sites (lines 362-374); no draw moves across a mode boundary (`Players/AGENTS.md` mode-4/5 draw-count parity).
- Floating-point expressions keep their exact evaluation order and forms (`/fp:strict`, bit-deterministic CRC-checked PostRender state).
- The unified path-query helper is called from both former block sites with identical arguments and ordering; cached steering and client debug waypoints persist across skipped-pathfinding ticks exactly as today.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp`

## In scope

- `PlayersPostRender::ComputeNavigation` and the new file-local helpers extracted from it, including the single shared path-query helper replacing the duplicated blocks.

## Out of scope

- Any navigation behavior, mode-transition, cadence, or draw-schedule change; other functions and files; cross-collection mirrored shapes; header/API changes.

## Risk tier and invariants

Change Workflow Tier 3. Trigger: determinism/CRC exposure — the function writes bit-deterministic CRC-checked PostRender state. Invariants: bit-identical replay (per-tick CRC unchanged), identical shared random stream, `/fp:strict` math untouched, no heap allocation added in the main loop.

## Acceptance criteria

- No function resulting from the decomposition exceeds cyclomatic complexity 10 (code-quality-metrics snapshot of the file).
- Replay determinism check through /agent-harness passes with per-tick CRC identical to pre-change.
- Client and server build clean through `/compile`.
