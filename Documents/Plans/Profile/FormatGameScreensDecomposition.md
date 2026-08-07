<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:04:46.015Z","dependsOn":[]} -->
# Decompose ProfileManager::FormatGameScreens into per-screen and per-section formatters

## Context

`game::ProfileManager::FormatGameScreens` (`Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.cpp:78-368`) has cyclomatic complexity 43 across 194 sloc and concentrates 95.8% file structuralErosion (corpus 55.3%). It bodies two fully independent overlay screens — Frames (lines 80-139) and Network (lines 141-367) — each with its own `ScopedWorkbufferArena` and its own `UpdateTextArea` sink, sharing no state; the Network branch is already self-delimited into Transport, Sync, Prediction, Clock, and Reconciliation sections by `// -- X --` comments. Repeated local logic the split removes: the coord-frame + `iSnapshotCount > 0` guard at lines 128-129 and 280-281, and `kSimConfig = engine::GetNetworkSimulationConfig(keNetworkSimulation)` re-declared at lines 148, 188, and 205. Verified by /external-deep-analysis over `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.cpp` (2026-08-07, Phase-3 confirmed); pre-existing debt. Client-only overlay formatting outside CRC/deterministic state.

## Design

Behavior-preserving in-function decomposition: split `FormatGameScreens` into client-only private methods `FormatFramesScreen` and `FormatNetworkScreen`; within the Network screen, extract the five comment-delimited section formatters as private client-only methods on `game::ProfileManager`, declared in the existing `BT_CLIENT` private section of `ProfileManager.h`, taking the workbuffer and the locals they share. A plain per-screen/per-section split measurably leaves Frames at ~cc 11 and Transport at ~cc 12, so additionally split the Frames screen into grid-map rendering versus tick formatting, and the Transport section into peer metrics versus traffic-rate formatting (verification-measured per-section cc for the rest: Sync ~5, Prediction ~8, Clock ~1, Reconciliation ~2). Constraints:

- Append order and emitted overlay bytes stay identical for both screens.
- `mSmoothedRtt`/`mSmoothedJitter` mutations (current lines 240-243) stay inside the Transport formatter's `pPeer != nullptr` path; later sections do not consume them, verified safe.
- All extracted code keeps its current `BT_CLIENT` guard scope; no public signature changes; `AppendBytes` stays a file-local helper.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.cpp`
- `Projects/BrokenEngineSandbox/Source/Profile/ProfileManager.h` (private client-only method declarations only)

## In scope

- `ProfileManager::FormatGameScreens` and the new per-screen and per-section formatters extracted from it, including the shared coord-frame/snapshot guard and single `kSimConfig` acquisition per formatter.
- The private `BT_CLIENT` method declarations those formatters need in `ProfileManager.h`.

## Out of scope

- Any change to emitted overlay text, thresholds, warning `"!"` logic, or screen selection; `ProfileManagerBase` and other engine profiling code; `NetworkGraphs.cpp`; `Save/GameSaveLoad.cpp` (explicitly excluded by the originating analysis); public API changes.

## Risk tier and invariants

Change Workflow Tier 1. Trigger: none of the Tier-2/3 surfaces — local behavior-preserving arrangement of client-only overlay code with no public signature or invariant exposure; the overlay stays outside CRC-checked PostRender state. Invariants: allocation-free formatting through the workbuffer preserved; `BT_CLIENT` affinity unchanged.

## Acceptance criteria

- No function resulting from the decomposition exceeds cyclomatic complexity 10 (code-quality-metrics snapshot of the file).
- Frames and Network overlay text byte-identical to pre-change for the same state (harness text-area query or screenshot comparison).
- Client and server build clean through `/compile`.
