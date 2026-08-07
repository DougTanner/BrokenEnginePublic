<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T21:59:16.178Z","dependsOn":[]} -->
# Deterministic no-island steering fallback in SpaceshipsPostRender::ComputeSteering

## Context

`SpaceshipsPostRender::ComputeSteering` (`Frame/Collections/Spaceships/SpaceshipsNavigation.cpp:61`) unconditionally indexes `islandCandidates[0]`. Its caller builds the span with exactly `rStaticData.islands.size()` entries (`Spaceships.cpp:645`). Zero islands is an accepted static-data state (`FrameStaticData::Read` accepts count zero; `FrameTick.cpp` handles empty island sets), and a spaceship can exist in such a cell: the group spawner has no island-count gate (empty elevation data returns sea-floor elevation, passing terrain clearance), and cross-cell transfer via `SpawnTransfer.cpp:16-26` only requires a live destination. The next PostRender tick then performs an out-of-range access on an empty span — undefined behavior inside bit-deterministic CRC-checked state. Normal procedural generation always places an anchor island; the state is reached through zero-island serialized static data plus spawn or transfer. Verified by /external-deep-analysis Phase-3 review (2026-08-06); pre-existing debt.

## Design

Handle the empty span locally in `ComputeSteering` before any island access: a deterministic no-island branch derived only from shared static data and shared ship state, so client and server take it identically. Chosen fallback: with no island to return to, skip the return-to-island priority and evaluate the existing flee/chase logic unchanged; if that logic also requires an island candidate, hold current heading with the existing turn-rate decay. The nonempty-span path stays byte-for-byte identical — no floating-point reordering, no changed draw schedule.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/SpaceshipsNavigation.cpp`
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp` (caller context, read-only unless the span construction site needs the guard)

## In scope

- `SpaceshipsPostRender::ComputeSteering`: the empty-span guard and the no-island steering branch.

## Out of scope

- Nonempty-island steering behavior, hysteresis constants, and evaluation order; spawn/transfer gating (spawning into zero-island cells stays legal); static-data validation; decomposition of `ComputeSteering` beyond what the guard itself requires.

## Risk tier and invariants

Change Workflow Tier 3. Trigger: determinism/CRC exposure — CRC-checked PostRender behavior in a reachable state. Invariants: nonempty-path PostRender state bit-identical (per-tick CRC unchanged for existing content); fallback deterministic across client/server/replay; `/fp:strict` math untouched.

## Acceptance criteria

- /agent-harness: ticks over zero-island static data with a transferred or spawned spaceship run without invalid access, with matching client/server per-tick CRC.
- Replay determinism check passes on existing (nonempty-island) content with unchanged CRC.
- Client and server build clean through `/compile`.
